#include "app.h"
#include "xboxui.h"
#include "server.h"
#include "version.h"
#include "textstream.h"
#ifdef _XBOX
#include <xtl.h>
#include <xbox.h>
#endif

static std::string short_text(const std::string& text,size_t chars){
  std::string out;for(size_t i=0;i<text.size();++i){u8 c=(u8)text[i];if((c&0xc0)==0x80) continue;out+=c>=127 ? '?' : (c<32 ? ' ' : c);}
  if(out.size()>chars) out=out.substr(0,chars-3)+"...";return out;
}
static void paragraph(f32 x,f32 y,const std::string& text,int columns,int rows,u32 color=GX_INK){
  std::string line;int row=0;size_t start=0;
  std::string clean=short_text(text,4096);
  while(start<clean.size() && row<rows){size_t end=GX_MIN(start+(size_t)columns,clean.size());
    if(end<clean.size()){size_t space=clean.rfind(' ',end);if(space!=std::string::npos && space>start) end=space;}
    line=clean.substr(start,end-start);gxui_label(x,y+row*28,color,line.c_str(),.85f);++row;start=end;while(start<clean.size() && clean[start]==' ') ++start;
  }
}
static std::string countdown(double seconds){
  int n=(int)ceil(GX_CLAMP(seconds,0.0,604800.0));char text[40];
  if(n>=3600) snprintf(text,sizeof(text),"%02d:%02d:%02d",n/3600,(n/60)%60,n%60);
  else snprintf(text,sizeof(text),"%02d:%02d",n/60,n%60);return text;
}
void App::shell(const char* title,const char* subtitle){
  gxui_fill(0,0,1280,12,GX_INK);gxui_fill(64,40,7,66,GX_ACCENT);
  if(!strcmp(title,"360AI")){gxui_label(90,35,GX_INK,"360",1.5f);gxui_label(162,35,GX_ACCENT,"AI",1.5f);}
  else gxui_label(90,35,GX_INK,title,1.5f);
  gxui_label(92,88,GX_MUTED,subtitle,.70f);
  gxui_label(1088,40,GX_MUTED,"360AI",.85f);gxui_label(1088,72,GX_MUTED,GX_VERSION,.65f);
  gxui_label(1032,112,GX_MUTED,save_status_.c_str(),.60f);
  gxui_fill(64,640,1152,2,GXRGB(195,201,189));
}
void App::home(){assistant_.cancel_followup();screen_=AS_HOME;confirm_id_=0;answer_confirm_.clear();answer_draft_.clear();kb_mode_=KB_NONE;chat_notice_.clear();}
void App::home_frame(){
  shell("360AI","YOUR XBOX. A LITTLE MORE USEFUL.");
  const char* titles[]={"ASSISTANT","NOTES","TIMERS","MODELS"};
  const char* subtitles[]={"A little conversation. A lot of useful tools.","Keep the things you want to remember.","Make time for what comes next.","Choose a local language model."};
  for(int i=0;i<4;++i){f32 x=64+(i%2)*584,y=158+(i/2)*222;bool selected=i==home_sel_;
    gxui_fill(x+4,y+5,568,200,GXRGB(205,210,195));gxui_fill(x,y,568,200,selected ? GX_INK : GX_PANEL);
    gxui_fill(x,y,selected ? 8 : 3,200,selected ? GX_ACCENT : GXRGB(190,198,180));
    char ordinal[4];snprintf(ordinal,sizeof(ordinal),"%02d",i+1);gxui_label(x+28,y+20,selected ? GX_ACCENT : GX_MUTED,ordinal,1.5f);
    gxui_label(x+104,y+24,selected ? GX_PANEL : GX_INK,titles[i],1.05f);
    paragraph(x+28,y+84,subtitles[i],36,2,selected ? GXRGB(213,222,208) : GX_MUTED);
    char detail[64];if(i==0) snprintf(detail,sizeof(detail),"PREDEFINED REPLIES / READY NOW");
    else snprintf(detail,sizeof(detail),"%u %s",(unsigned)(i==1 ? assistant_.note_count() : (i==2 ? assistant_.timer_count() : files_.size())),i==1 ? "SAVED NOTES" : (i==2 ? "ACTIVE TIMERS" : "AVAILABLE MODELS"));
    gxui_label(x+28,y+158,selected ? GX_ACCENT : GX_MUTED,detail,.65f);
    if(selected) gxui_prompt(x+430,y+145,GX_BTN_A,"Open",GX_PANEL);
  }
  UpdateStatus update=updater_.status();
  if(update.state==US_AVAILABLE || update.state==US_READY){gxui_fill(64,604,1152,30,GX_INK);gxui_label(80,606,GX_ACCENT,("360AI "+update.version+" is available. Press START for Updates.").c_str(),.65f);}
  gxui_prompt(64,658,GX_BTN_A,"Open");gxui_prompt(240,658,GX_BTN_X,"Ask");gxui_prompt(408,658,GX_BTN_Y,"Quick tools");gxui_prompt(736,658,GX_BTN_START,"Updates");gxui_prompt(1010,658,GX_BTN_DUP,"Choose");
  u32 p=gxui_buttons_pressed();if(p&GX_BTN_DLEFT) home_sel_&=~1;if(p&GX_BTN_DRIGHT) home_sel_|=1;
  if(p&GX_BTN_DUP) home_sel_&=1;if(p&GX_BTN_DDOWN) home_sel_|=2;
  if(p&GX_BTN_START) screen_=AS_UPDATES;
  else if(p&GX_BTN_X){open_assistant();kb_open(KB_CHAT,"Talk to 360AI","Say hello, ask a question, or type a command","");}
  else if(p&GX_BTN_Y){screen_=AS_TOOLS;tool_sel_=0;}
  else if(p&GX_BTN_A){confirm_id_=0;chat_notice_.clear();switch(home_sel_){case 0:open_assistant();break;case 1:screen_=AS_NOTES;break;case 2:screen_=AS_TIMERS;break;case 3:screen_=AS_BROWSE;break;}}
}
void App::local_command(const std::string& command){
  AssistantReply reply=assistant_.respond(command,assistant_context());chat_notice_=reply.text;
  queue_assistant_save();
}
void App::queue_assistant_save(){
#ifdef _XBOX
  if(assistant_.dirty()) save_status_=assistant_store_.submit(assistant_.snapshot()) ? "Saving..." : "Not saved";
#endif
}
void App::poll_assistant_save(){
  std::string saved;bool ok=false;
  if(assistant_store_.poll(saved,ok)){
    if(ok){assistant_.saved_snapshot(saved);save_status_=assistant_.dirty() ? "Saving..." : "Saved";}
    else {save_status_="Not saved";chat_notice_="Could not save assistant data. Check that the drive is writable; your data remains in memory.";}
  }
}
void App::notes_frame(){
  shell("Notes","A PLACE FOR THE THINGS THAT MATTER.");
  const std::vector<Assistant::Note>& notes=assistant_.notes();note_sel_=GX_CLAMP(note_sel_,0,GX_MAX(0,(int)notes.size()-1));
  gxui_fill(64,146,528,422,GX_PANEL);gxui_fill(616,146,600,422,GX_PANEL);
  if(notes.empty()){paragraph(92,190,"Nothing here yet. Press X to write your first note.",30,4);}
  else {int start=GX_MAX(0,note_sel_-5);for(int i=start;i<(int)notes.size() && i<start+6;++i){f32 y=160+(i-start)*64;bool selected=i==note_sel_;
      if(selected) gxui_fill(76,y,504,60,GX_INK);char number[24];snprintf(number,sizeof(number),"%02d",notes[i].id);
      gxui_label(92,y+13,selected ? GX_ACCENT : GX_MUTED,number,.8f);gxui_label(150,y+13,selected ? GX_PANEL : GX_INK,short_text(notes[i].text,31).c_str(),.8f);}
    gxui_label(644,164,GX_MUTED,"THE FULL NOTE",.7f);
    note_view_.update(notes[note_sel_].text,40,12);if(note_view_id_!=notes[note_sel_].id){note_view_id_=notes[note_sel_].id;note_view_.scroll(-100000);}
    const std::vector<std::string>& detail=note_view_.lines();for(int i=note_view_.top();i<(int)detail.size() && i<note_view_.top()+12;++i)
      gxui_label(644,210+(i-note_view_.top())*28,GX_INK,detail[i].c_str(),.85f);
  }
  if(!chat_notice_.empty()) paragraph(64,588,chat_notice_,38,1,GX_MUTED);
  gxui_label(644,592,GX_MUTED,"LB / RB: read longer notes",.65f);
  gxui_prompt(64,658,GX_BTN_A,"Edit");gxui_prompt(232,658,GX_BTN_X,"New note");gxui_prompt(448,658,GX_BTN_Y,"Delete");gxui_prompt(656,658,GX_BTN_START,"Undo");gxui_prompt(1010,658,GX_BTN_B,"Home");
  u32 p=gxui_buttons_pressed();if(kb_mode_){kb_draw();kb_input();return;}
  if(confirm_id_){gxui_fill(280,270,720,180,GX_INK);gxui_label(312,292,GX_PANEL,"Delete this note?",1.1f);gxui_prompt(312,384,GX_BTN_A,"Delete",GX_PANEL);gxui_prompt(650,384,GX_BTN_B,"Keep",GX_PANEL);
    if(p&GX_BTN_A){char command[64];snprintf(command,sizeof(command),"delete note %d",confirm_id_);local_command(command);confirm_id_=0;}
    if(p&GX_BTN_B){confirm_id_=0;kb_swallow_=true;}return;}
  if(p&GX_BTN_DUP) --note_sel_;if(p&GX_BTN_DDOWN) ++note_sel_;note_sel_=GX_CLAMP(note_sel_,0,GX_MAX(0,(int)notes.size()-1));
  if(p&GX_BTN_LB) note_view_.scroll(-12);if(p&GX_BTN_RB) note_view_.scroll(12);
  if(p&GX_BTN_X) kb_open(KB_NOTE_NEW,"Write a note","Your note is saved on this drive","");
  else if(p&GX_BTN_START) local_command("undo");
  else if(!notes.empty() && (p&GX_BTN_A)){edit_id_=notes[note_sel_].id;kb_open(KB_NOTE_EDIT,"Edit your note","Change the text, then press START",notes[note_sel_].text);}
  else if(!notes.empty() && (p&GX_BTN_Y)) confirm_id_=notes[note_sel_].id;
}
void App::timers_frame(){
  shell("Timers","KEEP 360AI OPEN. WE WILL KEEP AN EYE ON THE TIME.");
  const std::vector<Assistant::Timer>& timers=assistant_.timers();timer_sel_=GX_CLAMP(timer_sel_,0,GX_MAX(0,(int)timers.size()-1));
  if(timers.empty()){gxui_fill(64,150,1152,412,GX_PANEL);gxui_label(96,192,GX_INK,"A moment for yourself.",1.3f);paragraph(96,260,"Press X to start a timer. Try 5 minutes called tea. Timers run while this app stays open.",65,4);}
  else for(size_t i=0;i<timers.size();++i){f32 y=146+i*54;bool selected=(int)i==timer_sel_;gxui_fill(64,y,1152,50,selected ? GX_INK : GX_PANEL);
    gxui_label(88,y+8,selected ? GX_PANEL : GX_INK,short_text(timers[i].label,38).c_str(),.85f);
    gxui_label(748,y+8,selected ? GX_ACCENT : GX_INK,countdown(timers[i].paused ? timers[i].left : timers[i].due-gx_time_seconds()).c_str(),1.0f);
    gxui_label(1036,y+12,selected ? GX_PANEL : GX_MUTED,timers[i].paused ? "PAUSED" : "RUNNING",.65f);}
  if(!chat_notice_.empty()) paragraph(64,596,chat_notice_,82,1,GX_MUTED);
  gxui_prompt(64,658,GX_BTN_A,"Pause / run");gxui_prompt(310,658,GX_BTN_X,"New");gxui_prompt(466,658,GX_BTN_Y,"Cancel");gxui_prompt(654,658,GX_BTN_RB,"+1 min");gxui_prompt(1010,658,GX_BTN_B,"Home");
  u32 p=gxui_buttons_pressed();if(kb_mode_){kb_draw();kb_input();return;}
  if(confirm_id_){gxui_fill(280,270,720,180,GX_INK);gxui_label(312,292,GX_PANEL,"Cancel this timer?",1.1f);gxui_prompt(312,384,GX_BTN_A,"Cancel timer",GX_PANEL);gxui_prompt(700,384,GX_BTN_B,"Keep",GX_PANEL);
    if(p&GX_BTN_A){char command[64];snprintf(command,sizeof(command),"cancel timer %d",confirm_id_);local_command(command);confirm_id_=0;}
    if(p&GX_BTN_B){confirm_id_=0;kb_swallow_=true;}return;}
  if(p&GX_BTN_DUP) --timer_sel_;if(p&GX_BTN_DDOWN) ++timer_sel_;timer_sel_=GX_CLAMP(timer_sel_,0,GX_MAX(0,(int)timers.size()-1));
  if(p&GX_BTN_X) kb_open(KB_TIMER_NEW,"Set a timer","Try 5 minutes called tea","");
  else if(!timers.empty()){
    char command[96];int id=timers[timer_sel_].id;
    if(p&GX_BTN_A){snprintf(command,sizeof(command),"%s timer %d",timers[timer_sel_].paused ? "resume" : "pause",id);local_command(command);}
    else if(p&GX_BTN_RB){snprintf(command,sizeof(command),"extend timer %d by 1 minute",id);local_command(command);}
    else if(p&GX_BTN_LB){snprintf(command,sizeof(command),"restart timer %d",id);local_command(command);}
    else if(p&GX_BTN_Y) confirm_id_=id;
  }
}
void App::tools_frame(){
  shell("Quick tools","A FEW SHORTCUTS. LESS TYPING.");
  const char* names[]={"CALCULATE","CONVERT","NEW TIMER","NEW NOTE","SAVED ANSWERS","COMMAND GUIDE"};
  const char* tips[]={"Numbers, percentages and expressions.","Length, weight, temperature and more.","A break, a reminder, a cup of tea.","Write it down. Find it later.","Add, edit and test your own replies.","See everything the assistant can do."};
  for(int i=0;i<6;++i){f32 x=64+(i%2)*584,y=148+(i/2)*148;bool selected=i==tool_sel_;
    gxui_fill(x,y,568,132,selected ? GX_INK : GX_PANEL);gxui_fill(x,y,5,132,selected ? GX_ACCENT : GXRGB(190,198,180));
    gxui_label(x+24,y+18,selected ? GX_PANEL : GX_INK,names[i],.9f);paragraph(x+24,y+68,tips[i],36,2,selected ? GXRGB(207,217,198) : GX_MUTED);}
  gxui_prompt(64,658,GX_BTN_A,"Use tool");gxui_prompt(1010,658,GX_BTN_B,"Home");
  u32 p=gxui_buttons_pressed();if(p&GX_BTN_DLEFT) tool_sel_&=~1;if(p&GX_BTN_DRIGHT) tool_sel_|=1;
  if(p&GX_BTN_DUP) tool_sel_=GX_MAX(tool_sel_-2,tool_sel_%2);if(p&GX_BTN_DDOWN) tool_sel_=GX_MIN(tool_sel_+2,4+tool_sel_%2);
  if(p&GX_BTN_A){
    if(tool_sel_==2){screen_=AS_TIMERS;kb_open(KB_TIMER_NEW,"Set a timer","Try 5 minutes called tea","");}
    else if(tool_sel_==3){screen_=AS_NOTES;kb_open(KB_NOTE_NEW,"Write a note","Your note is saved on this drive","");}
    else if(tool_sel_==4) open_answers();
    else {open_assistant();const char* initial[]={"calculate ","convert ","","","teach ",""};
      if(tool_sel_==5) assistant_send("help");else kb_open(KB_CHAT,"360AI tools",tool_sel_==4 ? "teach favorite game: Halo 3" : "Complete the command, then press START",initial[tool_sel_]);}
  }
}
void App::restart_for_update(){
#ifdef _XBOX
  if(assistant_.dirty()) queue_assistant_save();assistant_store_.stop();poll_assistant_save();
  if(assistant_.dirty()){chat_notice_="Notes could not be saved. Update restart stopped. Check the drive and try again.";assistant_store_.start("game:\\360AI-assistant.json");return;}
  hub_.stop();updater_.stop();gen_stop_=true;server_stop();gx_thread_join(gen_thread_);gen_thread_=0;gx_thread_join(load_thread_);load_thread_=0;
  XLaunchNewImage("game:\\360AI-updater.xex",0);
  chat_notice_="Installer could not start. Copy the complete USB ZIP again.";assistant_store_.start("game:\\360AI-assistant.json");
#else
  chat_notice_="Restart installation is available on the Xbox.";
#endif
}
void App::updates_frame(){
  shell("Updates","DIRECT FROM GITHUB. RIGHT HERE ON YOUR XBOX.");UpdateStatus update=updater_.status();
  gxui_fill(64,150,1152,396,GX_PANEL);gxui_label(92,178,GX_MUTED,"INSTALLED",.70f);gxui_label(92,214,GX_INK,GX_VERSION,1.5f);
  gxui_label(632,178,GX_MUTED,"LATEST RELEASE",.70f);gxui_label(632,214,GX_INK,update.version.empty() ? "--" : update.version.c_str(),1.5f);
  const char* action=update.state==US_AVAILABLE ? "Download update" : (update.state==US_READY ? "Restart & install" : "Check now");
  paragraph(92,292,update.message,74,3);
  if(update.state==US_DOWNLOADING && update.total){f32 ratio=(f32)GX_MIN(update.received,update.total)/(f32)update.total;gxui_fill(92,390,1096,12,GXRGB(207,214,198));gxui_fill(92,390,1096*ratio,12,GX_ACCENT);char progress[80];snprintf(progress,sizeof(progress),"%.1f / %.1f MB",update.received/1048576.0,update.total/1048576.0);gxui_label(92,416,GX_MUTED,progress,.75f);}
  gxui_label(92,482,GX_MUTED,settings_.auto_update ? "Check on launch: ON (background)" : "Check on launch: OFF",.8f);
  paragraph(64,566,chat_notice_.empty() ? "App updates keep your models, notes and settings. Keep 32 MB free. Restarting ends session timers." : chat_notice_,85,2,GX_MUTED);
  bool busy=update.state==US_CHECKING || update.state==US_DOWNLOADING;
  if(!busy) gxui_prompt(64,658,GX_BTN_A,action);
  if(busy || update.state==US_READY || update.state==US_ERROR) gxui_prompt(440,658,GX_BTN_X,busy ? "Cancel" : "Discard");
  gxui_prompt(680,658,GX_BTN_Y,"Auto-check");gxui_prompt(1010,658,GX_BTN_B,"Home");
  u32 p=gxui_buttons_pressed();
  if(p&GX_BTN_Y){settings_.auto_update=!settings_.auto_update;save_settings();update_auto_checked_=true;}
  if(p&GX_BTN_X){if(busy) updater_.cancel();else if(update.state==US_READY || update.state==US_ERROR) updater_.discard();}
  if(!busy && (p&GX_BTN_A)){chat_notice_.clear();update_auto_checked_=true;
    if(update.state==US_READY) update_restart_=true;
    else updater_.start("game:\\",update.state==US_AVAILABLE);
  }
}
void App::frame(){
  HubState previous_hub=hub_view_.state;hub_.view(hub_view_);
  if(previous_hub!=HS_FILES && hub_view_.state==HS_FILES){hub_files_=true;hub_file_sel_=0;}
  if(previous_hub!=HS_MODELS && hub_view_.state==HS_MODELS){hub_files_=false;hub_sel_=0;}
  if(previous_hub!=HS_DONE && hub_view_.state==HS_DONE) refresh_models();
#ifdef _XBOX
  if(!update_auto_checked_ && gx_time_seconds()>=update_auto_at_){update_auto_checked_=true;if(settings_.auto_update) updater_.start("game:\\");}
#endif
  poll_assistant_save();assistant_poll();gxui_begin_frame(GX_PAPER);
  switch(screen_){case AS_HOME:home_frame();break;case AS_NOTES:notes_frame();break;case AS_TIMERS:timers_frame();break;case AS_TOOLS:tools_frame();break;
    case AS_BROWSE:browse_frame();break;case AS_SETTINGS:settings_frame();break;case AS_LOADING:loading_frame();break;case AS_CHAT:chat_frame();break;case AS_UPDATES:updates_frame();break;case AS_HUB:hub_frame();break;case AS_ANSWERS:answers_frame();break;}
  if(!timer_notice_.empty() && gx_time_seconds()<timer_notice_until_ && !kb_mode_){gxui_fill(64,110,1152,40,GX_INK);gxui_label(80,115,GX_PANEL,short_text(timer_notice_,90).c_str(),.75f);}
  gxui_end_frame();u32 p=gxui_buttons_pressed();bool swallowed=kb_swallow_;kb_swallow_=false;
  // Join inference/server workers only after releasing the renderer's GPU lock.
  if(update_restart_){update_restart_=false;restart_for_update();}
  if(!swallowed && !kb_mode_ && (p&(GX_BTN_B|GX_BTN_BACK))){
    confirm_id_=0;
    if(screen_==AS_CHAT && !assistant_mode_){gen_stop_=true;server_stop();gx_thread_join(gen_thread_);gen_thread_=0;engine_.reset();kv_=KVCache();model_loaded_=false;turns_.clear();chat_log_.clear();screen_=AS_BROWSE;}
    else if(screen_==AS_ANSWERS){if(!answer_confirm_.empty()) answer_confirm_.clear();else {answer_draft_.clear();screen_=AS_TOOLS;}}
    else if(screen_==AS_HUB){if(hub_confirm_){hub_confirm_=false;}
      else if(hub_files_){hub_files_=false;}else screen_=AS_BROWSE;}
    else if(screen_==AS_SETTINGS) screen_=AS_BROWSE;
    else if(screen_!=AS_LOADING) home();
  }
  if(!loading_ && load_done_ && screen_==AS_LOADING){load_done_=false;
    if(load_ok_){model_loaded_=true;screen_=AS_CHAT;if(engine_.tok().completion_only()) chat_notice_="Story toy: try Once upon a time";
      if(settings_.server_on) server_start(&engine_,&kv_,&settings_,eng_mutex_);}
    else {if(chat_notice_.empty()) chat_notice_="Model load failed. Check 360AI.log.";screen_=AS_SETTINGS;}}
}
#ifndef _XBOX
void App::test_frame(u32 buttons){gxui_test_input(buttons);frame();}
void App::test_seed(){
  AssistantContext c=assistant_context();assistant_.respond("remember Recharge the controllers before Friday.",c);assistant_.respond("remember Halo 3 co-op: finish the next mission.",c);
  assistant_.respond("set timer 5 minutes called Tea break",c);assistant_.respond("set timer 20 minutes called One more round",c);
  gx_file_entry f;f.name="SmolLM2-135M-Instruct-Q8_0.gguf";f.size=144811072;files_.push_back(f);f.name="gemma-3-270m-it-Q8_0.gguf";f.size=291545600;files_.push_back(f);
}
bool App::test_capture(const char* name,const char* path){
  if(!strcmp(name,"hub") || !strcmp(name,"hub-files")){
    HubView v;v.revision=10;v.page=2;v.has_previous=true;v.state=!strcmp(name,"hub") ? HS_MODELS : HS_FILES;v.message=!strcmp(name,"hub") ? "Cached results. No network request needed." : "Choose a quantization. All shards are downloaded together.";
    HubModel m;m.id="example/SmolLM2-135M-Instruct-GGUF";m.revision=std::string(40,'a');m.architecture="llama";m.parameters=135000000;m.license="apache-2.0";v.models.push_back(m);v.model=m;
    const char* labels[]={"SmolLM2-135M-Q4_K_M.gguf","Gemma-270M-Q8_0.gguf","Qwen2.5-0.5B-Q4_K_M.gguf","GLM-MoE-Q4.gguf"};const u64 sizes[]={105454144,291545600,491400032,5000000000ull};
    for(int i=0;i<4;++i){HubChoice c;c.name=labels[i];c.bytes=sizes[i];c.downloadable=i!=3;c.reason=i==3 ? "Unsupported architecture: MoE is not implemented." : "Candidate architecture. GGUF and memory checks still apply.";HubFile f;f.path=c.name;f.bytes=c.bytes;c.files.push_back(f);v.choices.push_back(c);}
    hub_.test_view(v);hub_files_=v.state==HS_FILES;screen_=AS_HUB;test_frame(0);return gxui_capture(path);
  }
  test_seed();if(!strcmp(name,"notes")) screen_=AS_NOTES;else if(!strcmp(name,"timers")) screen_=AS_TIMERS;else if(!strcmp(name,"tools")) screen_=AS_TOOLS;
  else if(!strcmp(name,"updates")) screen_=AS_UPDATES;else if(!strcmp(name,"models")) screen_=AS_BROWSE;else if(!strcmp(name,"chat")){open_assistant();assistant_send("hello");assistant_send("what do you think about the xbox 360");}
  else if(!strcmp(name,"keyboard")){open_assistant();kb_open(KB_CHAT,"Talk to 360AI","Say hello, ask a question, or type a command","");}
  test_frame(0);return gxui_capture(path);
}
#endif
