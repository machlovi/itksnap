/*=========================================================================

  ITK-SNAP Assistant Panel -- implementation.
  Delegates tool schemas and tool execution to SNAPRemoteControl.
  Includes auto-launch capability for bundled and sidecar agent servers,
  along with first-run UI setup guidance.

=========================================================================*/
#include "AssistantPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonDocument>
#include <QWebSocket>
#include <QTimer>
#include <QDebug>
#include <QAbstractSocket>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>

#include "GlobalUIModel.h"

AssistantPanel::AssistantPanel(QWidget *parent)
  : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);

  // --- LLM endpoint row: point the agent at a real model ---
  auto *llmRow = new QHBoxLayout();
  m_LlmEndpoint = new QLineEdit("http://localhost:11445/v1", this);
  m_LlmEndpoint->setToolTip(tr("LLM API URL (e.g. http://localhost:11445/v1 or http://localhost:11440)"));
  m_LlmModel = new QLineEdit("qwen", this);
  m_LlmModel->setToolTip(tr("Model id served by that endpoint (e.g. qwen, llama3, gpt-4o)"));
  m_LlmModel->setMaximumWidth(120);
  m_LlmApply = new QPushButton(tr("Use LLM"), this);
  llmRow->addWidget(new QLabel(tr("LLM:"), this));
  llmRow->addWidget(m_LlmEndpoint, 1);
  llmRow->addWidget(m_LlmModel);
  llmRow->addWidget(m_LlmApply);
  layout->addLayout(llmRow);
  connect(m_LlmApply, &QPushButton::clicked, this, &AssistantPanel::onApplyLlmClicked);

  m_Transcript = new QTextBrowser(this);
  m_Transcript->setOpenExternalLinks(true);
  layout->addWidget(m_Transcript, 1);

  auto *row = new QHBoxLayout();
  m_Input = new QLineEdit(this);
  m_Input->setPlaceholderText(tr("Ask the assistant..."));
  m_Send = new QPushButton(tr("Send"), this);
  m_Reconnect = new QPushButton(tr("Reconnect"), this);
  m_Reconnect->setVisible(false);
  row->addWidget(m_Input, 1);
  row->addWidget(m_Send);
  row->addWidget(m_Reconnect);
  layout->addLayout(row);

  connect(m_Send,  &QPushButton::clicked, this, &AssistantPanel::onSendClicked);
  connect(m_Input, &QLineEdit::returnPressed, this, &AssistantPanel::onSendClicked);
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

  // 3. Relative developer path candidates
  const QStringList candidateAgentDirs = {
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
  appendChat("system", tr("💡 Tip: Enter your LLM Endpoint URL and Model ID above, then click 'Use LLM' to start chatting."));
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
  appendChat("system", tr("Pointing the assistant at %1 (model: %2) ...").arg(ep, mdl));
  QJsonObject msg;
  msg["type"] = "set_llm";
  msg["base_url"] = ep;
  msg["model"] = mdl;
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

  if(type == "token")
    {
    m_StreamBuffer += e["text"].toString();
    }
  else if(type == "assistant")
    {
    m_StreamBuffer.clear();
    appendChat("assistant", e["text"].toString());
    }
  else if(type == "tool_call")
    {
    appendToolLine(e["name"].toString(), e["args"].toObject());
    dispatchToolCall(e["id"].toString(), e["name"].toString(), e["args"].toObject());
    }
  else if(type == "error")
    {
    appendChat("error", e["text"].toString());
    }
  else if(type == "llm_set")
    {
    appendChat("system", tr("LLM updated: %1 (model: %2)").arg(e["base_url"].toString(), e["model"].toString()));
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

  QJsonObject msg;
  msg["type"] = "user";
  msg["text"] = text;
  sendJson(msg);
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
