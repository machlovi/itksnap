/*=========================================================================

  Program:   ITK-SNAP Assistant Panel
  Purpose:   Dockable LLM chat panel that drives ITK-SNAP tools via the
             local itksnap-agent sidecar (ws://127.0.0.1:8077/wsbridge).

  Tool calls arrive over the WebSocket and are executed on the GUI thread
  via SNAPRemoteControl.

=========================================================================*/
#ifndef ASSISTANTPANEL_H
#define ASSISTANTPANEL_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include "SNAPRemoteControl.h"

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

  // Receives the application model
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
  // protocol & auto-launch
  void ensureAgentServerRunning();
  void connectToServer();
  void sendJson(const QJsonObject &obj);
  void sendHello();
  void dispatchToolCall(const QString &id, const QString &name,
                        const QJsonObject &args);

  // chat rendering
  void appendChat(const QString &who, const QString &text);
  void appendToolLine(const QString &name, const QJsonObject &args);

  GlobalUIModel     *m_Model = nullptr;
  SNAPRemoteControl  m_RemoteControl;

  QWebSocket    *m_Socket     = nullptr;
  QTextBrowser  *m_Transcript = nullptr;
  QLineEdit     *m_Input      = nullptr;
  QLineEdit     *m_LlmEndpoint = nullptr;
  QLineEdit     *m_LlmModel    = nullptr;
  QPushButton   *m_LlmApply    = nullptr;
  QPushButton   *m_Send       = nullptr;
  QPushButton   *m_Reconnect  = nullptr;
  QTimer        *m_RetryTimer = nullptr;

  QString m_ServerUrl = "ws://127.0.0.1:8077/wsbridge/itksnap";
  QString m_StreamBuffer;
  bool    m_AgentSpawnAttempted = false;
};

#endif // ASSISTANTPANEL_H
