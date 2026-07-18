/*=========================================================================
  assistant_eval — headless ground-truth harness for the ITK-SNAP LLM agent.

  Holds a REAL IRISApplication (no GUI) and executes the agent's tool
  operations against it, then dumps ACTUAL state (per-label voxel counts AND
  per-label intensity ranges) so a test driver can verify that a segmentation
  really contains the voxels it claims — never trusting the LLM's text.

  Protocol (line-based over stdin/stdout):
    load <path>                         -> load a main image
    tool <name> k=v k=v ...             -> run one tool, prints RESULT {json}
    state                               -> prints STATE {json} (ground truth)
    reset                               -> reload the last image (blank seg)
    quit
  Only IRISApplication-backed tools are implemented here (the verifiable core);
  GUI-model-only tools (layout/mesh/smooth/interpolate) are covered by the
  selection harness, not this one (see ASSUMPTIONS.md A2).
=========================================================================*/
#include "IRISApplication.h"
#include "GenericImageData.h"
#include "ImageWrapperBase.h"
#include "LabelImageWrapper.h"
#include "ImageIODelegates.h"
#include "ColorLabelTable.h"
#include "ColorLabel.h"
#include "SegmentationStatistics.h"
#include "SegmentationUpdateIterator.h"
#include "GlobalState.h"
#include "SNAPCommon.h"
#include "SNAPEvents.h"
#include "IRISException.h"
#include "UIReporterDelegates.h"
#include "SystemInterface.h"
#include "ColorMap.h"
#include "itksys/SystemTools.hxx"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionConstIteratorWithIndex.h"

#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <cmath>

/* ---- minimal headless delegates (from IRISApplicationTest.cxx) ---- */
class DummySystemInfoDelegate : public SystemInfoDelegate {
public:
  DummySystemInfoDelegate(const char *a){ m_Exe = a; }
  std::string GetApplicationDirectory() override { return itksys::SystemTools::GetFilenamePath(m_Exe); }
  std::string GetApplicationFile() override { return m_Exe; }
  std::string GetApplicationPermanentDataLocation() override { return ".itksnap.eval"; }
  std::string GetUserDocumentsLocation() override { return ".itksnap.eval"; }
  std::string GetTempDirectory() override { return "."; }
  std::string EncodeServerURL(const std::string &u) override { return u; }
  typedef SystemInfoDelegate::GrayscaleImage GrayscaleImage;
  typedef SystemInfoDelegate::RGBAImageType RGBAImageType;
  void LoadResourceAsImage2D(std::string, GrayscaleImage *) override {}
  void LoadResourceAsRegistry(std::string, Registry &) override {}
  void WriteRGBAImage2D(std::string, RGBAImageType *) override {}
protected: std::string m_Exe;
};
class DummyPreset : public AbstractColorMapPresetNameSource {
public: std::string GetPresetName(ColorMap::SystemPreset p, bool) override { return "cm" + std::to_string((int)p); }
};

typedef LabelImageWrapper LabelWrapper;
typedef ImageWrapperBase::FloatImageType FloatImageType;

static std::string g_lastPath;

/* ---- small helpers ---- */
static std::string esc(const std::string &s){ std::string o; for(char c:s){ if(c=='"'||c=='\\') o+='\''; else if(c=='\n') o+=' '; else o+=c; } return o; }

// parse "k=v" tokens after the tool name
static std::map<std::string,std::string> parseArgs(std::istringstream &iss){
  std::map<std::string,std::string> m; std::string tok;
  while(iss >> tok){ auto p = tok.find('='); if(p!=std::string::npos) m[tok.substr(0,p)] = tok.substr(p+1); }
  return m;
}
static double numArg(std::map<std::string,std::string>&a,const char*k,double d){ auto it=a.find(k); return it==a.end()?d:atof(it->second.c_str()); }
static int    intArg(std::map<std::string,std::string>&a,const char*k,int d){ auto it=a.find(k); return it==a.end()?d:atoi(it->second.c_str()); }
static std::string strArg(std::map<std::string,std::string>&a,const char*k,const char*d){ auto it=a.find(k); return it==a.end()?std::string(d):it->second; }

static FloatImageType::Pointer mainFloat(IRISApplication *d){
  ScalarImageWrapperBase *sc = d->GetCurrentImageData()->GetMain()->GetDefaultScalarRepresentation();
  FloatImageType::Pointer f = sc->CreateCastToFloatPipeline("eval");
  f->Update();
  return f;
}

/* ---- STATE dump: the ground truth ---- */
static void printState(IRISApplication *d){
  std::ostringstream o; o << "STATE {";
  bool loaded = d->IsMainImageLoaded();
  o << "\"loaded\":" << (loaded?"true":"false");
  if(loaded){
    Vector3ui sz = d->GetCurrentImageData()->GetMain()->GetSize();
    o << ",\"dims\":[" << sz[0] << "," << sz[1] << "," << sz[2] << "]";
    ScalarImageWrapperBase *sc = d->GetCurrentImageData()->GetMain()->GetDefaultScalarRepresentation();
    o << ",\"img_min\":" << sc->GetImageMinAsDouble() << ",\"img_max\":" << sc->GetImageMaxAsDouble();
    Vector3ui c = d->GetCursorPosition();
    o << ",\"cursor\":[" << c[0] << "," << c[1] << "," << c[2] << "]";
    o << ",\"undo\":" << (d->IsUndoPossible()?"true":"false") << ",\"redo\":" << (d->IsRedoPossible()?"true":"false");

    // per-label voxel count + intensity range (iterate seg + main float together)
    LabelWrapper *seg = d->GetSelectedSegmentationLayer();
    std::map<int,long> cnt; std::map<int,double> imin, imax;
    if(seg){
      typedef LabelWrapper::ImageType LabelImageType;
      LabelImageType *limg = seg->GetModifiableImage();
      FloatImageType::Pointer f = mainFloat(d);
      itk::ImageRegionConstIterator<LabelImageType> itL(limg, limg->GetBufferedRegion());
      itk::ImageRegionConstIterator<FloatImageType> itF(f, limg->GetBufferedRegion());
      for(; !itL.IsAtEnd(); ++itL, ++itF){
        int lab = (int)itL.Get(); if(lab==0) continue;
        double v = itF.Get();
        cnt[lab]++;
        if(imin.find(lab)==imin.end()){ imin[lab]=v; imax[lab]=v; }
        else { if(v<imin[lab]) imin[lab]=v; if(v>imax[lab]) imax[lab]=v; }
      }
    }
    ColorLabelTable *lt = d->GetColorLabelTable();
    o << ",\"labels\":[";
    bool first=true;
    for(auto &kv : cnt){
      if(!first) o << ","; first=false;
      std::string nm = lt->GetColorLabel(kv.first).GetLabel();
      o << "{\"id\":" << kv.first << ",\"name\":\"" << esc(nm) << "\",\"voxels\":" << kv.second
        << ",\"imin\":" << imin[kv.first] << ",\"imax\":" << imax[kv.first] << "}";
    }
    o << "]";
    // count of defined (valid) labels
    o << ",\"n_valid_labels\":" << lt->GetNumberOfValidLabels();
  }
  o << "}";
  std::cout << o.str() << std::endl;
}

static void result(bool ok, const std::string &text, const std::string &extra=""){
  std::cout << "RESULT {\"ok\":" << (ok?"true":"false") << ",\"text\":\"" << esc(text) << "\""
            << (extra.empty()?"":("," + extra)) << "}" << std::endl;
}

/* ---- tool execution against the real driver ---- */
static void runTool(IRISApplication *d, const std::string &name, std::map<std::string,std::string> &a){
  try {
    if(!d->IsMainImageLoaded() && name!="load_image"){ result(false, "no image loaded"); return; }

    if(name=="threshold_segment"){
      double lo = numArg(a,"lower",-1e30), hi = numArg(a,"upper",1e30);
      int lab = intArg(a,"label",1); std::string nm = strArg(a,"name","segmentation");
      GlobalState *gs = d->GetGlobalState();
      ColorLabelTable *lt = d->GetColorLabelTable();
      ColorLabel cl = lt->GetColorLabel(lab); cl.SetLabel(nm.c_str()); lt->SetColorLabel(lab, cl);
      gs->SetDrawingColorLabel((LabelType)lab);
      FloatImageType::Pointer f = mainFloat(d);
      LabelWrapper *seg = d->GetSelectedSegmentationLayer();
      auto region = seg->GetImageBase()->GetBufferedRegion();
      SegmentationUpdateIterator it(seg, region, (LabelType)lab, gs->GetDrawOverFilter());
      itk::ImageRegionConstIterator<FloatImageType> itF(f, region);
      long n=0;
      for(; !it.IsAtEnd(); ++it, ++itF){ double v=itF.Get(); if(v>=lo && v<=hi){ it.PaintAsForeground(); n++; } }
      it.Finalize("threshold");
      d->RecordCurrentLabelUse(); d->InvokeEvent(SegmentationChangeEvent());
      std::ostringstream e; e << "\"voxels\":" << n << ",\"label\":" << lab;
      result(true, "segmented " + nm, e.str());
    }
    else if(name=="clear_segmentation"){ d->ResetIRISSegmentationImage(); result(true,"cleared segmentation"); }
    else if(name=="clear_label"){ int lab=intArg(a,"label",1); size_t n=d->ReplaceLabel(0,(LabelType)lab); std::ostringstream e; e<<"\"voxels\":"<<n; result(true,"cleared label",e.str()); }
    else if(name=="replace_label"){ int fr=intArg(a,"from_label",1),to=intArg(a,"to_label",0); size_t n=d->ReplaceLabel((LabelType)to,(LabelType)fr); std::ostringstream e; e<<"\"voxels\":"<<n; result(true,"replaced",e.str()); }
    else if(name=="measure_volume"){
      int lab=intArg(a,"label",1); SegmentationStatistics s; s.Compute(d); auto &m=s.GetStats();
      auto it=m.find((LabelType)lab); if(it==m.end()||it->second.count==0){ result(false,"label empty"); return; }
      std::ostringstream e; e<<"\"voxels\":"<<(long)it->second.count<<",\"volume_ml\":"<<(it->second.volume_mm3/1000.0);
      result(true,"measured",e.str());
    }
    else if(name=="count_voxels"){ int lab=intArg(a,"label",1); size_t n=d->GetNumberOfVoxelsWithLabel((LabelType)lab); std::ostringstream e; e<<"\"voxels\":"<<n; result(true,"counted",e.str()); }
    else if(name=="set_active_label"){ int lab=intArg(a,"label",1); d->GetGlobalState()->SetDrawingColorLabel((LabelType)lab); std::ostringstream e; e<<"\"active\":"<<lab; result(true,"active label set",e.str()); }
    else if(name=="rename_label"){ int lab=intArg(a,"label",1); std::string nm=strArg(a,"name","label"); ColorLabelTable*lt=d->GetColorLabelTable(); ColorLabel cl=lt->GetColorLabel(lab); cl.SetLabel(nm.c_str()); lt->SetColorLabel(lab,cl); result(true,"renamed"); }
    else if(name=="set_label_color"){ int lab=intArg(a,"label",1); ColorLabelTable*lt=d->GetColorLabelTable(); ColorLabel cl=lt->GetColorLabel(lab); cl.SetRGB((unsigned char)intArg(a,"r",255),(unsigned char)intArg(a,"g",0),(unsigned char)intArg(a,"b",0)); lt->SetColorLabel(lab,cl); result(true,"colored"); }
    else if(name=="move_cursor"){ Vector3ui c; c[0]=intArg(a,"x",0);c[1]=intArg(a,"y",0);c[2]=intArg(a,"z",0); d->SetCursorPosition(c,true); std::ostringstream e; e<<"\"cursor\":["<<c[0]<<","<<c[1]<<","<<c[2]<<"]"; result(true,"moved cursor",e.str()); }
    else if(name=="focus_label"){
      int lab=intArg(a,"label",1); LabelWrapper*seg=d->GetSelectedSegmentationLayer();
      typedef LabelWrapper::ImageType LabelImageType; LabelImageType*img=seg->GetModifiableImage();
      itk::ImageRegionConstIteratorWithIndex<LabelImageType> it(img,img->GetBufferedRegion());
      double sx=0,sy=0,sz=0; long n=0;
      for(;!it.IsAtEnd();++it) if((int)it.Get()==lab){ auto ix=it.GetIndex(); sx+=ix[0];sy+=ix[1];sz+=ix[2];n++; }
      if(n==0){ result(false,"label empty"); return; }
      Vector3ui c; c[0]=(unsigned)(sx/n);c[1]=(unsigned)(sy/n);c[2]=(unsigned)(sz/n); d->SetCursorPosition(c,true);
      std::ostringstream e; e<<"\"cursor\":["<<c[0]<<","<<c[1]<<","<<c[2]<<"]"; result(true,"focused",e.str());
    }
    else if(name=="undo"){ if(!d->IsUndoPossible()){ result(false,"nothing to undo"); return; } d->Undo(); result(true,"undone"); }
    else if(name=="redo"){ if(!d->IsRedoPossible()){ result(false,"nothing to redo"); return; } d->Redo(); result(true,"redone"); }
    else if(name=="save_workspace"){ d->SaveProject(strArg(a,"path","eval_ws.itksnap")); result(true,"saved workspace"); }
    else if(name=="save_statistics"){ d->ExportSegmentationStatistics(strArg(a,"path","eval_stats.txt").c_str()); result(true,"saved stats"); }
    else if(name=="save_labels"){ d->SaveLabelDescriptions(strArg(a,"path","eval_labels.txt").c_str()); result(true,"saved labels"); }
    else if(name=="save_annotations"){ d->SaveAnnotations(strArg(a,"path","eval.annot").c_str()); result(true,"saved annotations"); }
    else if(name=="get_scene_overview" || name=="get_cursor_info"){ result(true,"ok"); }
    else if(name=="unload_main_image"){ d->UnloadMainImage(); result(true,"unloaded"); }
    else { result(false, "tool not verifiable headless: " + name); }
  } catch(IRISException &e){ result(false, std::string("iris error: ")+e.what()); }
    catch(std::exception &e){ result(false, std::string("error: ")+e.what()); }
}

int main(int argc, char *argv[]){
  DummySystemInfoDelegate sidel(argv[0]);
  SystemInterface::SetSystemInfoDelegate(&sidel);
  DummyPreset preset; ColorMap::SetColorMapPresetNameSource(&preset);
  IRISApplication::Pointer app = IRISApplication::New();

  std::cout << "READY" << std::endl;
  std::string line;
  while(std::getline(std::cin, line)){
    std::istringstream iss(line); std::string cmd; iss >> cmd;
    if(cmd=="quit" || cmd=="exit") break;
    else if(cmd=="load"){
      std::string path; iss >> path; g_lastPath = path;
      try {
        IRISWarningList wl; app->OpenImage(path.c_str(), MAIN_ROLE, wl);
        if(!app->GetSelectedSegmentationLayer()) app->AddBlankSegmentation();
        result(app->IsMainImageLoaded(), app->IsMainImageLoaded()?"loaded":"load failed");
      } catch(std::exception &e){ result(false, std::string("load error: ")+e.what()); }
    }
    else if(cmd=="reset"){
      try { IRISWarningList wl; app->OpenImage(g_lastPath.c_str(), MAIN_ROLE, wl);
            if(!app->GetSelectedSegmentationLayer()) app->AddBlankSegmentation();
            result(true,"reset"); }
      catch(std::exception &e){ result(false, std::string("reset error: ")+e.what()); }
    }
    else if(cmd=="tool"){ std::string name; iss >> name; auto a=parseArgs(iss); runTool(app, name, a); }
    else if(cmd=="state"){ printState(app); }
    else if(cmd.empty()) continue;
    else result(false, "unknown command: " + cmd);
  }
  return 0;
}
