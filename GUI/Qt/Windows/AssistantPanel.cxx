/*=========================================================================

  ITK-SNAP Assistant Panel -- implementation.
  Delegates tool schemas and tool execution to SNAPRemoteControl.
  Includes auto-launch capability for bundled and sidecar agent servers,
  along with multi-provider LLM endpoint configuration (Local, OpenAI,
  Anthropic, Gemini, Groq, DeepSeek).

=========================================================================*/
#include "AssistantPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QJsonDocument>
#include <QWebSocket>
#include <QTimer>
#include <QDebug>
#include <QAbstractSocket>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTextCursor>

#include "GlobalUIModel.h"

AssistantPanel::AssistantPanel(QWidget *parent)
  : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);

  // --- Row 1: LLM Provider selection & Apply ---
  auto *providerRow = new QHBoxLayout();
  m_ProviderCombo = new QComboBox(this);
  m_ProviderCombo->addItem("Local / Custom (/v1)", "custom");
  m_ProviderCombo->addItem("OpenAI Cloud", "openai");
  m_ProviderCombo->addItem("Anthropic Cloud", "anthropic");
  m_ProviderCombo->addItem("Google Gemini", "gemini");
  m_ProviderCombo->addItem("Groq Cloud", "groq");
  m_ProviderCombo->addItem("DeepSeek Cloud", "deepseek");
  m_ProviderCombo->addItem("Ollama (Local)", "ollama");

  m_LlmApply = new QPushButton(tr("Use LLM"), this);
  m_LlmApply->setToolTip(tr("Apply selected LLM endpoint and model settings"));
  providerRow->addWidget(new QLabel(tr("Provider:"), this));
  providerRow->addWidget(m_ProviderCombo, 1);
  providerRow->addWidget(m_LlmApply);
  layout->addLayout(providerRow);

  // --- Row 2: Endpoint URL & Model ID ---
  auto *llmRow = new QHBoxLayout();
  m_LlmEndpoint = new QLineEdit("http://localhost:11445/v1", this);
  m_LlmEndpoint->setToolTip(tr("LLM API base URL"));
  m_LlmModel = new QLineEdit("qwen", this);
  m_LlmModel->setToolTip(tr("Model ID (e.g. qwen, gpt-4o, claude-3-5-sonnet-20241022)"));
  m_LlmModel->setMaximumWidth(140);
  llmRow->addWidget(new QLabel(tr("URL:"), this));
  llmRow->addWidget(m_LlmEndpoint, 1);
  llmRow->addWidget(new QLabel(tr("Model:"), this));
  llmRow->addWidget(m_LlmModel);
  layout->addLayout(llmRow);

  // --- Row 3: API Key field (for Cloud Providers) ---
  auto *keyRow = new QHBoxLayout();
  m_ApiKeyInput = new QLineEdit(this);
  m_ApiKeyInput->setEchoMode(QLineEdit::Password);
  m_ApiKeyInput->setPlaceholderText(tr("Optional API Key for Cloud Services (sk-...)"));
  keyRow->addWidget(new QLabel(tr("API Key:"), this));
  keyRow->addWidget(m_ApiKeyInput, 1);
  layout->addLayout(keyRow);

  connect(m_ProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &AssistantPanel::onProviderIndexChanged);
  connect(m_LlmApply, &QPushButton::clicked, this, &AssistantPanel::onApplyLlmClicked);

  m_Transcript = new QTextBrowser(this);
  m_Transcript->setOpenExternalLinks(true);
  layout->addWidget(m_Transcript, 1);

  auto *row = new QHBoxLayout();
  m_Input = new QLineEdit(this);
  m_Input->setPlaceholderText(tr("Ask the assistant..."));
  m_Send = new QPushButton(tr("Send"), this);
  m_StopBtn = new QPushButton(tr("Stop"), this);
  m_StopBtn->setStyleSheet(
    "QPushButton { background-color: #d9534f; color: white; border-radius: 3px; font-weight: bold; padding: 4px 10px; }"
    "QPushButton:hover { background-color: #c9302c; }"
    "QPushButton:disabled { background-color: #e0e0e0; color: #888888; border: 1px solid #ccc; }"
  );
  m_StopBtn->setEnabled(false);
  m_Reconnect = new QPushButton(tr("Reconnect"), this);
  m_Reconnect->setVisible(false);
  row->addWidget(m_Input, 1);
  row->addWidget(m_Send);
  row->addWidget(m_StopBtn);
  row->addWidget(m_Reconnect);
  layout->addLayout(row);

  connect(m_Send,     &QPushButton::clicked, this, &AssistantPanel::onSendClicked);
  connect(m_StopBtn,  &QPushButton::clicked, this, &AssistantPanel::onStopClicked);
  connect(m_Input,    &QLineEdit::returnPressed, this, &AssistantPanel::onSendClicked);
  connect(m_Reconnect, &QPushButton::clicked, this, &AssistantPanel::onReconnectClicked);

  m_Socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  connect(m_Socket, &QWebSocket::connected,           this, &AssistantPanel::onConnected);
  connect(m_Socket, &QWebSocket::disconnected,        this, &AssistantPanel::onDisconnected);
  connect(m_Socket, &QWebSocket::textMessageReceived, this, &AssistantPanel::onTextMessageReceived);
  connect(m_Socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
          this, [this](QAbstractSocket::SocketError){ qWarning() << "[Assistant] socket error:" << m_Socket->errorString(); });

  m_RetryTimer = new QTimer(this);
  m_RetryTimer->setInterval(2000);
  connect(m_RetryTimer, &QTimer::timeout, this, [this]{
    if(m_Socket->state() == QAbstractSocket::UnconnectedState)
      m_Socket->open(QUrl(m_ServerUrl));
  });
}

AssistantPanel::~AssistantPanel()
{
  if(m_Socket) { m_Socket->close(); m_Socket->deleteLater(); }
}

void AssistantPanel::onProviderIndexChanged(int index)
{
  const QString p = m_ProviderCombo->itemData(index).toString();
  if(p == "openai")
  {
    m_LlmEndpoint->setText("https://api.openai.com/v1");
    m_LlmModel->setText("gpt-4o");
    m_ApiKeyInput->setPlaceholderText(tr("Required OpenAI API Key (sk-...)"));
  }
  else if(p == "anthropic")
  {
    m_LlmEndpoint->setText("https://api.anthropic.com/v1");
    m_LlmModel->setText("claude-3-5-sonnet-20241022");
    m_ApiKeyInput->setPlaceholderText(tr("Required Anthropic API Key (sk-ant-...)"));
  }
  else if(p == "gemini")
  {
    m_LlmEndpoint->setText("https://generativelanguage.googleapis.com/v1beta/openai");
    m_LlmModel->setText("gemini-2.0-flash");
    m_ApiKeyInput->setPlaceholderText(tr("Required Google Gemini API Key"));
  }
  else if(p == "groq")
  {
    m_LlmEndpoint->setText("https://api.groq.com/openai/v1");
    m_LlmModel->setText("llama-3.3-70b-versatile");
    m_ApiKeyInput->setPlaceholderText(tr("Required Groq API Key (gsk_...)"));
  }
  else if(p == "deepseek")
  {
    m_LlmEndpoint->setText("https://api.deepseek.com/v1");
    m_LlmModel->setText("deepseek-chat");
    m_ApiKeyInput->setPlaceholderText(tr("Required DeepSeek API Key (sk-...)"));
  }
  else if(p == "ollama")
  {
    m_LlmEndpoint->setText("http://localhost:11434/v1");
    m_LlmModel->setText("qwen2.5-coder");
    m_ApiKeyInput->setPlaceholderText(tr("Optional API Key for local Ollama"));
  }
  else // custom / local
  {
    m_LlmEndpoint->setText("http://localhost:11445/v1");
    m_LlmModel->setText("qwen");
    m_ApiKeyInput->setPlaceholderText(tr("Optional API Key for custom endpoint"));
  }
}

void AssistantPanel::SetModel(GlobalUIModel *model)
{
  m_Model = model;
  m_RemoteControl.SetModel(model);
  connectToServer();
}

void AssistantPanel::ensureAgentServerRunning()
{
  if(m_AgentSpawnAttempted) return;
  m_AgentSpawnAttempted = true;

  const QString appDir = QCoreApplication::applicationDirPath();

  // 1. Primary: Bundled standalone executable next to ITK-SNAP.exe
#ifdef Q_OS_WIN
  QString bundledExe = appDir + "/itksnap-agent.exe";
#else
  QString bundledExe = appDir + "/itksnap-agent";
#endif

  if(QFileInfo::exists(bundledExe))
  {
    qInfo() << "[Assistant] Auto-launching bundled AI Agent from" << bundledExe;
    appendChat("system", tr("Auto-starting background AI agent..."));
    QProcess::startDetached(bundledExe, QStringList());
    return;
  }

  // 2. Environment variable override (ITKSNAP_AGENT_PATH)
  const QString envPath = QString::fromUtf8(qgetenv("ITKSNAP_AGENT_PATH"));
  if(!envPath.isEmpty())
  {
    if(QFileInfo::exists(envPath))
    {
      qInfo() << "[Assistant] Launching AI Agent from ITKSNAP_AGENT_PATH:" << envPath;
      appendChat("system", tr("Starting AI agent from ITKSNAP_AGENT_PATH..."));
      QProcess::startDetached(envPath, QStringList());
      return;
    }
  }

  // 3. Relative developer path candidates in monorepo
  const QStringList candidateAgentDirs = {
    appDir + "/agent",
    appDir + "/../agent",
    appDir + "/itksnap-agent",
    appDir + "/../itksnap-agent"
  };

  for(const QString &dir : candidateAgentDirs)
  {
    QString pyExe = dir + "/.venv/Scripts/python.exe";
#ifndef Q_OS_WIN
    pyExe = dir + "/.venv/bin/python";
#endif

    if(QFileInfo::exists(pyExe))
    {
      qInfo() << "[Assistant] Auto-launching sidecar agent background server from" << dir;
      appendChat("system", tr("Auto-starting background AI agent from %1...").arg(dir));
      QProcess::startDetached(pyExe, QStringList() << "-m" << "server", dir);
      return;
    }
  }

  // 4. First-run status message if agent binary is missing
  appendChat("system", tr("AI Agent sidecar binary (itksnap-agent) not found in application directory.\n"
                          "Ensure itksnap-agent is located alongside ITK-SNAP.exe, or start the agent server externally on ws://127.0.0.1:8077."));
}

void AssistantPanel::connectToServer()
{
  ensureAgentServerRunning();
  appendChat("system", tr("Connecting to %1 ...").arg(m_ServerUrl));
  m_Socket->open(QUrl(m_ServerUrl));
}

void AssistantPanel::onConnected()
{
  qInfo() << "[Assistant] connected to" << m_ServerUrl;
  appendChat("system", tr("Connected to AI Agent sidecar."));
  appendChat("system", tr("💡 Select an LLM Provider above (or enter a custom URL), provide an API key if required, then click 'Use LLM' to start."));
  m_Reconnect->setVisible(false);
  if(m_RetryTimer) m_RetryTimer->stop();
  sendHello();
}

void AssistantPanel::onDisconnected()
{
  qWarning() << "[Assistant] disconnected. close code:" << m_Socket->closeCode()
             << "reason:" << m_Socket->closeReason();
  appendChat("system", tr("Disconnected (retrying connection)..."));
  m_Reconnect->setVisible(true);
  if(m_RetryTimer) m_RetryTimer->start();
}

void AssistantPanel::onReconnectClicked()
{
  m_AgentSpawnAttempted = false;
  connectToServer();
}

void AssistantPanel::onApplyLlmClicked()
{
  if(m_Socket->state() != QAbstractSocket::ConnectedState)
    { appendChat("error", tr("Not connected to the agent server yet.")); return; }

  const QString ep = m_LlmEndpoint->text().trimmed();
  const QString mdl = m_LlmModel->text().trimmed();
  const QString key = m_ApiKeyInput->text().trimmed();

  appendChat("system", tr("Configuring LLM provider: %1 (model: %2) ...").arg(ep, mdl));

  QJsonObject msg;
  msg["type"] = "set_llm";
  msg["base_url"] = ep;
  msg["model"] = mdl;
  if(!key.isEmpty())
  {
    msg["api_key"] = key;
  }
  sendJson(msg);
}

void AssistantPanel::sendJson(const QJsonObject &obj)
{
  m_Socket->sendTextMessage(
    QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void AssistantPanel::sendHello()
{
  QJsonObject hello;
  hello["type"]  = "hello";
  hello["tools"] = m_RemoteControl.GetSupportedCommandSchemas();
  sendJson(hello);
}

void AssistantPanel::onTextMessageReceived(const QString &message)
{
  QJsonObject e = QJsonDocument::fromJson(message.toUtf8()).object();
  const QString type = e["type"].toString();

  if(type == "thought")
    {
    QString txt = e["text"].toString();
    if(!txt.isEmpty())
      {
      m_Transcript->moveCursor(QTextCursor::End);
      if(m_StreamState != STATE_THINKING)
        {
        closeStreamBlock();
        m_Transcript->insertHtml("<br/><div style='background-color:#f5f5fa; border-left:3px solid #7030a0; padding:6px; margin:4px 0;'><b style='color:#7030a0;'>🧠 Model Reasoning:</b><br/><span style='color:#555555; font-style:italic;'>");
        m_StreamState = STATE_THINKING;
        }
      m_Transcript->textCursor().insertText(txt);
      }
    }
  else if(type == "token")
    {
    QString txt = e["text"].toString();
    if(!txt.isEmpty())
      {
      m_Transcript->moveCursor(QTextCursor::End);
      if(m_StreamState == STATE_THINKING)
        {
        closeStreamBlock();
        }
      if(m_StreamState != STATE_OUTPUT)
        {
        m_Transcript->insertHtml("<br/><b style='color:#0a7d5a'>assistant:</b> ");
        m_StreamState = STATE_OUTPUT;
        }
      m_Transcript->textCursor().insertText(txt);
      }
    }
  else if(type == "assistant")
    {
    closeStreamBlock();
    m_Send->setEnabled(true);
    m_StopBtn->setEnabled(false);
    }
  else if(type == "tool_call")
    {
    closeStreamBlock();
    appendToolLine(e["name"].toString(), e["args"].toObject());
    dispatchToolCall(e["id"].toString(), e["name"].toString(), e["args"].toObject());
    }
  else if(type == "error")
    {
    closeStreamBlock();
    appendChat("error", e["text"].toString());
    m_Send->setEnabled(true);
    m_StopBtn->setEnabled(false);
    }
  else if(type == "turn_end")
    {
    closeStreamBlock();
    m_Send->setEnabled(true);
    m_StopBtn->setEnabled(false);
    }
  else if(type == "llm_set")
    {
    appendChat("system", tr("LLM configured successfully: %1").arg(e["label"].toString()));
    }
}

void AssistantPanel::onSendClicked()
{
  const QString text = m_Input->text().trimmed();
  if(text.isEmpty()) return;
  if(m_Socket->state() != QAbstractSocket::ConnectedState)
    { appendChat("error", tr("Not connected to the agent server yet. Connect to the sidecar agent first.")); return; }

  appendChat("you", text);
  m_Input->clear();
  m_Send->setEnabled(false);
  m_StopBtn->setEnabled(true);

  m_StreamState = STATE_IDLE;

  QJsonObject msg;
  msg["type"] = "user";
  msg["text"] = text;
  sendJson(msg);
}

void AssistantPanel::onStopClicked()
{
  m_Send->setEnabled(true);
  m_StopBtn->setEnabled(false);
  if(m_Socket && m_Socket->state() == QAbstractSocket::ConnectedState)
  {
    QJsonObject msg;
    msg["type"] = "stop";
    sendJson(msg);
  }
  appendChat("system", tr("Sent cancellation request to stop LLM turn."));
}

void AssistantPanel::dispatchToolCall(const QString &id, const QString &name,
                                      const QJsonObject &args)
{
  QJsonObject res = m_RemoteControl.ExecuteCommand(name, args);

  QJsonObject reply;
  reply["type"] = "tool_result";
  reply["id"]   = id;
  reply["ok"]   = res["ok"].toBool();
  reply["text"] = res["text"].toString();
  sendJson(reply);
}

void AssistantPanel::appendChat(const QString &who, const QString &text)
{
  QString color = who == "you"       ? "#1f6feb"
                : who == "error"     ? "#cc3333"
                : who == "assistant" ? "#0a7d5a"
                                     : "#888888";
  m_Transcript->append(QString("<b style='color:%1'>%2:</b> %3")
    .arg(color, who.toHtmlEscaped(), text.toHtmlEscaped()));
}

void AssistantPanel::appendToolLine(const QString &name, const QJsonObject &args)
{
  m_Transcript->append(QString("<i style='color:#5580cc'>&#9881; %1(%2)</i>")
    .arg(name.toHtmlEscaped(),
         QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))
           .toHtmlEscaped()));
}

void AssistantPanel::closeStreamBlock()
{
  if(m_StreamState == STATE_THINKING)
    {
    m_Transcript->moveCursor(QTextCursor::End);
    m_Transcript->insertHtml("</span></div><br/>");
    }
  m_StreamState = STATE_IDLE;
}
