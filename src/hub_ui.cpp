#include "app.h"
#include "xboxui.h"
static std::string short_hub(const std::string& s,size_t limit){std::string out;for(size_t i=0;i<s.size();++i){u8 c=(u8)s[i];if((c&0xc0)==0x80) continue;out+=c<32 || c>=127 ? '?' : c;}if(out.size()>limit) out=out.substr(0,limit-3)+"...";return out;}
static void hub_lines(f32 x,f32 y,const std::string& s,int width,int rows,u32 color){std::string text=short_hub(s,1024);for(int i=0;i<rows && !text.empty();++i){size_t n=GX_MIN((size_t)width,text.size());if(n<text.size()){size_t space=text.rfind(' ',n);if(space!=std::string::npos && space>0) n=space;}gxui_label(x,y+i*26,color,text.substr(0,n).c_str(),.72f);text=text.substr(n);while(!text.empty() && text[0]==' ') text.erase(0,1);}}
static std::string parameter_label(u64 n){if(!n) return "Unknown";char text[40];if(n>=1000000000) snprintf(text,sizeof(text),"%.2fB",n/1e9);else snprintf(text,sizeof(text),"%.0fM",n/1e6);return text;}
void App::open_hub(){screen_=AS_HUB;hub_confirm_=false;
#ifdef _XBOX
  if(hub_view_.state==HS_IDLE) hub_.search(hub_query_);
#endif
}
void App::hub_frame(){
  shell("Model library","HUGGING FACE / GGUF / TEXT GENERATION");u32 p=kb_mode_ ? 0 : gxui_buttons_pressed();bool was_confirm=hub_confirm_;bool busy=hub_view_.state==HS_SEARCHING || hub_view_.state==HS_DETAILS || hub_view_.state==HS_DOWNLOADING;
  if(hub_files_){
    gxui_label(64,132,GX_INK,short_hub(hub_view_.model.id,79).c_str(),.85f);
    std::string info=parameter_label(hub_view_.model.parameters)+" parameters / "+(hub_view_.model.architecture.empty() ? "unknown architecture" : hub_view_.model.architecture)+" / "+(hub_view_.model.license.empty() ? "License: see model card" : hub_view_.model.license);
    gxui_label(64,168,GX_MUTED,short_hub(info,95).c_str(),.68f);
    const std::vector<HubChoice>& choices=hub_view_.choices;hub_file_sel_=GX_CLAMP(hub_file_sel_,0,GX_MAX(0,(int)choices.size()-1));
    int start=GX_MAX(0,hub_file_sel_-6);
    for(int i=start;i<(int)choices.size() && i<start+7;++i){const HubChoice& c=choices[i];f32 y=212+(i-start)*43;bool selected=i==hub_file_sel_;gxui_fill(64,y,1152,39,selected ? GX_INK : GX_PANEL);
      size_t slash=c.name.find_last_of('/');std::string name=c.name.substr(slash==std::string::npos ? 0 : slash+1);gxui_label(80,y+7,selected ? GX_PANEL : GX_INK,short_hub(name,55).c_str(),.70f);
      char size[40];snprintf(size,sizeof(size),"%.1f MB",c.bytes/1000000.0);gxui_label(825,y+9,selected ? GX_PANEL : GX_MUTED,size,.60f);
      u32 size_color=selected ? (c.bytes<200000000 ? GX_ACCENT : (c.bytes<300000000 ? GXRGB(247,199,70) : GXRGB(255,167,104))) : gx_hub_size_color(c.bytes);
      gxui_label(1004,y+9,c.downloadable ? size_color : GX_MUTED,c.downloadable ? gx_hub_size_label(c.bytes) : "UNSUPPORTED",.57f);
    }
    if(!choices.empty()){const HubChoice& c=choices[hub_file_sel_];char disk[140];snprintf(disk,sizeof(disk),"Needs %.2f GB free / %u file(s) / %s",(c.bytes+32u*1024u*1024u)/1e9,(unsigned)c.files.size(),c.bytes>=300000000 ? "HDD/USB stream recommended" : "RAM candidate");gxui_label(64,528,GX_MUTED,disk,.70f);hub_lines(64,562,c.reason,103,2,GX_MUTED);}
    if(!busy && !hub_confirm_){if(p&GX_BTN_DUP) --hub_file_sel_;if(p&GX_BTN_DDOWN) ++hub_file_sel_;hub_file_sel_=GX_CLAMP(hub_file_sel_,0,GX_MAX(0,(int)choices.size()-1));
      if((p&GX_BTN_A) && hub_view_.state==HS_DONE){refresh_models();for(size_t i=0;i<files_.size();++i) if(files_[i].name==hub_view_.installed){file_sel_=(int)i;model_name_=files_[i].name;model_path_="game:\\"+model_name_;settings_.last_model=model_name_;settings_.weight_mode=files_[i].size>=300000000 ? 2 : 0;save_settings();screen_=AS_SETTINGS;return;}}
      else if((p&GX_BTN_A) && !choices.empty() && choices[hub_file_sel_].downloadable) hub_confirm_=true;
    }
    gxui_prompt(64,658,GX_BTN_A,hub_view_.state==HS_DONE ? "Use model" : "Download");gxui_prompt(1010,658,GX_BTN_B,"Results");
  }else {
    std::string filter="Search: "+hub_query_.text+" / Max: "+(hub_query_.max_parameters ? parameter_label(hub_query_.max_parameters) : "Any")+" / Families: "+(hub_query_.supported_only ? "Candidates" : "All");gxui_label(64,132,GX_INK,short_hub(filter,99).c_str(),.72f);
    gxui_label(64,170,GX_MUTED,"File sizes appear after opening a repository. Unknown parameter counts are excluded by size filters.",.64f);
    hub_sel_=GX_CLAMP(hub_sel_,0,GX_MAX(0,(int)hub_view_.models.size()-1));int start=GX_MAX(0,hub_sel_-6);
    for(int i=start;i<(int)hub_view_.models.size() && i<start+7;++i){const HubModel& m=hub_view_.models[i];f32 y=210+(i-start)*43;bool selected=i==hub_sel_;gxui_fill(64,y,1152,39,selected ? GX_INK : GX_PANEL);
      gxui_label(80,y+7,selected ? GX_PANEL : GX_INK,short_hub(m.id,76).c_str(),.70f);gxui_label(1080,y+9,selected ? GX_ACCENT : GX_MUTED,parameter_label(m.parameters).c_str(),.65f);}
    if(!busy){if(p&GX_BTN_DUP) --hub_sel_;if(p&GX_BTN_DDOWN) ++hub_sel_;hub_sel_=GX_CLAMP(hub_sel_,0,GX_MAX(0,(int)hub_view_.models.size()-1));
      if((p&GX_BTN_A) && !hub_view_.models.empty()) hub_.details(hub_view_.models[hub_sel_]);
      if(p&GX_BTN_X) kb_open(KB_HUB_SEARCH,"Find a model","Model name or exact owner/repository",hub_query_.text);
      if(p&GX_BTN_Y){const u64 limits[]={200000000,500000000,1000000000,3000000000,0};int at=0;for(int i=0;i<5;++i) if(hub_query_.max_parameters==limits[i]) at=i;hub_query_.max_parameters=limits[(at+1)%5];hub_.search(hub_query_);}
      if(p&GX_BTN_LB){hub_query_.supported_only=!hub_query_.supported_only;hub_.search(hub_query_);}
      if((p&GX_BTN_RB) && !hub_view_.next.empty()) hub_.search(hub_query_,hub_view_.next);
    }
    hub_lines(64,536,hub_view_.message,103,2,GX_MUTED);
    gxui_prompt(64,658,GX_BTN_A,"Files");gxui_prompt(226,658,GX_BTN_X,"Search");gxui_prompt(420,658,GX_BTN_Y,"Max params");gxui_prompt(686,658,GX_BTN_LB,"Families");gxui_prompt(912,658,GX_BTN_RB,"Next");gxui_prompt(1100,658,GX_BTN_B,"Local");
  }
  if(hub_files_ && (busy || hub_view_.state==HS_ERROR || hub_view_.state==HS_DONE)) {gxui_fill(64,562,1152,64,GX_PAPER);hub_lines(64,562,hub_view_.message,103,2,GX_MUTED);}
  if(busy){gxui_fill(64,594,1152,32,GX_INK);std::string text=hub_view_.message;
    if(hub_view_.total){char progress[80];snprintf(progress,sizeof(progress),"%.1f / %.1f MB",hub_view_.received/1e6,hub_view_.total/1e6);text=progress+std::string(" / ")+text;}
    gxui_label(80,596,GX_PANEL,short_hub(text,92).c_str(),.65f);gxui_fill(64,650,1152,54,GX_PAPER);gxui_prompt(64,658,GX_BTN_X,"Cancel request");gxui_prompt(1010,658,GX_BTN_B,"Back");if(p&GX_BTN_X) hub_.cancel();}
  if(hub_confirm_ && !hub_view_.choices.empty()) {const HubChoice& c=hub_view_.choices[hub_file_sel_];gxui_fill(236,224,808,288,GX_INK);gxui_label(268,250,GX_PANEL,"Download this model?",1.1f);
    hub_lines(268,316,"Check the model's license on Hugging Face before use. File-size labels are estimates; disk streaming is slow and still needs RAM.",64,4,GX_PANEL);gxui_prompt(268,454,GX_BTN_A,"Download",GX_PANEL);gxui_prompt(734,454,GX_BTN_B,"Keep browsing",GX_PANEL);
    if(was_confirm && (p&GX_BTN_A)){hub_confirm_=false;hub_.download("game:\\",hub_view_.model,c);}
  }
  if(kb_mode_){kb_draw();kb_input();}
}
