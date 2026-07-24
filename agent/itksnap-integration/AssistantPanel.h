/*=========================================================================
  ITK-SNAP Assistant Panel  (integration scaffold)

  A dockable chat panel that connects to the local itksnap-agent server over
  a WebSocket (the /wsbridge protocol) and executes the agent's tool calls
  against ITK-SNAP's own Logic layer (IRISApplication) on the GUI thread.

  Design notes:
   - No QtWebEngine / no QWebChannel. The chat UI is native Qt Widgets
     (QTextBrowser + QLineEdit); the only new Qt module is Qt6::WebSockets.
   - The LLM + agent loop live in the local Python sidecar (itksnap-agent),
     so this class stays thin: send user text, render events, and run the
     tool calls the agent issues via GlobalUIModel::GetDriver().
   - WebSocket slots fire on the GUI thread, so calling IRISApplication here
     is thread-safe (no marshalling needed).

  This file is a SCAFFOLD: method signatures follow the real ITK-SNAP API
  (verified against master), but exact overloads/includes may need small
  adjustments when compiled in-tree. Search for TODO.
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

class AssistantPanel : public QWidget
{
  Q_OBJECT

public:
  explicit AssistantPanel(QWidget *parent = nullptr);
  ~AssistantPanel() override;

  // ITK-SNAP panels receive the application model this way (see other panels).
  void SetModel(GlobalUIModel *model);

  // Where the local itksnap-agent server is listening.
  void SetServerUrl(const QString &url) { m_ServerUrl = url; }

private slots:
  void onConnected();
  void onDisconnected();
  void onTextMessageReceived(const QString &message);
  void onSendClicked();

private:
  // -- protocol -------------------------------------------------------
  void connectToServer();
  void sendJson(const QJsonObject &obj);
  void sendHello();                         // advertise ITK-SNAP's tools
  QJsonArray toolSchemas() const;           // the tool definitions we host
  void dispatchToolCall(const QString &id,
                        const QString &name,
                        const QJsonObject &args);

  // -- tool implementations (thin adapters over IRISApplication) ------
  // Each returns a human-readable result string; ok=false via out-param.
  QString toolLoadImage(const QJsonObject &args, bool &ok);
  QString toolListLabels(const QJsonObject &args, bool &ok);
  QString toolMeasureVolume(const QJsonObject &args, bool &ok);
  QString toolMoveCursor(const QJsonObject &args, bool &ok);
  QString toolSegmentStructure(const QJsonObject &args, bool &ok); // delegates to DL model

  // -- chat rendering -------------------------------------------------
  void appendChat(const QString &who, const QString &text);
  void appendToolLine(const QString &name, const QJsonObject &args);

  GlobalUIModel *m_Model      = nullptr;
  QWebSocket    *m_Socket     = nullptr;
  QTextBrowser  *m_Transcript = nullptr;
  QLineEdit     *m_Input      = nullptr;
  QPushButton   *m_Send       = nullptr;

  QString m_ServerUrl = "ws://127.0.0.1:8077/wsbridge/itksnap";
  QString m_StreamBuffer;   // accumulates streamed assistant tokens
};

#endif // ASSISTANTPANEL_H
