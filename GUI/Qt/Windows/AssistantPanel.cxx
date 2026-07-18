/*=========================================================================

  ITK-SNAP Assistant Panel -- implementation.

  Speaks the itksnap-agent /wsbridge protocol:
    out: {type:hello, tools[]}, {type:user, text}, {type:tool_result, id, ok, text}
    in : {type:token|assistant|status|tool_call|error|turn_end, ...}

  All tool execution happens on the GUI thread (QWebSocket slots), so the
  IRISApplication calls below are thread-safe without extra marshalling.

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

#include "GlobalUIModel.h"
#include "IRISApplication.h"
#include "GlobalState.h"
#include "GenericImageData.h"
#include "ImageWrapperBase.h"
#include "LabelImageWrapper.h"
#include "ImageIODelegates.h"        // IRISWarningList
#include "ColorLabelTable.h"
#include "ColorLabel.h"
#include "SegmentationStatistics.h"
#include "SegmentationUpdateIterator.h"
#include "SNAPCommon.h"              // MAIN_ROLE/OVERLAY_ROLE/LABEL_ROLE, Vector3ui, LabelType
#include "SNAPEvents.h"              // SegmentationChangeEvent
#include "IRISException.h"
#include "DisplayLayoutModel.h"
#include "Generic3DModel.h"
#include "SmoothLabelsModel.h"
#include "InterpolateLabelModel.h"
#include "IntensityCurveModel.h"
#include "ImageCoordinateGeometry.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include <unordered_set>
#include <vector>
#include <utility>     // std::swap
#include <algorithm>   // std::min/std::max

AssistantPanel::AssistantPanel(QWidget *parent)
  : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);

  // --- LLM endpoint row: point the agent at a real model (URL or host:port) ---
  auto *llmRow = new QHBoxLayout();
  m_LlmEndpoint = new QLineEdit("http://localhost:11440", this);
  m_LlmEndpoint->setToolTip(tr("LLM API URL or host:port (e.g. http://localhost:11440 or 10.0.0.5:8000)"));
  m_LlmModel = new QLineEdit("localmodel", this);
  m_LlmModel->setToolTip(tr("Model id served by that endpoint"));
  m_LlmModel->setMaximumWidth(140);
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

  // Auto-reconnect: the sidecar agent may start after ITK-SNAP, or drop.
  m_RetryTimer = new QTimer(this);
  m_RetryTimer->setInterval(3000);
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
  connectToServer();
}

/* ------------------------------------------------------------------ */
/* protocol                                                            */
/* ------------------------------------------------------------------ */
void AssistantPanel::connectToServer()
{
  appendChat("system", tr("Connecting to %1 ...").arg(m_ServerUrl));
  m_Socket->open(QUrl(m_ServerUrl));
}

void AssistantPanel::onConnected()
{
  qInfo() << "[Assistant] connected to" << m_ServerUrl;
  appendChat("system", tr("Connected. Registering ITK-SNAP tools."));
  m_Reconnect->setVisible(false);
  if(m_RetryTimer) m_RetryTimer->stop();
  sendHello();
}

void AssistantPanel::onDisconnected()
{
  qWarning() << "[Assistant] disconnected. close code:" << m_Socket->closeCode()
             << "reason:" << m_Socket->closeReason();
  appendChat("system", tr("Disconnected (retrying). Is the itksnap-agent server running?"));
  m_Reconnect->setVisible(true);
  if(m_RetryTimer) m_RetryTimer->start();   // keep trying to reconnect
}

void AssistantPanel::onReconnectClicked()
{
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
  msg["base_url"] = ep;      // sidecar accepts a full URL or a bare host:port
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
  hello["tools"] = toolSchemas();
  sendJson(hello);
}

QJsonArray AssistantPanel::toolSchemas() const
{
  auto typeobj = [](const char *t){ QJsonObject o; o["type"] = t; return o; };
  QJsonArray tools;

  auto addTool = [&](const QString &name, const QString &desc,
                     const QJsonObject &props, const QJsonArray &req = {})
  {
    QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
    if(!req.isEmpty()) schema["required"] = req;
    QJsonObject t; t["name"] = name; t["description"] = desc;
    t["input_schema"] = schema;
    tools.append(t);
  };

  auto numObj = [&](const char *desc){ QJsonObject o; o["type"]="number"; o["description"]=desc; return o; };
  auto intObj = [&](const char *desc){ QJsonObject o; o["type"]="integer"; o["description"]=desc; return o; };
  auto strObj = [&](const char *desc){ QJsonObject o; o["type"]="string"; o["description"]=desc; return o; };

  QJsonObject none;
  auto labelProp = [&](const char *d){ QJsonObject p; p["label"]=intObj(d); return p; };
  auto pathProp  = [&](const char *d){ QJsonObject p; p["path"]=strObj(d); return p; };

  // ---- state / info ----
  addTool("get_scene_overview",
          "Report the current ITK-SNAP state: whether an image is loaded, its dimensions, "
          "intensity range, cursor position, and the defined segmentation labels. Call this "
          "first when you are unsure of the state or need an intensity range to segment.", none);
  addTool("get_cursor_info",
          "Report the current crosshair voxel position and which segmentation label is under it.", none);
  addTool("measure_volume",
          "Measure the volume in mL (and voxel count) of ONE segmentation label.",
          labelProp("Label id (default 1)."));
  addTool("measure_all_labels",
          "Measure the volume in mL of EVERY non-empty segmentation label at once.", none);
  addTool("count_voxels",
          "Count how many voxels currently carry a given label.",
          labelProp("Label id."), QJsonArray{"label"});

  // ---- image i/o ----
  addTool("load_image",
          "Load a main medical image volume into ITK-SNAP from an absolute file path "
          "(NIfTI/DICOM/MHA/NRRD). Replaces the current main image.",
          pathProp("Absolute path to the image file."), QJsonArray{"path"});
  addTool("load_overlay",
          "Load an additional image as an overlay on top of the main image.",
          pathProp("Absolute path to the overlay image."), QJsonArray{"path"});
  addTool("load_segmentation",
          "Load an existing segmentation (label image) from a file into ITK-SNAP.",
          pathProp("Absolute path to the segmentation label image."), QJsonArray{"path"});
  addTool("unload_overlays",
          "Remove all overlay images, leaving only the main image.", none);

  // ---- segmentation ----
  { QJsonObject p;
    p["lower"] = numObj("Minimum intensity to include.");
    p["upper"] = numObj("Maximum intensity (optional; defaults to the image max).");
    p["label"] = intObj("Label id to paint (1-6 exist by default; default 1).");
    p["name"]  = strObj("Human name for the structure (e.g. 'nodule').");
    addTool("threshold_segment",
            "Segment all voxels whose intensity is in [lower, upper] into a label and paint "
            "it into the viewer. Use get_scene_overview first to choose a sensible range.",
            p, QJsonArray{"lower"}); }
  addTool("clear_segmentation",
          "Erase the ENTIRE segmentation (all labels) and start blank.", none);
  addTool("clear_label",
          "Erase only the voxels belonging to one label, leaving other labels intact.",
          labelProp("Label id to erase."), QJsonArray{"label"});
  { QJsonObject p; p["from_label"]=intObj("Existing label to replace.");
    p["to_label"]=intObj("Label to replace it with (0 = clear).");
    addTool("replace_label",
            "Replace every voxel of one label with another label id.",
            p, QJsonArray{"from_label","to_label"}); }

  // ---- labels ----
  addTool("set_active_label",
          "Set the active drawing label (the label future segmentation edits will use).",
          labelProp("Label id to make active."), QJsonArray{"label"});
  { QJsonObject p; p["label"]=intObj("Label id."); p["name"]=strObj("New name.");
    addTool("rename_label", "Rename a segmentation label.", p, QJsonArray{"label","name"}); }
  { QJsonObject p; p["label"]=intObj("Label id.");
    p["r"]=intObj("Red 0-255."); p["g"]=intObj("Green 0-255."); p["b"]=intObj("Blue 0-255.");
    addTool("set_label_color", "Set the RGB color of a segmentation label.",
            p, QJsonArray{"label","r","g","b"}); }

  // ---- navigation / view ----
  { QJsonObject p; p["x"]=intObj("X voxel."); p["y"]=intObj("Y voxel."); p["z"]=intObj("Z voxel.");
    addTool("move_cursor", "Move the ITK-SNAP crosshair to a voxel coordinate.",
            p, QJsonArray{"x","y","z"}); }
  addTool("focus_label",
          "Center the ITK-SNAP view on the middle of a segmentation label so the user sees it.",
          labelProp("Label id to center on."), QJsonArray{"label"});
  { QJsonObject p; QJsonObject e; e["type"]="string";
    e["enum"]=QJsonArray{"all","axial","coronal","sagittal","3d"};
    e["description"]="Which view panel layout to show.";
    p["layout"]=e;
    addTool("set_layout", "Change the ITK-SNAP view layout (all four panels, or a single view).",
            p, QJsonArray{"layout"}); }
  addTool("update_3d_mesh",
          "Rebuild the 3D surface mesh from the current segmentation so it shows in the 3D view.", none);

  // ---- workspace / edit ----
  addTool("save_workspace",
          "Save the current ITK-SNAP workspace (image + segmentation + labels) to a .itksnap file.",
          pathProp("Absolute path for the .itksnap workspace file."), QJsonArray{"path"});
  addTool("load_workspace",
          "Open an ITK-SNAP workspace (.itksnap) file, restoring images and segmentation.",
          pathProp("Absolute path to the .itksnap workspace file."), QJsonArray{"path"});
  addTool("save_statistics",
          "Write/export/save per-label statistics to a FILE ON DISK at the given path. "
          "measure_all_labels only reports numbers in chat; this one is REQUIRED whenever "
          "the user asks to save, export, or write measurements/statistics to a file or path.",
          pathProp("Absolute path for the statistics file (e.g. D:/out/stats.csv)."), QJsonArray{"path"});
  addTool("undo", "Undo the last segmentation edit.", none);
  addTool("redo", "Redo the last undone segmentation edit.", none);

  // ---- advanced segmentation edits ----
  { QJsonObject p; p["label"]=intObj("Label id to smooth (default 1).");
    p["sigma_mm"]=numObj("Gaussian smoothing sigma in millimetres (default 1.0).");
    addTool("smooth_labels",
            "Smooth the boundary of a segmentation label with a Gaussian (removes jagged "
            "edges / staircase artifacts).", p); }
  addTool("interpolate_labels",
          "Interpolate a segmentation across slices that were only labeled on some slices "
          "(fills the gaps between manually drawn slices).",
          labelProp("Label id to interpolate (optional; omit to interpolate all labels)."));

  // ---- display ----
  { QJsonObject p; QJsonObject dir; dir["type"]="string";
    dir["enum"]=QJsonArray{"axial","coronal","sagittal"};
    dir["description"]="Which slice orientation to export.";
    p["direction"]=dir; p["path"]=strObj("Absolute path for the output image (e.g. .png).");
    addTool("export_slice", "Save the current slice (at the crosshair) as a 2D image file.",
            p, QJsonArray{"direction","path"}); }

  // ---- more workspace / files ----
  addTool("unload_main_image", "Close the current main image and everything derived from it.", none);
  addTool("save_annotations", "Save ruler/landmark annotations to a file.",
          pathProp("Absolute path for the annotations file."), QJsonArray{"path"});
  addTool("load_annotations", "Load ruler/landmark annotations from a file.",
          pathProp("Absolute path to the annotations file."), QJsonArray{"path"});
  addTool("save_labels", "Save the label descriptions (names/colors) to a text file.",
          pathProp("Absolute path for the label description file."), QJsonArray{"path"});
  addTool("load_labels", "Load label descriptions (names/colors) from a text file.",
          pathProp("Absolute path to the label description file."), QJsonArray{"path"});

  // ---- contrast / display / label lifecycle (capability audit) ----
  addTool("auto_window_level",
          "Automatically adjust the brightness/contrast (window/level) of the current image "
          "to fit its intensity, so it displays well.", none);
  { QJsonObject p; p["window"]=numObj("Window width (contrast). Optional.");
    p["level"]=numObj("Window level/center (brightness). Optional.");
    addTool("set_window_level",
            "Set the display window (contrast) and/or level (brightness) of the current image "
            "to specific values.", p); }
  { QJsonObject p; p["opacity"]=intObj("Overlay opacity percent 0-100.");
    addTool("set_segmentation_opacity",
            "Set how opaque the segmentation overlay is drawn over the image (0=invisible, 100=solid).",
            p, QJsonArray{"opacity"}); }
  { QJsonObject p; p["label"]=intObj("Label id."); p["opacity"]=intObj("Opacity 0-255.");
    addTool("set_label_opacity", "Set the opacity of one specific segmentation label.",
            p, QJsonArray{"label","opacity"}); }
  { QJsonObject p; p["label"]=intObj("Label id.");
    QJsonObject vb; vb["type"]="boolean"; vb["description"]="true to show, false to hide."; p["visible"]=vb;
    addTool("set_label_visibility", "Show or hide one segmentation label.",
            p, QJsonArray{"label","visible"}); }
  { QJsonObject p; p["name"]=strObj("Name for the new label.");
    p["r"]=intObj("Red 0-255."); p["g"]=intObj("Green 0-255."); p["b"]=intObj("Blue 0-255.");
    addTool("create_label", "Create a new segmentation label with a name and color, using the "
            "next free id.", p, QJsonArray{"name"}); }
  { QJsonObject p; p["label"]=intObj("Label id to delete.");
    addTool("delete_label", "Delete a segmentation label: erase its voxels and remove it from "
            "the label table.", p, QJsonArray{"label"}); }

  return tools;
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
    appendChat("system", tr("LLM ready: %1").arg(e["label"].toString()));
    }
  else if(type == "hello_ack")
    {
    appendChat("system", tr("Registered %1 ITK-SNAP tools.").arg(e["n_tools"].toInt()));
    }
  else if(type == "turn_end")
    {
    if(!m_StreamBuffer.isEmpty())
      { appendChat("assistant", m_StreamBuffer); m_StreamBuffer.clear(); }
    m_Input->setEnabled(true);
    m_Input->setFocus();
    }
}

void AssistantPanel::onSendClicked()
{
  const QString text = m_Input->text().trimmed();
  if(text.isEmpty())
    return;
  appendChat("you", text);
  m_Input->clear();
  m_Input->setEnabled(false);
  QJsonObject msg; msg["type"] = "user"; msg["text"] = text;
  sendJson(msg);
}

/* ------------------------------------------------------------------ */
/* tool dispatch -> IRISApplication (GUI thread)                       */
/* ------------------------------------------------------------------ */
void AssistantPanel::dispatchToolCall(const QString &id, const QString &name,
                                      const QJsonObject &args)
{
  bool ok = true;
  QString result;
  try
    {
    if(name == "get_scene_overview")      result = toolSceneOverview(args, ok);
    else if(name == "get_cursor_info")    result = toolCursorInfo(args, ok);
    else if(name == "measure_volume")     result = toolMeasureVolume(args, ok);
    else if(name == "measure_all_labels") result = toolMeasureAllLabels(args, ok);
    else if(name == "count_voxels")       result = toolCountVoxels(args, ok);
    else if(name == "load_image")         result = toolLoadImage(args, ok);
    else if(name == "load_overlay")       result = toolLoadOverlay(args, ok);
    else if(name == "load_segmentation")  result = toolLoadSegmentation(args, ok);
    else if(name == "unload_overlays")    result = toolUnloadOverlays(args, ok);
    else if(name == "threshold_segment")  result = toolThresholdSegment(args, ok);
    else if(name == "clear_segmentation") result = toolClearSegmentation(args, ok);
    else if(name == "clear_label")        result = toolClearLabel(args, ok);
    else if(name == "replace_label")      result = toolReplaceLabel(args, ok);
    else if(name == "set_active_label")   result = toolSetActiveLabel(args, ok);
    else if(name == "rename_label")       result = toolRenameLabel(args, ok);
    else if(name == "set_label_color")    result = toolSetLabelColor(args, ok);
    else if(name == "move_cursor")        result = toolMoveCursor(args, ok);
    else if(name == "focus_label")        result = toolFocusLabel(args, ok);
    else if(name == "set_layout")         result = toolSetLayout(args, ok);
    else if(name == "update_3d_mesh")     result = toolUpdate3DMesh(args, ok);
    else if(name == "smooth_labels")      result = toolSmoothLabels(args, ok);
    else if(name == "interpolate_labels") result = toolInterpolateLabels(args, ok);
    else if(name == "export_slice")       result = toolExportSlice(args, ok);
    else if(name == "save_workspace")     result = toolSaveWorkspace(args, ok);
    else if(name == "load_workspace")     result = toolLoadWorkspace(args, ok);
    else if(name == "save_statistics")    result = toolSaveStatistics(args, ok);
    else if(name == "unload_main_image")  result = toolUnloadMainImage(args, ok);
    else if(name == "save_annotations")   result = toolSaveAnnotations(args, ok);
    else if(name == "load_annotations")   result = toolLoadAnnotations(args, ok);
    else if(name == "save_labels")        result = toolSaveLabels(args, ok);
    else if(name == "load_labels")        result = toolLoadLabels(args, ok);
    else if(name == "undo")               result = toolUndo(args, ok);
    else if(name == "redo")               result = toolRedo(args, ok);
    else if(name == "auto_window_level")  result = toolAutoWindowLevel(args, ok);
    else if(name == "set_window_level")   result = toolSetWindowLevel(args, ok);
    else if(name == "set_segmentation_opacity") result = toolSetSegmentationOpacity(args, ok);
    else if(name == "set_label_opacity")  result = toolSetLabelOpacity(args, ok);
    else if(name == "set_label_visibility") result = toolSetLabelVisibility(args, ok);
    else if(name == "create_label")       result = toolCreateLabel(args, ok);
    else if(name == "delete_label")       result = toolDeleteLabel(args, ok);
    else { ok = false; result = QString("Unknown tool '%1'.").arg(name); }
    }
  catch(IRISException &exc)
    { ok = false; result = QString("ITK-SNAP error: %1").arg(exc.what()); }
  catch(std::exception &exc)
    { ok = false; result = QString("error: %1").arg(exc.what()); }

  QJsonObject reply;
  reply["type"] = "tool_result";
  reply["id"]   = id;
  reply["ok"]   = ok;
  reply["text"] = result;
  sendJson(reply);
}

QString AssistantPanel::toolSceneOverview(const QJsonObject &, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  ok = true;
  QStringList out;
  if(!driver->IsMainImageLoaded())
    return "No image is loaded. Ask the user for an image path, then call load_image.";

  Vector3ui sz = driver->GetCurrentImageData()->GetMain()->GetSize();
  out << QString("Image loaded: %1 x %2 x %3 voxels.").arg(sz[0]).arg(sz[1]).arg(sz[2]);

  ScalarImageWrapperBase *scalar =
      driver->GetCurrentImageData()->GetMain()->GetDefaultScalarRepresentation();
  if(scalar)
    out << QString("Intensity range: %1 to %2.")
              .arg(scalar->GetImageMinAsDouble(), 0, 'f', 1)
              .arg(scalar->GetImageMaxAsDouble(), 0, 'f', 1);

  Vector3ui c = driver->GetCursorPosition();
  out << QString("Cursor at (%1, %2, %3).").arg(c[0]).arg(c[1]).arg(c[2]);

  ColorLabelTable *lt = driver->GetColorLabelTable();
  QStringList labs;
  for(auto const &kv : lt->GetValidLabels())
    if(kv.first != 0)
      labs << QString("%1=%2").arg(kv.first).arg(QString::fromUtf8(kv.second.GetLabel()));
  out << (labs.isEmpty() ? QString("No labels defined.")
                         : QString("Labels: ") + labs.join(", "));
  return out.join(" ");
}

QString AssistantPanel::toolLoadImage(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  const QString path = args["path"].toString();
  IRISWarningList wl;
  driver->OpenImage(path.toUtf8().constData(), MAIN_ROLE, wl);
  ok = driver->IsMainImageLoaded();
  if(!ok)
    return QString("Failed to load %1.").arg(path);
  Vector3ui sz = driver->GetCurrentImageData()->GetMain()->GetSize();
  return QString("Loaded %1 into ITK-SNAP (%2 x %3 x %4 voxels).")
    .arg(path).arg(sz[0]).arg(sz[1]).arg(sz[2]);
}

QString AssistantPanel::toolThresholdSegment(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  if(!driver->IsMainImageLoaded())
    { ok = false; return "No image is loaded. Load one first."; }

  // -- parameter validation --
  if(!args.contains("lower"))
    { ok = false; return "threshold_segment needs a 'lower' intensity value."; }
  int labelId = args.contains("label") ? args["label"].toInt() : 1;
  if(labelId < 1)   labelId = 1;
  if(labelId > 255) labelId = 255;                     // labels are 8/16-bit ids
  double lower = args["lower"].toDouble();
  double upper = args.contains("upper") ? args["upper"].toDouble() : 1e30;
  QString note;
  if(lower > upper) { std::swap(lower, upper); note = " (swapped reversed bounds)"; }
  const QString name = args.contains("name") ? args["name"].toString() : QString("segmentation");

  GlobalState *gs = driver->GetGlobalState();
  ColorLabelTable *lt = driver->GetColorLabelTable();

  // name the label and make it the active drawing label
  ColorLabel cl = lt->GetColorLabel(labelId);
  cl.SetLabel(name.toUtf8().constData());
  lt->SetColorLabel(labelId, cl);
  gs->SetDrawingColorLabel(static_cast<LabelType>(labelId));

  // type-agnostic float view of the main image
  ScalarImageWrapperBase *scalar =
      driver->GetCurrentImageData()->GetMain()->GetDefaultScalarRepresentation();
  typedef ImageWrapperBase::FloatImageType FloatImageType;
  FloatImageType::Pointer fimg = scalar->CreateCastToFloatPipeline("assistant_thresh");
  fimg->Update();

  LabelImageWrapper *seg = driver->GetSelectedSegmentationLayer();
  if(!seg)
    { ok = false; return "No segmentation layer available."; }

  const auto region = seg->GetImageBase()->GetBufferedRegion();
  SegmentationUpdateIterator itVol(seg, region,
                                   static_cast<LabelType>(labelId),
                                   gs->GetDrawOverFilter());
  itk::ImageRegionConstIterator<FloatImageType> itSrc(fimg, region);

  long count = 0;
  for(; !itVol.IsAtEnd(); ++itVol, ++itSrc)
    {
    const double v = itSrc.Get();
    if(v >= lower && v <= upper)
      { itVol.PaintAsForeground(); ++count; }
    }
  itVol.Finalize("Assistant threshold segmentation");
  driver->RecordCurrentLabelUse();
  driver->InvokeEvent(SegmentationChangeEvent());

  ok = true;
  if(count == 0)
    return QString("No voxels matched intensity [%1, %2] — try a different range "
                   "(call get_scene_overview to see the range).")
             .arg(lower).arg(upper);
  return QString("Segmented '%1' as label %2: %3 voxels in intensity [%4, %5]%6. "
                 "The mask is now visible in the ITK-SNAP viewer.")
           .arg(name).arg(labelId).arg(count).arg(lower).arg(upper).arg(note);
}

QString AssistantPanel::toolMeasureVolume(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  if(!driver->IsMainImageLoaded())
    { ok = false; return "No image is loaded."; }

  const int label = args.contains("label") ? args["label"].toInt() : 1;
  SegmentationStatistics stats;
  stats.Compute(driver);
  const auto &m = stats.GetStats();
  auto it = m.find(static_cast<LabelType>(label));
  if(it == m.end() || it->second.count == 0)
    { ok = false; return QString("Label %1 has no voxels.").arg(label); }
  ok = true;
  return QString("Label %1: %2 mL (%3 voxels).")
    .arg(label)
    .arg(it->second.volume_mm3 / 1000.0, 0, 'f', 2)
    .arg((qulonglong) it->second.count);
}

QString AssistantPanel::toolFocusLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  if(!driver->IsMainImageLoaded())
    { ok = false; return "No image is loaded."; }
  const int labelId = args.contains("label") ? args["label"].toInt() : 1;

  LabelImageWrapper *seg = driver->GetSelectedSegmentationLayer();
  if(!seg)
    { ok = false; return "No segmentation available."; }

  typedef LabelImageWrapper::ImageType LabelImageType;
  LabelImageType *img = seg->GetModifiableImage();
  itk::ImageRegionConstIteratorWithIndex<LabelImageType> it(img, img->GetBufferedRegion());
  double sx = 0, sy = 0, sz = 0;
  long n = 0;
  for(; !it.IsAtEnd(); ++it)
    {
    if(static_cast<int>(it.Get()) == labelId)
      {
      const LabelImageType::IndexType idx = it.GetIndex();
      sx += idx[0]; sy += idx[1]; sz += idx[2]; ++n;
      }
    }
  if(n == 0)
    { ok = false; return QString("Label %1 has no voxels to focus on.").arg(labelId); }

  Vector3ui c;
  c[0] = static_cast<unsigned int>(sx / n);
  c[1] = static_cast<unsigned int>(sy / n);
  c[2] = static_cast<unsigned int>(sz / n);
  driver->SetCursorPosition(c, true);
  ok = true;
  return QString("Centered the view on label %1 at voxel (%2, %3, %4) (%5 voxels).")
           .arg(labelId).arg(c[0]).arg(c[1]).arg(c[2]).arg(n);
}

/* ------------------------------------------------------------------ */
/* state / info                                                        */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolCursorInfo(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  ok = true;
  Vector3ui c = d->GetCursorPosition();
  QString s = QString("Cursor at voxel (%1, %2, %3).").arg(c[0]).arg(c[1]).arg(c[2]);
  LabelImageWrapper *seg = d->GetSelectedSegmentationLayer();
  if(seg)
    {
    typedef LabelImageWrapper::ImageType LabelImageType;
    LabelImageType::IndexType idx;
    idx[0] = c[0]; idx[1] = c[1]; idx[2] = c[2];
    LabelType lab = seg->GetModifiableImage()->GetPixel(idx);
    QString nm = QString::fromUtf8(d->GetColorLabelTable()->GetColorLabel(lab).GetLabel());
    s += QString(" Label under cursor: %1 (%2).").arg(static_cast<int>(lab)).arg(nm);
    }
  return s;
}

QString AssistantPanel::toolMeasureAllLabels(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  SegmentationStatistics stats;
  stats.Compute(d);
  ColorLabelTable *lt = d->GetColorLabelTable();
  QStringList lines;
  for(auto const &kv : stats.GetStats())
    {
    if(kv.first == 0 || kv.second.count == 0) continue;
    QString nm = QString::fromUtf8(lt->GetColorLabel(kv.first).GetLabel());
    lines << QString("%1 (%2): %3 mL (%4 vox)").arg(static_cast<int>(kv.first)).arg(nm)
             .arg(kv.second.volume_mm3 / 1000.0, 0, 'f', 2)
             .arg(static_cast<qulonglong>(kv.second.count));
    }
  ok = true;
  return lines.isEmpty() ? QString("No labeled voxels yet.")
                         : QString("Volumes -> ") + lines.join("; ");
}

QString AssistantPanel::toolCountVoxels(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int lab = args["label"].toInt();
  const size_t n = d->GetNumberOfVoxelsWithLabel(static_cast<LabelType>(lab));
  ok = true;
  return QString("Label %1 has %2 voxels.").arg(lab).arg(static_cast<qulonglong>(n));
}

/* ------------------------------------------------------------------ */
/* image i/o                                                           */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolLoadOverlay(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "Load a main image first."; }
  IRISWarningList wl;
  d->OpenImage(args["path"].toString().toUtf8().constData(), OVERLAY_ROLE, wl);
  ok = true;
  return QString("Loaded overlay %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolLoadSegmentation(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "Load a main image first."; }
  IRISWarningList wl;
  d->OpenImage(args["path"].toString().toUtf8().constData(), LABEL_ROLE, wl);
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Loaded segmentation %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolUnloadOverlays(const QJsonObject &, bool &ok)
{
  m_Model->GetDriver()->UnloadAllOverlays();
  ok = true;
  return "Removed all overlay images.";
}

/* ------------------------------------------------------------------ */
/* segmentation editing                                                */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolClearSegmentation(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->ResetIRISSegmentationImage();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Cleared the entire segmentation.";
}

QString AssistantPanel::toolClearLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int lab = args["label"].toInt();
  const size_t n = d->ReplaceLabel(0, static_cast<LabelType>(lab));   // old lab -> clear
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Cleared label %1 (%2 voxels erased).").arg(lab).arg(static_cast<qulonglong>(n));
}

QString AssistantPanel::toolReplaceLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int from = args["from_label"].toInt();
  const int to   = args["to_label"].toInt();
  const size_t n = d->ReplaceLabel(static_cast<LabelType>(to), static_cast<LabelType>(from));
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Replaced label %1 with %2 (%3 voxels).").arg(from).arg(to).arg(static_cast<qulonglong>(n));
}

/* ------------------------------------------------------------------ */
/* labels                                                              */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolSetActiveLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  d->GetGlobalState()->SetDrawingColorLabel(static_cast<LabelType>(lab));
  ok = true;
  return QString("Active drawing label set to %1.").arg(lab);
}

QString AssistantPanel::toolRenameLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  ColorLabelTable *lt = d->GetColorLabelTable();
  ColorLabel cl = lt->GetColorLabel(lab);
  cl.SetLabel(args["name"].toString().toUtf8().constData());
  lt->SetColorLabel(lab, cl);
  ok = true;
  return QString("Renamed label %1 to '%2'.").arg(lab).arg(args["name"].toString());
}

QString AssistantPanel::toolSetLabelColor(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  ColorLabelTable *lt = d->GetColorLabelTable();
  ColorLabel cl = lt->GetColorLabel(lab);
  cl.SetRGB(static_cast<unsigned char>(args["r"].toInt()),
            static_cast<unsigned char>(args["g"].toInt()),
            static_cast<unsigned char>(args["b"].toInt()));
  lt->SetColorLabel(lab, cl);
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Set label %1 color to (%2, %3, %4).")
           .arg(lab).arg(args["r"].toInt()).arg(args["g"].toInt()).arg(args["b"].toInt());
}

/* ------------------------------------------------------------------ */
/* navigation / view                                                   */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolMoveCursor(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  // clamp to image bounds so an out-of-range coordinate can't throw
  Vector3ui sz = d->GetCurrentImageData()->GetMain()->GetSize();
  int xi = args["x"].toInt(), yi = args["y"].toInt(), zi = args["z"].toInt();
  Vector3ui c;
  c[0] = (unsigned) std::max(0, std::min(xi, (int)sz[0] - 1));
  c[1] = (unsigned) std::max(0, std::min(yi, (int)sz[1] - 1));
  c[2] = (unsigned) std::max(0, std::min(zi, (int)sz[2] - 1));
  d->SetCursorPosition(c, true);
  ok = true;
  return QString("Crosshair moved to (%1, %2, %3).").arg(c[0]).arg(c[1]).arg(c[2]);
}

QString AssistantPanel::toolSetLayout(const QJsonObject &args, bool &ok)
{
  const QString L = args["layout"].toString().toLower();
  DisplayLayoutModel::ViewPanelLayout vp = DisplayLayoutModel::VIEW_ALL;
  if(L == "axial")         vp = DisplayLayoutModel::VIEW_AXIAL;
  else if(L == "coronal")  vp = DisplayLayoutModel::VIEW_CORONAL;
  else if(L == "sagittal") vp = DisplayLayoutModel::VIEW_SAGITTAL;
  else if(L == "3d")       vp = DisplayLayoutModel::VIEW_3D;
  m_Model->GetDisplayLayoutModel()->SetViewPanelLayout(vp);
  ok = true;
  return QString("View layout set to %1.").arg(L);
}

QString AssistantPanel::toolUpdate3DMesh(const QJsonObject &, bool &ok)
{
  m_Model->GetModel3D()->UpdateSegmentationMesh(NULL);
  ok = true;
  return "Rebuilding the 3D segmentation mesh (it will appear in the 3D view).";
}

/* ------------------------------------------------------------------ */
/* workspace / edit                                                    */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolSaveWorkspace(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->SaveProject(args["path"].toString().toStdString());
  ok = true;
  return QString("Saved workspace to %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolLoadWorkspace(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  IRISWarningList wl;
  d->OpenProject(args["path"].toString().toStdString(), wl);
  ok = true;
  return QString("Opened workspace %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolSaveStatistics(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->ExportSegmentationStatistics(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved per-label statistics to %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolUndo(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsUndoPossible()) { ok = false; return "Nothing to undo."; }
  d->Undo();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Undid the last segmentation edit.";
}

QString AssistantPanel::toolRedo(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsRedoPossible()) { ok = false; return "Nothing to redo."; }
  d->Redo();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Redid the last undone edit.";
}

/* ------------------------------------------------------------------ */
/* advanced segmentation edits                                         */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolSmoothLabels(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int lab = args.contains("label") ? args["label"].toInt() : 1;
  const double s = args.contains("sigma_mm") ? args["sigma_mm"].toDouble() : 1.0;
  std::unordered_set<LabelType> labs;
  labs.insert(static_cast<LabelType>(lab));
  std::vector<double> sigma = { s, s, s };
  m_Model->GetSmoothLabelsModel()->Smooth(labs, sigma, SmoothLabelsModel::mm, false);
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Smoothed label %1 with sigma %2 mm.").arg(lab).arg(s);
}

QString AssistantPanel::toolInterpolateLabels(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  InterpolateLabelModel *im = m_Model->GetInterpolateLabelModel();
  if(args.contains("label"))
    { im->SetInterpolateAll(false); im->SetInterpolateLabel(static_cast<LabelType>(args["label"].toInt())); }
  else
    { im->SetInterpolateAll(true); }
  im->Interpolate();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return args.contains("label")
      ? QString("Interpolated label %1 across slices.").arg(args["label"].toInt())
      : QString("Interpolated all labels across slices.");
}

/* ------------------------------------------------------------------ */
/* display                                                             */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolExportSlice(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const QString dir = args["direction"].toString().toLower();
  AnatomicalDirection ad = ANATOMY_AXIAL;
  if(dir == "sagittal")     ad = ANATOMY_SAGITTAL;
  else if(dir == "coronal") ad = ANATOMY_CORONAL;
  d->ExportSlice(ad, args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Exported the %1 slice to %2.").arg(dir, args["path"].toString());
}

/* ------------------------------------------------------------------ */
/* more workspace / files                                              */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolUnloadMainImage(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->UnloadMainImage();
  ok = true;
  return "Closed the main image.";
}

QString AssistantPanel::toolSaveAnnotations(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->SaveAnnotations(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved annotations to %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolLoadAnnotations(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->LoadAnnotations(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Loaded annotations from %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolSaveLabels(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->SaveLabelDescriptions(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved label descriptions to %1.").arg(args["path"].toString());
}

QString AssistantPanel::toolLoadLabels(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->LoadLabelDescriptions(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Loaded label descriptions from %1.").arg(args["path"].toString());
}

/* ------------------------------------------------------------------ */
/* wave 1: contrast + label display + label lifecycle                  */
/* ------------------------------------------------------------------ */
QString AssistantPanel::toolAutoWindowLevel(const QJsonObject &, bool &ok)
{
  if(!m_Model->GetDriver()->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  m_Model->GetIntensityCurveModel()->OnAutoFitWindow();
  ok = true;
  return "Auto-adjusted the image window/level (contrast).";
}

QString AssistantPanel::toolSetWindowLevel(const QJsonObject &args, bool &ok)
{
  if(!m_Model->GetDriver()->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  IntensityCurveModel *icm = m_Model->GetIntensityCurveModel();
  QStringList did;
  if(args.contains("window"))
    { icm->GetIntensityRangeModel(IntensityCurveModel::WINDOW)->SetValue(args["window"].toDouble()); did << "window"; }
  if(args.contains("level"))
    { icm->GetIntensityRangeModel(IntensityCurveModel::LEVEL)->SetValue(args["level"].toDouble()); did << "level"; }
  if(did.isEmpty()) { ok = false; return "Provide a window and/or level value."; }
  ok = true;
  return QString("Set %1.").arg(did.join(" and "));
}

QString AssistantPanel::toolSetSegmentationOpacity(const QJsonObject &args, bool &ok)
{
  double pct = args["opacity"].toDouble();
  pct = std::max(0.0, std::min(100.0, pct));
  m_Model->GetDriver()->GetGlobalState()->SetSegmentationAlpha(pct / 100.0);
  m_Model->GetDriver()->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Set segmentation overlay opacity to %1%.").arg((int)pct);
}

QString AssistantPanel::toolSetLabelOpacity(const QJsonObject &args, bool &ok)
{
  const int lab = args["label"].toInt();
  int a = std::max(0, std::min(255, args["opacity"].toInt()));
  ColorLabelTable *lt = m_Model->GetDriver()->GetColorLabelTable();
  ColorLabel cl = lt->GetColorLabel(lab);
  cl.SetAlpha((unsigned char)a);
  lt->SetColorLabel(lab, cl);
  ok = true;
  return QString("Set label %1 opacity to %2/255.").arg(lab).arg(a);
}

QString AssistantPanel::toolSetLabelVisibility(const QJsonObject &args, bool &ok)
{
  const int lab = args["label"].toInt();
  const bool vis = args["visible"].toBool();
  ColorLabelTable *lt = m_Model->GetDriver()->GetColorLabelTable();
  ColorLabel cl = lt->GetColorLabel(lab);
  cl.SetVisible(vis);
  lt->SetColorLabel(lab, cl);
  ok = true;
  return QString("%1 label %2.").arg(vis ? "Showing" : "Hiding").arg(lab);
}

QString AssistantPanel::toolCreateLabel(const QJsonObject &args, bool &ok)
{
  ColorLabelTable *lt = m_Model->GetDriver()->GetColorLabelTable();
  LabelType id = lt->GetInsertionSpot(1);          // next free id >= 1
  ColorLabel cl = lt->GetColorLabel(id);
  cl.SetLabel(args["name"].toString().toUtf8().constData());
  cl.SetRGB((unsigned char)(args.contains("r") ? args["r"].toInt() : 255),
            (unsigned char)(args.contains("g") ? args["g"].toInt() : 200),
            (unsigned char)(args.contains("b") ? args["b"].toInt() : 0));
  cl.SetAlpha(255); cl.SetVisible(true);
  lt->SetColorLabel(id, cl);
  lt->SetColorLabelValid(id, true);
  ok = true;
  return QString("Created label %1 ('%2').").arg(id).arg(args["name"].toString());
}

QString AssistantPanel::toolDeleteLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  size_t n = 0;
  if(d->IsMainImageLoaded())
    n = d->ReplaceLabel(0, (LabelType)lab);        // erase its voxels
  d->GetColorLabelTable()->SetColorLabelValid((LabelType)lab, false);  // remove from table
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Deleted label %1 (erased %2 voxels and removed it).").arg(lab).arg((qulonglong)n);
}

/* ------------------------------------------------------------------ */
/* chat rendering                                                      */
/* ------------------------------------------------------------------ */
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
