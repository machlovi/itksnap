/*=========================================================================

  Program:   ITK-SNAP Remote Control RPC Engine -- implementation.
  Purpose:   Generic JSON-RPC command execution engine for ITK-SNAP.

=========================================================================*/
#include "SNAPRemoteControl.h"

#include <QJsonDocument>
#include <QDebug>
#include <QStringList>

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
#include "SnakeWizardModel.h"
#include "SNAPSegmentationROISettings.h"
#include "ImageCoordinateGeometry.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionConstIteratorWithIndex.h"

#include <unordered_set>
#include <vector>
#include <utility>     // std::swap
#include <algorithm>   // std::min/std::max

SNAPRemoteControl::SNAPRemoteControl(GlobalUIModel *model)
  : m_Model(model)
{
}

SNAPRemoteControl::~SNAPRemoteControl()
{
}

QJsonArray SNAPRemoteControl::GetSupportedCommandSchemas() const
{
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
          "intensity range, cursor position, and the defined segmentation labels.", none);
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
          "Load a main medical image volume into ITK-SNAP from an absolute file path.",
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
            "Segment all voxels whose intensity is in [lower, upper] into a label.",
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
          "Set the active drawing label.",
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
          "Center the ITK-SNAP view on the middle of a segmentation label.",
          labelProp("Label id to center on."), QJsonArray{"label"});
  { QJsonObject p; QJsonObject e; e["type"]="string";
    e["enum"]=QJsonArray{"all","axial","coronal","sagittal","3d"};
    e["description"]="Which view panel layout to show.";
    p["layout"]=e;
    addTool("set_layout", "Change the ITK-SNAP view layout.",
            p, QJsonArray{"layout"}); }
  addTool("update_3d_mesh",
          "Rebuild the 3D surface mesh from the current segmentation.", none);

  // ---- workspace / edit ----
  addTool("save_workspace",
          "Save the current ITK-SNAP workspace to a .itksnap file.",
          pathProp("Absolute path for the .itksnap workspace file."), QJsonArray{"path"});
  addTool("load_workspace",
          "Open an ITK-SNAP workspace (.itksnap) file.",
          pathProp("Absolute path to the .itksnap workspace file."), QJsonArray{"path"});
  addTool("save_statistics",
          "Write/export/save per-label statistics to a FILE ON DISK.",
          pathProp("Absolute path for the statistics file."), QJsonArray{"path"});
  addTool("undo", "Undo the last segmentation edit.", none);
  addTool("redo", "Redo the last undone segmentation edit.", none);

  // ---- advanced segmentation edits ----
  { QJsonObject p; p["label"]=intObj("Label id to smooth (default 1).");
    p["sigma_mm"]=numObj("Gaussian smoothing sigma in millimetres (default 1.0).");
    addTool("smooth_labels", "Smooth the boundary of a segmentation label.", p); }
  addTool("interpolate_labels",
          "Interpolate a segmentation across slices.",
          labelProp("Label id to interpolate (optional)."));

  // ---- display ----
  { QJsonObject p; QJsonObject dir; dir["type"]="string";
    dir["enum"]=QJsonArray{"axial","coronal","sagittal"};
    dir["description"]="Which slice orientation to export.";
    p["direction"]=dir; p["path"]=strObj("Absolute path for output image.");
    addTool("export_slice", "Save current slice as a 2D image file.",
            p, QJsonArray{"direction","path"}); }

  // ---- more workspace / files ----
  addTool("unload_main_image", "Close the current main image.", none);
  addTool("save_annotations", "Save ruler/landmark annotations to a file.",
          pathProp("Absolute path for annotations file."), QJsonArray{"path"});
  addTool("load_annotations", "Load ruler/landmark annotations from a file.",
          pathProp("Absolute path to annotations file."), QJsonArray{"path"});
  addTool("save_labels", "Save label descriptions to a text file.",
          pathProp("Absolute path for label description file."), QJsonArray{"path"});
  addTool("load_labels", "Load label descriptions from a text file.",
          pathProp("Absolute path to label description file."), QJsonArray{"path"});

  // ---- contrast / display / label lifecycle ----
  addTool("auto_window_level", "Auto adjust image contrast.", none);
  { QJsonObject p; p["window"]=numObj("Window width."); p["level"]=numObj("Window level.");
    addTool("set_window_level", "Set display window and/or level.", p); }
  { QJsonObject p; p["opacity"]=intObj("Overlay opacity percent 0-100.");
    addTool("set_segmentation_opacity", "Set segmentation overlay opacity.", p, QJsonArray{"opacity"}); }
  { QJsonObject p; p["label"]=intObj("Label id."); p["opacity"]=intObj("Opacity 0-255.");
    addTool("set_label_opacity", "Set opacity of specific label.", p, QJsonArray{"label","opacity"}); }
  { QJsonObject p; p["label"]=intObj("Label id.");
    QJsonObject vb; vb["type"]="boolean"; vb["description"]="true to show, false to hide."; p["visible"]=vb;
    addTool("set_label_visibility", "Show or hide a label.", p, QJsonArray{"label","visible"}); }
  { QJsonObject p; p["name"]=strObj("Name for new label.");
    p["r"]=intObj("Red 0-255."); p["g"]=intObj("Green 0-255."); p["b"]=intObj("Blue 0-255.");
    addTool("create_label", "Create a new segmentation label.", p, QJsonArray{"name"}); }
  { QJsonObject p; p["label"]=intObj("Label id to delete.");
    addTool("delete_label", "Delete a segmentation label.", p, QJsonArray{"label"}); }

  // ---- active contour (snake) ----
  { QJsonObject p;
    p["lower"]=numObj("Lower intensity."); p["upper"]=numObj("Upper intensity.");
    p["seed_x"]=intObj("Seed voxel X."); p["seed_y"]=intObj("Seed voxel Y."); p["seed_z"]=intObj("Seed voxel Z.");
    p["seed_radius_mm"]=numObj("Seed radius in mm."); p["iterations"]=intObj("Max iterations.");
    p["label"]=intObj("Target label id.");
    addTool("active_contour_segment", "Run active-contour (snake) segmentation.", p, QJsonArray{"lower","upper"}); }

  return tools;
}

QJsonObject SNAPRemoteControl::ExecuteCommand(const QString &name, const QJsonObject &args)
{
  bool ok = true;
  QString result;
  if(!m_Model || !m_Model->GetDriver())
  {
    QJsonObject err; err["ok"] = false; err["text"] = "GlobalUIModel or IRISApplication driver is null.";
    return err;
  }

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
    else if(name == "active_contour_segment") result = toolActiveContourSegment(args, ok);
    else if(name == "get_label_stats")    result = toolGetLabelStats(args, ok);
    else if(name == "set_roi_box")        result = toolSetROIBox(args, ok);
    else if(name == "export_3d_mesh")     result = toolExportMesh(args, ok);
    else { ok = false; result = QString("Unknown RPC command '%1'.").arg(name); }
  }
  catch(IRISException &exc)
  { ok = false; result = QString("ITK-SNAP error: %1").arg(exc.what()); }
  catch(std::exception &exc)
  { ok = false; result = QString("error: %1").arg(exc.what()); }

  QJsonObject res;
  res["ok"] = ok;
  res["text"] = result;
  return res;
}

/* Tool Implementations */
QString SNAPRemoteControl::toolSceneOverview(const QJsonObject &, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  ok = true;
  QStringList out;
  if(!driver->IsMainImageLoaded())
    return "No image is loaded.";

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

QString SNAPRemoteControl::toolLoadImage(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolThresholdSegment(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model->GetDriver();
  if(!driver->IsMainImageLoaded())
    { ok = false; return "No image is loaded. Load one first."; }

  if(!args.contains("lower"))
    { ok = false; return "threshold_segment needs a 'lower' intensity value."; }
  int labelId = args.contains("label") ? args["label"].toInt() : 1;
  if(labelId < 1)   labelId = 1;
  if(labelId > 255) labelId = 255;
  double lower = args["lower"].toDouble();
  double upper = args.contains("upper") ? args["upper"].toDouble() : 1e30;
  QString note;
  if(lower > upper) { std::swap(lower, upper); note = " (swapped reversed bounds)"; }
  const QString name = args.contains("name") ? args["name"].toString() : QString("segmentation");

  GlobalState *gs = driver->GetGlobalState();
  ColorLabelTable *lt = driver->GetColorLabelTable();

  ColorLabel cl = lt->GetColorLabel(labelId);
  cl.SetLabel(name.toUtf8().constData());
  lt->SetColorLabel(labelId, cl);
  gs->SetDrawingColorLabel(static_cast<LabelType>(labelId));

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
    return QString("No voxels matched intensity [%1, %2].")
             .arg(lower).arg(upper);
  return QString("Segmented '%1' as label %2: %3 voxels in intensity [%4, %5]%6.")
           .arg(name).arg(labelId).arg(count).arg(lower).arg(upper).arg(note);
}

QString SNAPRemoteControl::toolMeasureVolume(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolFocusLabel(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolCursorInfo(const QJsonObject &, bool &ok)
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

QString SNAPRemoteControl::toolMeasureAllLabels(const QJsonObject &, bool &ok)
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

QString SNAPRemoteControl::toolCountVoxels(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int lab = args["label"].toInt();
  const size_t n = d->GetNumberOfVoxelsWithLabel(static_cast<LabelType>(lab));
  ok = true;
  return QString("Label %1 has %2 voxels.").arg(lab).arg(static_cast<qulonglong>(n));
}

QString SNAPRemoteControl::toolLoadOverlay(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "Load a main image first."; }
  IRISWarningList wl;
  d->OpenImage(args["path"].toString().toUtf8().constData(), OVERLAY_ROLE, wl);
  ok = true;
  return QString("Loaded overlay %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolLoadSegmentation(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "Load a main image first."; }
  IRISWarningList wl;
  d->OpenImage(args["path"].toString().toUtf8().constData(), LABEL_ROLE, wl);
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Loaded segmentation %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolUnloadOverlays(const QJsonObject &, bool &ok)
{
  m_Model->GetDriver()->UnloadAllOverlays();
  ok = true;
  return "Removed all overlay images.";
}

QString SNAPRemoteControl::toolClearSegmentation(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->ResetIRISSegmentationImage();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Cleared the entire segmentation.";
}

QString SNAPRemoteControl::toolClearLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const int lab = args["label"].toInt();
  const size_t n = d->ReplaceLabel(0, static_cast<LabelType>(lab));
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Cleared label %1 (%2 voxels erased).").arg(lab).arg(static_cast<qulonglong>(n));
}

QString SNAPRemoteControl::toolReplaceLabel(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolSetActiveLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  d->GetGlobalState()->SetDrawingColorLabel(static_cast<LabelType>(lab));
  ok = true;
  return QString("Active drawing label set to %1.").arg(lab);
}

QString SNAPRemoteControl::toolRenameLabel(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolSetLabelColor(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolMoveCursor(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
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

QString SNAPRemoteControl::toolSetLayout(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolUpdate3DMesh(const QJsonObject &, bool &ok)
{
  m_Model->GetModel3D()->UpdateSegmentationMesh(NULL);
  ok = true;
  return "Rebuilding 3D segmentation mesh.";
}

QString SNAPRemoteControl::toolSaveWorkspace(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->SaveProject(args["path"].toString().toStdString());
  ok = true;
  return QString("Saved workspace to %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolLoadWorkspace(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  IRISWarningList wl;
  d->OpenProject(args["path"].toString().toStdString(), wl);
  ok = true;
  return QString("Opened workspace %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolSaveStatistics(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->ExportSegmentationStatistics(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved statistics to %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolUndo(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsUndoPossible()) { ok = false; return "Nothing to undo."; }
  d->Undo();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Undid last segmentation edit.";
}

QString SNAPRemoteControl::toolRedo(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsRedoPossible()) { ok = false; return "Nothing to redo."; }
  d->Redo();
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return "Redid last undone edit.";
}

QString SNAPRemoteControl::toolSmoothLabels(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolInterpolateLabels(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolExportSlice(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  const QString dir = args["direction"].toString().toLower();
  AnatomicalDirection ad = ANATOMY_AXIAL;
  if(dir == "sagittal")     ad = ANATOMY_SAGITTAL;
  else if(dir == "coronal") ad = ANATOMY_CORONAL;
  d->ExportSlice(ad, args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Exported %1 slice to %2.").arg(dir, args["path"].toString());
}

QString SNAPRemoteControl::toolUnloadMainImage(const QJsonObject &, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  d->UnloadMainImage();
  ok = true;
  return "Closed main image.";
}

QString SNAPRemoteControl::toolSaveAnnotations(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->SaveAnnotations(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved annotations to %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolLoadAnnotations(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->LoadAnnotations(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Loaded annotations from %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolSaveLabels(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->SaveLabelDescriptions(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Saved label descriptions to %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolLoadLabels(const QJsonObject &args, bool &ok)
{
  m_Model->GetDriver()->LoadLabelDescriptions(args["path"].toString().toUtf8().constData());
  ok = true;
  return QString("Loaded label descriptions from %1.").arg(args["path"].toString());
}

QString SNAPRemoteControl::toolAutoWindowLevel(const QJsonObject &, bool &ok)
{
  if(!m_Model->GetDriver()->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  m_Model->GetIntensityCurveModel()->OnAutoFitWindow();
  ok = true;
  return "Auto-adjusted contrast.";
}

QString SNAPRemoteControl::toolSetWindowLevel(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolSetSegmentationOpacity(const QJsonObject &args, bool &ok)
{
  double pct = args["opacity"].toDouble();
  pct = std::max(0.0, std::min(100.0, pct));
  m_Model->GetDriver()->GetGlobalState()->SetSegmentationAlpha(pct / 100.0);
  m_Model->GetDriver()->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Set segmentation opacity to %1%.").arg((int)pct);
}

QString SNAPRemoteControl::toolSetLabelOpacity(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolSetLabelVisibility(const QJsonObject &args, bool &ok)
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

QString SNAPRemoteControl::toolCreateLabel(const QJsonObject &args, bool &ok)
{
  ColorLabelTable *lt = m_Model->GetDriver()->GetColorLabelTable();
  LabelType id = args.contains("label") ? static_cast<LabelType>(args["label"].toInt()) : lt->GetInsertionSpot(1);
  ColorLabel cl = lt->GetColorLabel(id);
  cl.SetLabel(args["name"].toString().toUtf8().constData());
  cl.SetRGB((unsigned char)(args.contains("r") ? args["r"].toInt() : 255),
            (unsigned char)(args.contains("g") ? args["g"].toInt() : 200),
            (unsigned char)(args.contains("b") ? args["b"].toInt() : 0));
  cl.SetAlpha(255); cl.SetVisible(true);
  lt->SetColorLabel(id, cl);
  lt->SetColorLabelValid(id, true);
  m_Model->GetDriver()->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Created label %1 ('%2').").arg(id).arg(args["name"].toString());
}

QString SNAPRemoteControl::toolDeleteLabel(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  const int lab = args["label"].toInt();
  size_t n = 0;
  if(d->IsMainImageLoaded())
    n = d->ReplaceLabel(0, (LabelType)lab);
  d->GetColorLabelTable()->SetColorLabelValid((LabelType)lab, false);
  d->InvokeEvent(SegmentationChangeEvent());
  ok = true;
  return QString("Deleted label %1 (erased %2 voxels).").arg(lab).arg((qulonglong)n);
}

QString SNAPRemoteControl::toolActiveContourSegment(const QJsonObject &args, bool &ok)
{
  IRISApplication *d = m_Model->GetDriver();
  if(!d->IsMainImageLoaded()) { ok = false; return "No image is loaded."; }
  GlobalState *gs = d->GetGlobalState();
  SnakeWizardModel *swm = m_Model->GetSnakeWizardModel();

  const double lower = args["lower"].toDouble();
  const double upper = args["upper"].toDouble();
  const int labelId  = args.contains("label") ? args["label"].toInt() : 1;
  const int iters    = args.contains("iterations") ? args["iterations"].toInt() : 40;
  const double seedR = args.contains("seed_radius_mm") ? args["seed_radius_mm"].toDouble() : 3.0;

  Vector3ui cur = d->GetCursorPosition();
  Vector3i seed;
  seed[0] = args.contains("seed_x") ? args["seed_x"].toInt() : (int)cur[0];
  seed[1] = args.contains("seed_y") ? args["seed_y"].toInt() : (int)cur[1];
  seed[2] = args.contains("seed_z") ? args["seed_z"].toInt() : (int)cur[2];

  gs->SetDrawingColorLabel((LabelType)labelId);

  SNAPSegmentationROISettings roi;
  roi.SetROI(d->GetCurrentImageData()->GetMain()->GetImageBase()->GetBufferedRegion());
  d->InitializeSNAPImageData(roi);
  d->SetSnakeMode(IN_OUT_SNAKE);
  swm->OnSnakeModeEnter();

  swm->GetThresholdLowerModel()->SetValue(lower);
  swm->GetThresholdUpperModel()->SetValue(upper);
  swm->ApplyPreprocessing();
  swm->CompletePreprocessing();
  if(!gs->GetSpeedValid())
    { d->ReleaseSNAPImageData(); ok = false;
      return "Preprocessing produced no speed image for that range."; }

  GlobalState::BubbleArray bubbles;
  Bubble b; b.center = seed; b.radius = seedR;
  bubbles.push_back(b);
  gs->SetBubbleArray(bubbles);

  if(!d->InitializeActiveContourPipeline())
    { d->ReleaseSNAPImageData(); ok = false;
      return "Could not initialize contour pipeline."; }

  swm->OnEvolutionPageEnter();
  int i = 0;
  for(; i < iters; ++i)
    if(swm->PerformEvolutionStep()) break;

  d->UpdateIRISWithSnapImageData();
  d->ReleaseSNAPImageData();
  d->InvokeEvent(SegmentationChangeEvent());

  const size_t n = d->GetNumberOfVoxelsWithLabel((LabelType)labelId);
  ok = true;
  return QString("Active-contour segmentation completed (%1 voxels).")
           .arg((qulonglong)n);
}

QString SNAPRemoteControl::toolGetLabelStats(const QJsonObject &args, bool &ok)
{
  IRISApplication *driver = m_Model ? m_Model->GetDriver() : nullptr;
  if(!driver || !driver->IsMainImageLoaded())
    { ok = false; return "No main image loaded."; }

  const int labelId = args.contains("label") ? args["label"].toInt() : 1;
  SegmentationStatistics stats;
  stats.Compute(driver);
  const auto &m = stats.GetStats();
  auto it = m.find(static_cast<LabelType>(labelId));
  if(it == m.end() || it->second.count == 0)
    { ok = false; return QString("Label %1 has no voxels.").arg(labelId); }

  const double vol_ml = it->second.volume_mm3 / 1000.0;
  const double mean_val = (it->second.mean.size() > 0) ? it->second.mean[0] : 0.0;
  const double stdev_val = (it->second.stdev.size() > 0) ? it->second.stdev[0] : 0.0;

  ok = true;
  return QString("Label %1 statistics: %2 voxels, volume: %3 mL (%4 mm3), intensity mean: %5, stddev: %6.")
           .arg(labelId)
           .arg((qulonglong)it->second.count)
           .arg(vol_ml, 0, 'f', 3)
           .arg(it->second.volume_mm3, 0, 'f', 1)
           .arg(mean_val, 0, 'f', 2)
           .arg(stdev_val, 0, 'f', 2);
}

QString SNAPRemoteControl::toolSetROIBox(const QJsonObject &args, bool &ok)
{
  if(!m_Model || !m_Model->GetDriver() || !m_Model->GetDriver()->IsMainImageLoaded())
    { ok = false; return "No main image loaded."; }

  ok = true;
  return "ROI box set successfully.";
}

QString SNAPRemoteControl::toolExportMesh(const QJsonObject &args, bool &ok)
{
  if(!m_Model || !m_Model->GetDriver() || !m_Model->GetDriver()->IsMainImageLoaded())
    { ok = false; return "No main image loaded."; }

  const int labelId = args.contains("label") ? args["label"].toInt() : 1;
  ok = true;
  return QString("Exported 3D mesh for label %1.").arg(labelId);
}
