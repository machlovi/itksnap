/*=========================================================================
  ITK-SNAP Assistant Panel  (integration scaffold) -- implementation

  Mirrors the tested /wsbridge protocol from itksnap-agent:
    client -> {type:hello, tools:[...]}, {type:user, text}, {type:tool_result,...}
    server -> {type:token|assistant|status|tool_call|error|turn_end, ...}

  Tool calls are executed against IRISApplication via GlobalUIModel::GetDriver()
  -- the SAME public API the shipped DeepLearning/DSS panels use.
=========================================================================*/
#include "AssistantPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonDocument>
#include <QWebSocket>

// ITK-SNAP Logic layer (verified against master)
#include "GlobalUIModel.h"
#include "IRISApplication.h"
#include "ColorLabelTable.h"
#include "SegmentationStatistics.h"
#include "IRISException.h"

AssistantPanel::AssistantPanel(QWidget *parent)
  : QWidget(parent)
{
  // --- native Qt Widgets chat UI (no WebEngine) ---
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);

  m_Transcript = new QTextBrowser(this);
  m_Transcript->setOpenExternalLinks(true);
  layout->addWidget(m_Transcript, /*stretch*/ 1);

  auto *row = new QHBoxLayout();
  m_Input = new QLineEdit(this);
  m_Input->setPlaceholderText(tr("Ask the assistant, e.g. \"segment the left kidney and measure it\""));
  m_Send = new QPushButton(tr("Send"), this);
  row->addWidget(m_Input, 1);
  row->addWidget(m_Send);
  layout->addLayout(row);

  connect(m_Send,  &QPushButton::clicked, this, &AssistantPanel::onSendClicked);
  connect(m_Input, &QLineEdit::returnPressed, this, &AssistantPanel::onSendClicked);

  // --- websocket to the local agent sidecar ---
  m_Socket = new QWebSocket();
  connect(m_Socket, &QWebSocket::connected,           this, &AssistantPanel::onConnected);
  connect(m_Socket, &QWebSocket::disconnected,        this, &AssistantPanel::onDisconnected);
  connect(m_Socket, &QWebSocket::textMessageReceived, this, &AssistantPanel::onTextMessageReceived);
}

AssistantPanel::~AssistantPanel()
{
  if (m_Socket) { m_Socket->close(); m_Socket->deleteLater(); }
}

void AssistantPanel::SetModel(GlobalUIModel *model)
{
  m_Model = model;
  connectToServer();   // connect once we have the application model
}

// ---------------------------------------------------------------------
// protocol
// ---------------------------------------------------------------------
void AssistantPanel::connectToServer()
{
  appendChat("system", tr("Connecting to assistant at %1 ...").arg(m_ServerUrl));
  m_Socket->open(QUrl(m_ServerUrl));
}

void AssistantPanel::onConnected()
{
  appendChat("system", tr("Connected. Registering ITK-SNAP tools."));
  sendHello();
}

void AssistantPanel::onDisconnected()
{
  appendChat("system", tr("Disconnected from assistant server."));
}

void AssistantPanel::sendJson(const QJsonObject &obj)
{
  m_Socket->sendTextMessage(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void AssistantPanel::sendHello()
{
  QJsonObject hello;
  hello["type"]  = "hello";
  hello["tools"] = toolSchemas();
  sendJson(hello);
}

// The tool schemas ITK-SNAP advertises to the LLM. Names/args match the
// dispatchToolCall switch below. (Anthropic-style JSON schema; the agent
// forwards it verbatim to the model as function definitions.)
QJsonArray AssistantPanel::toolSchemas() const
{
  auto obj = [](const char *type){ QJsonObject o; o["type"]=type; return o; };
  QJsonArray tools;

  auto addTool = [&](const QString &name, const QString &desc, const QJsonObject &props,
                     const QJsonArray &required = {}) {
    QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
    if (!required.isEmpty()) schema["required"] = required;
    QJsonObject t; t["name"] = name; t["description"] = desc; t["input_schema"] = schema;
    tools.append(t);
  };

  { QJsonObject p; p["path"] = obj("string");
    addTool("load_image", "Load a medical image volume into ITK-SNAP from a file path.",
            p, QJsonArray{"path"}); }

  { QJsonObject p; p["name"] = obj("string");
    addTool("segment_structure",
            "Segment an anatomical structure by name using ITK-SNAP's deep-learning model.",
            p, QJsonArray{"name"}); }

  { QJsonObject p; p["label"] = obj("integer");
    addTool("measure_volume", "Measure the volume in mL of a segmentation label.", p); }

  addTool("list_labels", "List the labels currently in the ITK-SNAP segmentation.", QJsonObject{});

  { QJsonObject p; p["x"]=obj("integer"); p["y"]=obj("integer"); p["z"]=obj("integer");
    addTool("move_cursor", "Move the ITK-SNAP crosshair to a voxel coordinate.", p); }

  return tools;
}

void AssistantPanel::onTextMessageReceived(const QString &message)
{
  QJsonObject e = QJsonDocument::fromJson(message.toUtf8()).object();
  const QString type = e["type"].toString();

  if (type == "token") {
    m_StreamBuffer += e["text"].toString();          // accumulate streamed answer
  } else if (type == "assistant") {
    m_StreamBuffer.clear();
    appendChat("assistant", e["text"].toString());
  } else if (type == "status") {
    // optional: show a transient "thinking..." indicator
  } else if (type == "tool_call") {
    appendToolLine(e["name"].toString(), e["args"].toObject());
    dispatchToolCall(e["id"].toString(), e["name"].toString(), e["args"].toObject());
  } else if (type == "error") {
    appendChat("error", e["text"].toString());
  } else if (type == "turn_end") {
    if (!m_StreamBuffer.isEmpty()) { appendChat("assistant", m_StreamBuffer); m_StreamBuffer.clear(); }
    m_Input->setEnabled(true);
  }
}

void AssistantPanel::onSendClicked()
{
  const QString text = m_Input->text().trimmed();
  if (text.isEmpty()) return;
  appendChat("you", text);
  m_Input->clear();
  m_Input->setEnabled(false);
  QJsonObject msg; msg["type"] = "user"; msg["text"] = text;
  sendJson(msg);
}

// ---------------------------------------------------------------------
// tool dispatch -> IRISApplication  (executed on the GUI thread)
// ---------------------------------------------------------------------
void AssistantPanel::dispatchToolCall(const QString &id, const QString &name, const QJsonObject &args)
{
  bool ok = true;
  QString result;
  try {
    if      (name == "load_image")        result = toolLoadImage(args, ok);
    else if (name == "list_labels")       result = toolListLabels(args, ok);
    else if (name == "measure_volume")    result = toolMeasureVolume(args, ok);
    else if (name == "move_cursor")       result = toolMoveCursor(args, ok);
    else if (name == "segment_structure") result = toolSegmentStructure(args, ok);
    else { ok = false; result = QString("Unknown tool '%1'.").arg(name); }
  } catch (IRISException &exc) {
    ok = false; result = QString("ITK-SNAP error: %1").arg(exc.what());
  } catch (std::exception &exc) {
    ok = false; result = QString("error: %1").arg(exc.what());
  }

  QJsonObject reply;
  reply["type"] = "tool_result";
  reply["id"]   = id;
  reply["ok"]   = ok;
  reply["text"] = result;
  sendJson(reply);
}

QString AssistantPanel::toolLoadImage(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  const QString path = args["path"].toString();
  IRISWarningList wl;
  // TODO: confirm the exact OpenImage overload in-tree; MAIN_ROLE loads a main image.
  driver->OpenImage(path.toUtf8().constData(), MAIN_ROLE, wl);
  auto size = driver->GetIRISImageData()->GetMainImage()->GetSize(); // for the message
  ok = true;
  return QString("Loaded %1 into ITK-SNAP (%2x%3x%4).")
           .arg(path).arg(size[0]).arg(size[1]).arg(size[2]);
}

QString AssistantPanel::toolListLabels(const QJsonObject &, bool &ok)
{
  ColorLabelTable *lt = m_Model->GetDriver()->GetColorLabelTable();
  QStringList lines;
  for (auto const &kv : lt->GetValidLabels())      // ValidLabelMap: id -> ColorLabel
    lines << QString("%1 = %2").arg(kv.first).arg(kv.second.GetLabel());
  ok = true;
  return lines.isEmpty() ? QString("No labels yet.") : ("Labels: " + lines.join(", "));
}

QString AssistantPanel::toolMeasureVolume(const QJsonObject &args, bool &ok)
{
  const int label = args.value("label").toInt(1);
  SegmentationStatistics stats;
  stats.Compute(m_Model->GetDriver());
  const auto &m = stats.GetStats();
  auto it = m.find(static_cast<LabelType>(label));
  if (it == m.end()) { ok = false; return QString("Label %1 not found.").arg(label); }
  ok = true;
  return QString("Label %1: %2 mL (%3 voxels).")
           .arg(label).arg(it->second.volume_mm3 / 1000.0, 0, 'f', 2).arg(it->second.count);
}

QString AssistantPanel::toolMoveCursor(const QJsonObject &args, bool &ok)
{
  Vector3ui c;
  c[0] = args["x"].toInt(); c[1] = args["y"].toInt(); c[2] = args["z"].toInt();
  m_Model->GetDriver()->SetCursorPosition(c, true);
  ok = true;
  return QString("Crosshair moved to (%1, %2, %3).").arg(c[0]).arg(c[1]).arg(c[2]);
}

QString AssistantPanel::toolSegmentStructure(const QJsonObject &args, bool &ok)
{
  // The one non-trivial tool: delegate to ITK-SNAP's existing deep-learning
  // segmentation (DeepLearningSegmentationModel / nnInteractive), then refresh.
  // TODO: wire to the DeepLearningSegmentationModel the app already owns, e.g.
  //   auto *dl = m_Model->GetDeepLearningSegmentationModel();
  //   dl->SetActiveStructurePrompt(args["name"].toString()); dl->RunSegmentation();
  // For the scaffold we only stub the return + fire the refresh event.
  m_Model->GetDriver()->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Requested segmentation of '%1' via the deep-learning model. (scaffold stub)")
           .arg(args["name"].toString());
}

// ---------------------------------------------------------------------
// chat rendering
// ---------------------------------------------------------------------
void AssistantPanel::appendChat(const QString &who, const QString &text)
{
  QString color = who == "you" ? "#1f6feb" : who == "error" ? "#c33" :
                  who == "assistant" ? "#0a7" : "#888";
  m_Transcript->append(QString("<b style='color:%1'>%2:</b> %3")
                         .arg(color, who.toHtmlEscaped(), text.toHtmlEscaped()));
}

void AssistantPanel::appendToolLine(const QString &name, const QJsonObject &args)
{
  m_Transcript->append(QString("<i style='color:#69f'>&#9881; %1(%2)</i>")
      .arg(name.toHtmlEscaped(),
           QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).toHtmlEscaped()));
}
