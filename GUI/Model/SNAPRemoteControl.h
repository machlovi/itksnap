/*=========================================================================

  Program:   ITK-SNAP Remote Control RPC Engine
  Purpose:   Generic JSON-RPC command execution engine for ITK-SNAP.
             Translates structured remote control commands into core ITK-SNAP
             actions against IRISApplication / GlobalUIModel.

  This class is completely independent of Qt GUI widgets or network transports.
  It enables clean external automation, scripting, and sidecar agent control.

=========================================================================*/
#ifndef SNAPREMOTECONTROL_H
#define SNAPREMOTECONTROL_H

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class GlobalUIModel;

class SNAPRemoteControl
{
public:
  explicit SNAPRemoteControl(GlobalUIModel *model = nullptr);
  virtual ~SNAPRemoteControl();

  void SetModel(GlobalUIModel *model) { m_Model = model; }
  GlobalUIModel* GetModel() const { return m_Model; }

  // Returns JSON array of all available RPC command schemas
  QJsonArray GetSupportedCommandSchemas() const;

  // Executes a named tool command with arguments.
  // Returns result JSON object: {"ok": bool, "text": QString, ...extra}
  QJsonObject ExecuteCommand(const QString &commandName, const QJsonObject &args);

private:
  // Tool implementations
  QString toolSceneOverview(const QJsonObject &args, bool &ok);
  QString toolCursorInfo(const QJsonObject &args, bool &ok);
  QString toolMeasureVolume(const QJsonObject &args, bool &ok);
  QString toolMeasureAllLabels(const QJsonObject &args, bool &ok);
  QString toolCountVoxels(const QJsonObject &args, bool &ok);

  QString toolLoadImage(const QJsonObject &args, bool &ok);
  QString toolLoadOverlay(const QJsonObject &args, bool &ok);
  QString toolLoadSegmentation(const QJsonObject &args, bool &ok);
  QString toolUnloadOverlays(const QJsonObject &args, bool &ok);

  QString toolThresholdSegment(const QJsonObject &args, bool &ok);
  QString toolClearSegmentation(const QJsonObject &args, bool &ok);
  QString toolClearLabel(const QJsonObject &args, bool &ok);
  QString toolReplaceLabel(const QJsonObject &args, bool &ok);

  QString toolSetActiveLabel(const QJsonObject &args, bool &ok);
  QString toolRenameLabel(const QJsonObject &args, bool &ok);
  QString toolSetLabelColor(const QJsonObject &args, bool &ok);

  QString toolMoveCursor(const QJsonObject &args, bool &ok);
  QString toolFocusLabel(const QJsonObject &args, bool &ok);
  QString toolSetLayout(const QJsonObject &args, bool &ok);
  QString toolUpdate3DMesh(const QJsonObject &args, bool &ok);

  QString toolSmoothLabels(const QJsonObject &args, bool &ok);
  QString toolInterpolateLabels(const QJsonObject &args, bool &ok);

  QString toolExportSlice(const QJsonObject &args, bool &ok);

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

  QString toolAutoWindowLevel(const QJsonObject &args, bool &ok);
  QString toolSetWindowLevel(const QJsonObject &args, bool &ok);
  QString toolSetSegmentationOpacity(const QJsonObject &args, bool &ok);
  QString toolSetLabelOpacity(const QJsonObject &args, bool &ok);
  QString toolSetLabelVisibility(const QJsonObject &args, bool &ok);
  QString toolCreateLabel(const QJsonObject &args, bool &ok);
  QString toolDeleteLabel(const QJsonObject &args, bool &ok);

  QString toolActiveContourSegment(const QJsonObject &args, bool &ok);

  GlobalUIModel *m_Model = nullptr;
};

#endif // SNAPREMOTECONTROL_H
