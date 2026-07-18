/*=========================================================================

  Program:   ITK-SNAP Assistant Panel
  Purpose:   Dockable LLM chat panel that drives ITK-SNAP tools via the
             local itksnap-agent sidecar (ws://127.0.0.1:8077/wsbridge).

  The chat UI is native Qt Widgets; the only extra Qt module is WebSockets.
  Tool calls arrive over the socket and are executed on the GUI thread
  against IRISApplication (via GlobalUIModel::GetDriver()).

=========================================================================*/
#ifndef ASSISTANTPANEL_H
#define ASSISTANTPANEL_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>

class GlobalUIModel;
class QTextBrowser;
class QLineEdit;
class QPushButton;
class QWebSocket;
class QTimer;

class AssistantPanel : public QWidget
{
  Q_OBJECT

public:
  explicit AssistantPanel(QWidget *parent = nullptr);
  ~AssistantPanel() override;

  // Receives the application model, like every other ITK-SNAP panel
  void SetModel(GlobalUIModel *model);

  void SetServerUrl(const QString &url) { m_ServerUrl = url; }

private slots:
  void onConnected();
  void onDisconnected();
  void onTextMessageReceived(const QString &message);
  void onSendClicked();
  void onReconnectClicked();
  void onApplyLlmClicked();

private:
  // protocol
  void connectToServer();
  void sendJson(const QJsonObject &obj);
  void sendHello();
  QJsonArray toolSchemas() const;
  void dispatchToolCall(const QString &id, const QString &name,
                        const QJsonObject &args);

  // tool implementations (thin adapters over IRISApplication / models)
  // -- state / info --
  QString toolSceneOverview(const QJsonObject &args, bool &ok);
  QString toolCursorInfo(const QJsonObject &args, bool &ok);
  QString toolMeasureVolume(const QJsonObject &args, bool &ok);
  QString toolMeasureAllLabels(const QJsonObject &args, bool &ok);
  QString toolCountVoxels(const QJsonObject &args, bool &ok);
  // -- image i/o --
  QString toolLoadImage(const QJsonObject &args, bool &ok);
  QString toolLoadOverlay(const QJsonObject &args, bool &ok);
  QString toolLoadSegmentation(const QJsonObject &args, bool &ok);
  QString toolUnloadOverlays(const QJsonObject &args, bool &ok);
  // -- segmentation --
  QString toolThresholdSegment(const QJsonObject &args, bool &ok);
  QString toolClearSegmentation(const QJsonObject &args, bool &ok);
  QString toolClearLabel(const QJsonObject &args, bool &ok);
  QString toolReplaceLabel(const QJsonObject &args, bool &ok);
  // -- labels --
  QString toolSetActiveLabel(const QJsonObject &args, bool &ok);
  QString toolRenameLabel(const QJsonObject &args, bool &ok);
  QString toolSetLabelColor(const QJsonObject &args, bool &ok);
  // -- navigation / view --
  QString toolMoveCursor(const QJsonObject &args, bool &ok);
  QString toolFocusLabel(const QJsonObject &args, bool &ok);
  QString toolSetLayout(const QJsonObject &args, bool &ok);
  QString toolUpdate3DMesh(const QJsonObject &args, bool &ok);
  // -- advanced segmentation edits --
  QString toolSmoothLabels(const QJsonObject &args, bool &ok);
  QString toolInterpolateLabels(const QJsonObject &args, bool &ok);
  // -- display --
  QString toolExportSlice(const QJsonObject &args, bool &ok);
  // -- workspace / edit --
  QString toolSaveWorkspace(const QJsonObject &args, bool &ok);
  QString toolLoadWorkspace(const QJsonObject &args, bool &ok);
  QString toolSaveStatistics(const QJsonObject &args, bool &ok);
  QString toolUnloadMainImage(const QJsonObject &args, bool &ok);
  QString toolSaveAnnotations(const QJsonObject &args, bool &ok);
  QString toolLoadAnnotations(const QJsonObject &args, bool &ok);
  QString toolSaveLabels(const QJsonObject &args, bool &ok);
  QString toolLoadLabels(const QJsonObject &args, bool &ok);
  QString toolUndo(const QJsonObject &args, bool &ok);
  QString toolRedo(const QJsonObject &args, bool &ok);

  // chat rendering
  void appendChat(const QString &who, const QString &text);
  void appendToolLine(const QString &name, const QJsonObject &args);

  GlobalUIModel *m_Model      = nullptr;
  QWebSocket    *m_Socket     = nullptr;
  QTextBrowser  *m_Transcript = nullptr;
  QLineEdit     *m_Input      = nullptr;
  QLineEdit     *m_LlmEndpoint = nullptr;   // e.g. http://localhost:11440 or IP:port
  QLineEdit     *m_LlmModel    = nullptr;   // e.g. localmodel
  QPushButton   *m_LlmApply    = nullptr;
  QPushButton   *m_Send       = nullptr;
  QPushButton   *m_Reconnect  = nullptr;
  QTimer        *m_RetryTimer = nullptr;

  QString m_ServerUrl = "ws://127.0.0.1:8077/wsbridge/itksnap";
  QString m_StreamBuffer;
};

#endif // ASSISTANTPANEL_H
