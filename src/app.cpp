#include "app.h"
#include "xboxui.h"
#include "platform.h"
#include "server.h"
#include "gpu.h"
#include "version.h"
#include "textstream.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <time.h>
#include <algorithm>

static const char* kBrowseDir = "game:";
static const char* kSettingsPath="game:\\360AI.json";
static const char* kAssistantPath="game:\\360AI-assistant.json";

AppSettings::AppSettings(){
  ctx_size = 256;
  temperature = 0.8f;
  top_p=1.0f;repeat_penalty=1.0f;
  topk = 40;
  debug_level = DC_WARN;
  server_on = false;
  chat_on = true;
  auto_update = true;
  models_smallest_first = true;
  backend_mode = 0;
  weight_mode = 0;
  reply_tokens = 48;
}

App::App(){
  screen_ = AS_HOME;
  file_sel_ = 0;
  file_scroll_ = 0;
  set_sel_ = 0;
  home_sel_=tool_sel_=note_sel_=timer_sel_=confirm_id_=edit_id_=0;
  answer_sel_=0;
  note_view_id_=0;
  kb_mode_ = KB_NONE;
  kb_cur_ = 0;
  kb_upper_ = false;
  kb_swallow_ = false;
  model_loaded_ = false;
  assistant_mode_ = false;
  timer_notice_until_ = 0;
  update_auto_checked_=false;update_restart_=false;update_auto_at_=0;
  hub_sel_=hub_file_sel_=0;hub_files_=hub_confirm_=false;
  loading_ = false;
  load_ok_ = false;
  load_done_ = false;
  load_cur_ = 0;
  load_total_ = 0;
  generating_ = false;
  gen_stop_ = false;
  gen_start_time_ = 0;
  gen_first_time_=0;gen_prompt_done_=gen_prompt_total_=0;
  gen_layer_done_=gen_layer_total_=0;
  gen_last_time_ = 0;
  gen_count_ = 0;
  gen_last_ms_ = 0;
  gen_tps_ = 0;
  chat_revision_=1;view_revision_=0;view_tokens_=-1;view_generating_=false;
  load_label_[0] = 0;
  gen_mutex_ = gx_mutex_create();
  eng_mutex_ = gx_mutex_create();
  gen_thread_ = load_thread_ = 0;
}

App::~App(){
  hub_.stop();
  updater_.stop();
  assistant_store_.stop();
  gen_stop_ = true;
  server_stop();
  gx_thread_join(gen_thread_); gx_thread_join(load_thread_);
  if(gen_mutex_) gx_mutex_destroy(gen_mutex_);
  if(eng_mutex_) gx_mutex_destroy(eng_mutex_);
}

static void utf8_truncate_scroll(const std::string& s, size_t max_chars,
                                 std::string& out, bool from_end){
  size_t n = 0, i = 0, start = 0;
  for(; i < s.size(); ){
    unsigned char c = (unsigned char)s[i];
    size_t adv = 1;
    if((c & 0x80) == 0) adv = 1;
    else if((c & 0xE0) == 0xC0) adv = 2;
    else if((c & 0xF0) == 0xE0) adv = 3;
    else if((c & 0xF8) == 0xF0) adv = 4;
    i += adv;
    ++n;
    if(from_end && n > max_chars){
      start = i;
    }
  }
  if(from_end) out = s.substr(start);
  else out = s.substr(0, GX_MIN(s.size(), max_chars * 4));
}

int App::run(){
  settings_.load(kSettingsPath);
  hub_query_=settings_.hub_query;
  assistant_.load(kAssistantPath);
  if(!assistant_.load_rules("game:\\assistant-rules.json")) assistant_.load_rules("game:\\assistant-rules.example.json");
  gx_set_debug_level(settings_.debug_level);
  if(!gxui_init()) return 1;
  if(!assistant_store_.start(kAssistantPath)) save_status_="Saving unavailable";
  update_auto_at_=gx_time_seconds()+2;

  refresh_models();

  bool running = true;
  while(running){frame();gx_sleep_ms(10);}

  server_stop();
  gen_stop_ = true;
  gx_thread_join(gen_thread_); gen_thread_ = 0;
  gx_thread_join(load_thread_); load_thread_ = 0;
  gxui_shutdown();
  return 0;
}

void App::refresh_models(){
  files_.clear();file_sel_=file_scroll_=0;
  std::vector<gx_file_entry> all_files;
  gx_list_dir(kBrowseDir, ".gguf", all_files);
  for(size_t i=0;i<all_files.size();++i){
    std::string prefix;u32 part=0,count=0;
    gx_file_entry entry=all_files[i];
    if(gx_gguf_split_name(entry.name,prefix,part,count)){
      if(part!=1) continue;
      for(size_t j=0;j<all_files.size();++j){
        std::string other;u32 p=0,n=0;
        if(j!=i && gx_gguf_split_name(all_files[j].name,other,p,n) && other==prefix && n==count)
          entry.size+=all_files[j].size;
      }
    }
    files_.push_back(entry);
  }
  sort_models();

  for(size_t i=0;i<files_.size();++i)
    if(files_[i].name==settings_.last_model){file_sel_=(int)i;break;}
  // A fresh install should start with the smaller conversational model.
  if(settings_.last_model.empty()) for(size_t i=0;i<files_.size();++i)
    if(files_[i].name=="SmolLM2-135M-Instruct-Q8_0.gguf"){file_sel_=(int)i;break;}

}

static bool model_name_less(const gx_file_entry& a,const gx_file_entry& b){return a.name<b.name;}
static bool model_size_less(const gx_file_entry& a,const gx_file_entry& b){return a.size==b.size ? a.name<b.name : a.size<b.size;}
void App::sort_models(){std::string selected=file_sel_>=0 && file_sel_<(int)files_.size() ? files_[file_sel_].name : "";
  std::sort(files_.begin(),files_.end(),settings_.models_smallest_first ? model_size_less : model_name_less);file_scroll_=0;
  for(size_t i=0;i<files_.size();++i) if(files_[i].name==selected){file_sel_=(int)i;break;}}
void App::browse_frame(){
  shell("Models","PICK A MODEL. SET IT UP. MAKE IT YOURS.");
  u32 buttons=gxui_buttons_pressed();if(buttons&GX_BTN_X){open_assistant();return;}
  if(buttons&GX_BTN_Y){open_hub();return;}
  if(buttons&GX_BTN_LB){settings_.models_smallest_first=!settings_.models_smallest_first;sort_models();save_settings();}
  if(files_.empty()){gxui_label(92,184,GX_INK,"No models on this drive.",1.2f);gxui_label(92,250,GX_MUTED,"Copy a GGUF beside 360AI.xex, or open Assistant.",.85f);}
  else {
    file_sel_=GX_CLAMP(file_sel_,0,(int)files_.size()-1);
    if(file_sel_<file_scroll_) file_scroll_=file_sel_;if(file_sel_>=file_scroll_+7) file_scroll_=file_sel_-6;
    for(int i=file_scroll_;i<(int)files_.size() && i<file_scroll_+7;++i){f32 y=146+(i-file_scroll_)*52;bool selected=i==file_sel_;
      gxui_fill(64,y,1152,48,selected ? GX_INK : GX_PANEL);std::string name=files_[i].name;if(name.size()>63) name=name.substr(0,60)+"...";
      gxui_label(86,y+10,selected ? GX_PANEL : GX_INK,name.c_str(),.85f);char size[40];snprintf(size,sizeof(size),"%.0f MB",files_[i].size/1048576.0);
      gxui_label(1080,y+12,selected ? GX_ACCENT : GX_MUTED,size,.7f);
    }
    const gx_file_entry& selected=files_[file_sel_];char hint[128];double reserve=ceil(selected.size/100000000.0)/10;
    snprintf(hint,sizeof(hint),"Reserve %.2f GB on HDD/USB. %s",reserve,selected.size>320u*1024u*1024u ? "HDD stream: very slow." : "RAM recommended.");
    gxui_label(64,532,GX_MUTED,hint,.75f);gxui_label(848,532,gx_hub_size_color(selected.size),gx_hub_size_label(selected.size),.75f);
    const char* tip=selected.name=="stories260K.gguf" ? "Tiny story toy: mostly nonsense; not a chat assistant." : (selected.name=="stories15M-q4_0.gguf" ? "Small story toy: simple completions; not instruction chat." : "Leave compute/storage on Auto. Quick preset gives shorter replies.");
    gxui_label(64,576,GX_MUTED,tip,.7f);
  }
  gxui_prompt(64,658,GX_BTN_A,"Set up");gxui_prompt(288,658,GX_BTN_X,"Assistant");gxui_prompt(572,658,GX_BTN_Y,"Get models");gxui_prompt(1010,658,GX_BTN_B,"Home");
  gxui_prompt(64,612,GX_BTN_LB,settings_.models_smallest_first ? "Sort: smallest first" : "Sort: name");
  if(files_.empty()) return;
  u32 p = gxui_buttons_pressed();
  if(p & GX_BTN_DUP) file_sel_--;
  if(p & GX_BTN_DDOWN) file_sel_++;
  file_sel_=GX_CLAMP(file_sel_,0,(int)files_.size()-1);
  if(p & GX_BTN_A){
    if(hub_view_.state==HS_DOWNLOADING){chat_notice_="Wait for the model download/check, or cancel it in Get models.";return;}
    model_path_ = std::string(kBrowseDir) + "\\" + files_[file_sel_].name;
    model_name_ = files_[file_sel_].name;
    settings_.last_model=model_name_;
    save_settings();
    screen_ = AS_SETTINGS;
  }
}

void App::settings_frame(){
  f32 lh=30; shell("Model setup",model_name_.c_str());
  static const char* dbg_names[DC_COUNT] = {
    "everything", "warning", "error", "nothing"
  };

  const char* backends[]={"Auto (benchmark)","CPU","GPU forced (experimental)"};
  const char* storage[]={"Auto (RAM when it fits)","RAM","HDD/USB stream (8 MB cache)"};
  char line[13][192];
  snprintf(line[0], 192, "Context size        %d", settings_.ctx_size);
  snprintf(line[1], 192, "Temperature         %.2f", (double)settings_.temperature);
  snprintf(line[2], 192, "Top-K               %d", settings_.topk);
  snprintf(line[3], 192, "Debug level         %s", dbg_names[settings_.debug_level]);
  snprintf(line[4], 192, "OpenAI / MCP server %s", settings_.server_on ? "ON" : "OFF");
  snprintf(line[5], 192, "Chat on Xbox        %s", settings_.chat_on ? "ON" : "OFF");
  snprintf(line[6], 192, "Compute backend     %s", backends[settings_.backend_mode]);
  snprintf(line[7], 192, "Weight storage      %s",storage[settings_.weight_mode]);
  snprintf(line[8], 192, "Reply token limit   %d",settings_.reply_tokens);
  const char* profiles[]={"Custom","Quick (short replies)","Balanced","Creative"};
  snprintf(line[9],192,"Top-P               %.2f",(double)settings_.top_p);
  snprintf(line[10],192,"Repeat penalty      %.2f",(double)settings_.repeat_penalty);
  snprintf(line[11],192,"Reply preset        %s",profiles[settings_.profile()]);
  snprintf(line[12],192,"Load model");

  f32 y = 138;
  for(int i = 0; i < 13; ++i){
    if(i == set_sel_){
      gxui_fill(64,y,1152,30,GX_INK);
      gxui_label(80,y,GX_PANEL,line[i],.80f);
    } else {
      gxui_label(80,y,GX_INK,line[i],.80f);
    }
    y += lh;
  }

  gxui_label(64,548,GX_MUTED,set_sel_==11 ? "Quick: short replies / Auto compute and storage" : "GPU is experimental. Auto compares measured backends.",.7f);
  gxui_label(64,582,GX_MUTED,chat_notice_.empty() ? gx_gpu_diagnostic() : chat_notice_.c_str(),.7f);
  gxui_prompt(64,658,GX_BTN_A,"Change / load");gxui_prompt(1010,658,GX_BTN_B,"Models");

  if(kb_mode_){
    kb_draw();
    kb_input();
    return;
  }

  u32 p = gxui_buttons_pressed();
  if(p & GX_BTN_DUP) set_sel_--;
  if(p & GX_BTN_DDOWN) set_sel_++;
  if(set_sel_ < 0) set_sel_ = 0;
  if(set_sel_ > 12) set_sel_ = 12;

  if(p & GX_BTN_A){
    switch(set_sel_){
      case 0: {
        char ib[32];
        snprintf(ib, 32, "%d", settings_.ctx_size);
        kb_open(KB_CTX, "Context size", "Tokens of KV cache (256-8192)", ib);
        break;
      }
      case 1: {
        char ib[32];
        snprintf(ib, 32, "%.2f", (double)settings_.temperature);
        kb_open(KB_TEMP, "Temperature", "Sampling temperature (0.00-2.00)", ib);
        break;
      }
      case 2: {
        char ib[32];
        snprintf(ib, 32, "%d", settings_.topk);
        kb_open(KB_TOPK, "Top-K", "Sampling pool size (1-200)", ib);
        break;
      }
      case 3:
        settings_.debug_level = (settings_.debug_level + 1) % DC_COUNT;
        gx_set_debug_level(settings_.debug_level);
        save_settings();
        break;
      case 4:
        settings_.server_on = !settings_.server_on;
        save_settings();
        break;
      case 5:
        settings_.chat_on = !settings_.chat_on;
        save_settings();
        break;
      case 6:
        settings_.backend_mode=(settings_.backend_mode+1)%3;
        save_settings();
        break;
      case 7:
        settings_.weight_mode=(settings_.weight_mode+1)%3;
        save_settings();
        break;
      case 8:
        {char ib[32];snprintf(ib,sizeof(ib),"%d",settings_.reply_tokens);
        kb_open(KB_REPLY,"Reply token limit","Maximum reply length (16-512)",ib);}
        break;
      case 9:
        {char ib[32];snprintf(ib,sizeof(ib),"%.2f",(double)settings_.top_p);
        kb_open(KB_TOPP,"Top-P","0.05-1.00 (1=off)",ib);}
        break;
      case 10:
        {char ib[32];snprintf(ib,sizeof(ib),"%.2f",(double)settings_.repeat_penalty);
        kb_open(KB_REPEAT,"Repeat penalty","1.00-1.50 (1=off)",ib);}
        break;
      case 11:
        settings_.apply_profile(settings_.profile()%3+1);
        chat_notice_="Preset applied. Compute and weight storage are set to Auto.";
        save_settings();
        break;
      case 12:
        chat_notice_.clear();
        save_settings();
        gx_gpu_enable(false);
        gx_set_debug_level(settings_.debug_level);
        start_load();
        screen_ = AS_LOADING;
        break;
    }
  }
}

void App::start_load(){
  if(loading_) return;
  assistant_mode_=false;turns_.clear();chat_log_.clear();chat_view_=ChatView();
  loading_ = true;
  load_ok_ = false;
  load_done_ = false;
  load_cur_ = 0;
  load_total_ = 0;
  strncpy(load_label_, "starting", sizeof(load_label_));
  load_label_[sizeof(load_label_)-1] = 0;
  gx_thread_join(load_thread_);
  load_thread_ = gx_thread_begin(&App::load_thread, this);
  if(!load_thread_){loading_=false;load_done_=true;chat_notice_="Could not start model loader. Try a smaller context.";GX_LOG_ERROR("load: could not start worker thread");}
}

void App::load_thread(void* arg){
  App* a = (App*)arg;
  a->do_load();
}

App* App::s_load_app = 0;

void App::load_progress_cb(const char* name, int cur, int total){
  App* a = s_load_app;
  if(!a) return;
  strncpy(a->load_label_, name, sizeof(a->load_label_));
  a->load_label_[sizeof(a->load_label_)-1] = 0;
  a->load_cur_ = cur;
  a->load_total_ = total;
}

void App::do_load(){
  gx_log(GX_DEBUG_ALL, "loading model: %s", model_path_.c_str());
  s_load_app = this;
  bool ok = engine_.load(model_path_.c_str(), &App::load_progress_cb,settings_.weight_mode==2,settings_.weight_mode==0,(u32)settings_.ctx_size);
  if(ok){load_progress_cb("comparing CPU/GPU",0,1);engine_.select_backend(settings_.backend_mode);}
  if(ok && !engine_.storage_ok()) ok=false;
  s_load_app = 0;
  if(ok){
    const ModelCfg& c = engine_.cfg();
    u32 sl = GX_MIN((u32)settings_.ctx_size, c.max_seq_len);
    size_t kvb = engine_.kv_bytes(sl);
    if(gx_physical_free() < kvb + 5u * 1024u * 1024u){
      GX_LOG_ERROR("kv cache: not enough memory (%u KB needed)",
                   (unsigned)(kvb / 1024));
      ok = false;
    } else {
      kv_.init(sl, c.layer.kv_heads, c.layer.qkv_dim, c.num_layers);
      gx_log(GX_DEBUG_ALL, "kv cache: %.1f MB", (double)(kv_.bytes()/(1024.0f*1024.0f)));
    }
  }
  load_ok_ = ok ? 1 : 0;
  load_cur_ = 0;
  load_total_ = 0;
  strncpy(load_label_, ok ? "ready" : "FAILED", sizeof(load_label_));
  load_label_[sizeof(load_label_)-1] = 0;
  loading_ = false;
  load_done_ = true;
}

void App::loading_frame(){
  shell("Loading",model_name_.c_str());gxui_label(92,230,GX_INK,"Getting things ready.",1.4f);
  f32 fraction=load_total_>0 ? (f32)load_cur_/load_total_ : 0;fraction=GX_CLAMP(fraction,0.0f,1.0f);
  gxui_fill(92,338,1096,14,GXRGB(194,203,182));gxui_fill(92,338,1096*fraction,14,GX_ACCENT);
  gxui_label(92,386,GX_MUTED,load_label_,.9f);gxui_label(92,450,GX_MUTED,"Large models and drive streaming can take a while.",.75f);
}

void App::append_chat(const std::string& role, const std::string& text){
  chat_log_ += role;
  chat_log_ += text;
  chat_log_ += "\n";
  ++chat_revision_;
}

void App::chat_frame(){
  shell(assistant_mode_ ? "Talk" : "Model chat",assistant_mode_ ? "SAY HELLO. ASK A QUESTION. TRY A TOOL." : model_name_.c_str());
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);std::string body,notice=chat_notice_;
  bool changed=chat_revision_!=view_revision_ || gen_count_!=view_tokens_ || generating_!=view_generating_;
  if(changed){body=chat_log_;if(generating_){body+="Model: ";body+=gen_text_;}view_revision_=chat_revision_;view_tokens_=gen_count_;view_generating_=generating_;}
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
  const int max_lines=12;if(changed) chat_view_.update(body,79,max_lines);u32 p=gxui_buttons_pressed();
  if(!kb_mode_){if(p&GX_BTN_DUP) chat_view_.scroll(-1);if(p&GX_BTN_DDOWN) chat_view_.scroll(1);
    if(p&GX_BTN_LB) chat_view_.scroll(-max_lines);if(p&GX_BTN_RB) chat_view_.scroll(max_lines);if(p&GX_BTN_RTHUMB) chat_view_.latest();}
  gxui_fill(64,142,1152,398,GX_PANEL);gxui_fill(64,142,4,398,GX_ACCENT);
  const std::vector<std::string>& lines=chat_view_.lines();int start=chat_view_.top();
  for(int i=start;i<(int)lines.size() && i<start+max_lines;++i){u32 color=GX_INK;
    if(lines[i].compare(0,5,"You: ")==0) color=GX_MUTED;
    gxui_label(88,150+(i-start)*32,color,lines[i].c_str(),.85f);}
  std::string status=notice;
  if(status.empty() && !assistant_mode_){
    if(settings_.server_on) status=server_status_text();
    else status=std::string(engine_.backend_note())+" / "+(engine_.disk_streaming() ? "HDD/USB stream" : "RAM weights");
  }
  if(generating_){char progress[120];if(gen_mutex_) gx_mutex_lock(gen_mutex_);
    if(gen_count_==0) snprintf(progress,sizeof(progress),"Working: %u/%u prompt tokens | layer %u/%u",gen_prompt_done_,gen_prompt_total_,gen_layer_done_,gen_layer_total_);
    else snprintf(progress,sizeof(progress),"%.1f tokens/sec | %.0f ms/token | layer %u/%u",(double)gen_tps_,(double)gen_last_ms_,gen_layer_done_,gen_layer_total_);
    if(gen_mutex_) gx_mutex_unlock(gen_mutex_);status=progress;
  }
  if(status.size()>92) status=status.substr(0,89)+"...";gxui_label(64,554,GX_MUTED,status.c_str(),.75f);
  gxui_label(64,596,GX_MUTED,chat_view_.following() ? "D-pad scrolls / LB-RB pages / Right stick jumps to latest" : "READING HISTORY / Right stick jumps to latest",.65f);
  gxui_prompt(64,658,GX_BTN_A,"Write");gxui_prompt(244,658,GX_BTN_X,"New chat");gxui_prompt(468,658,GX_BTN_Y,assistant_mode_ ? "Tools" : "Stop");gxui_prompt(678,658,GX_BTN_START,"Save");gxui_prompt(1010,658,GX_BTN_B,assistant_mode_ ? "Home" : "Models");
  if(kb_mode_){kb_draw();kb_input();return;}
  if(assistant_mode_ && (p&GX_BTN_Y)){screen_=AS_TOOLS;return;}
  if(generating_ && (p&GX_BTN_Y)){gen_stop_=true;chat_notice_="Stopping after the current operation...";}
  else if((settings_.chat_on || assistant_mode_) && !generating_){
    if(p&GX_BTN_X) new_chat();else if(p&GX_BTN_START) export_chat();
    else if(p&GX_BTN_A) kb_open(KB_CHAT,assistant_mode_ ? "Talk to 360AI" : "Your message",assistant_mode_ ? "Say hello, ask a question, or type a command" : "Write to the selected model","");
  }
}

void App::save_settings(){
  if(!settings_.save(kSettingsPath)){
    chat_notice_="Could not save settings. Check HDD/USB is writable.";
    GX_LOG_WARN("settings: could not save game:\\360AI.json; check drive is writable");
  }
}
void App::new_chat(){
  if(eng_mutex_ && !gx_mutex_try_lock(eng_mutex_)){chat_notice_="API request running; try new chat when it finishes.";return;}
  gx_thread_join(gen_thread_);gen_thread_=0;
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  kv_.clear();turns_.clear();chat_log_.clear();gen_text_.clear();
  ++chat_revision_;
  chat_view_=ChatView();
  if(assistant_mode_) assistant_.reset_conversation();
  chat_notice_=assistant_mode_ ? "New chat. Saved notes and active timers are kept." : "New chat started. Model stays loaded.";
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
  if(eng_mutex_) gx_mutex_unlock(eng_mutex_);
}
void App::export_chat(){
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  std::string text=chat_log_;
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
  if(text.empty()){chat_notice_="No conversation to save.";return;}
  text=gx_utf8_clean("360AI "+std::string(GX_VERSION)+" - "+model_name_+"\n\n"+text);
  chat_notice_=gx_write_text_file("game:\\360AI-chat.txt",text) ?
    "Saved conversation to 360AI-chat.txt." : "Could not save chat. Check HDD/USB is writable.";
}

AssistantContext App::assistant_context() const{
  AssistantContext c;c.now=gx_time_seconds();c.free_bytes=gx_physical_free();c.version=GX_VERSION;c.profile=settings_.profile();
  for(size_t i=0;i<files_.size();++i) c.models.push_back(files_[i].name);
  time_t now=time(0);struct tm* local=localtime(&now);char text[64];
  if(local){if(strftime(text,sizeof(text),"%H:%M:%S",local)) c.clock=text;if(strftime(text,sizeof(text),"%Y-%m-%d",local)) c.date=text;}
  return c;
}
void App::open_assistant(){
  if(assistant_mode_ && !chat_log_.empty()){model_name_="360 Assistant";screen_=AS_CHAT;return;}
  assistant_mode_=true;model_name_="360 Assistant";
  turns_.clear();chat_log_.clear();chat_notice_.clear();chat_view_=ChatView();assistant_.reset_conversation();
  append_chat("Assistant: ","Hello there. Ask me something, or press Y for Quick tools.");
  screen_=AS_CHAT;
}
void App::assistant_send(const std::string& text){
  chat_view_.latest();chat_notice_.clear();append_chat("You: ",gx_utf8_clean(text));
  AssistantReply reply=assistant_.respond(text,assistant_context());
  if(reply.action==AA_PROFILE){
    settings_.apply_profile(reply.value);save_settings();
    reply.text="Model preset applied; compute/storage Auto. "+std::string(chat_notice_.empty() ? "Settings saved." : "Could not save preferences to the drive.");
  } else if(reply.action==AA_EXPORT){export_chat();reply.text=chat_notice_;}
  else if(reply.action==AA_CLEAR_CHAT) new_chat();
  else if(reply.action==AA_MODELS){assistant_mode_=false;chat_log_.clear();turns_.clear();chat_view_=ChatView();screen_=AS_BROWSE;return;}
  else if(reply.action==AA_UPDATES) screen_=AS_UPDATES;
  else if(reply.action==AA_MODEL_LIBRARY) open_hub();
  else if(reply.action==AA_ANSWERS) open_answers();
  queue_assistant_save();
  append_chat("Assistant: ",reply.text);
  if(chat_log_.size()>65536){size_t cut=chat_log_.find('\n',chat_log_.size()-49152);if(cut!=std::string::npos) chat_log_="[Earlier assistant messages trimmed]\n"+chat_log_.substr(cut+1);}
}
void App::assistant_poll(){
  std::vector<std::string> alerts;assistant_.poll(gx_time_seconds(),alerts);
  if(alerts.empty()) return;
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  for(size_t i=0;i<alerts.size();++i){append_chat("Assistant: ",alerts[i]);chat_notice_=alerts[i];}
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
  timer_notice_=alerts.size()>1 ? "Several timers finished. Ask timer history in 360 Assistant." : alerts[0];
  if(timer_notice_.size()>90) timer_notice_=gx_utf8_clean(timer_notice_.substr(0,87))+"...";
  timer_notice_until_=gx_time_seconds()+10;
  chat_view_.latest();
}

void App::start_generate(const std::string& prompt){
  if(generating_) return;
  chat_view_.latest();
  gx_thread_join(gen_thread_); gen_thread_ = 0;
  generating_ = true;
  gen_stop_ = false;
  GX_LOG_ALL("gen: send %d chars", (int)prompt.size());
  {
    std::string one;
    for(size_t i = 0; i < prompt.size() && one.size() < 200; ++i){
      char c = prompt[i];
      one += (c == '\n') ? '|' : c;
    }
    GX_LOG_ALL("gen: msg %s", one.c_str());
  }
  gen_start_time_ = gx_time_seconds();
  gen_last_time_ = gen_start_time_;
  gen_count_ = 0;
  gen_first_time_=0;gen_prompt_done_=gen_prompt_total_=0;
  gen_layer_done_=gen_layer_total_=0;
  gen_last_ms_ = 0;
  gen_tps_ = 0;
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  gen_text_.clear();
  chat_notice_.clear();
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
  user_input_ = prompt;
  gen_thread_ = gx_thread_begin(&App::gen_thread, this);
  if(!gen_thread_){generating_=false;chat_notice_="Could not start generation. Try a smaller context.";}
}

void App::gen_thread(void* arg){
  App* a = (App*)arg;
  std::vector<int> ids;
  a->engine_.tok().encode_prompt(a->user_input_, ids);
  a->do_generate(ids);
}

void App::do_generate(const std::vector<int>& prompt){
  EngineCfg ec;
  ec.temperature = settings_.temperature;
  ec.top_k = (u32)settings_.topk;
  ec.greedy = settings_.temperature <= 0.001f;
  ec.stop = &gen_stop_;
  ec.cancel=0;
  ec.top_p=settings_.top_p;ec.repeat_penalty=settings_.repeat_penalty;

  std::vector<int> out;
  int maxn = GX_MIN(settings_.reply_tokens,(int)kv_.seq_len);
  if(eng_mutex_) gx_mutex_lock(eng_mutex_);
  engine_.generate(prompt,out,maxn,kv_,ec,&App::on_token,this,&App::on_prefill,&App::on_layer);
  GenerationStats stats=engine_.stats();
  if(eng_mutex_) gx_mutex_unlock(eng_mutex_);

  std::string reply;
  for(size_t i = 0; i < out.size(); ++i){
    reply += engine_.tok().decode_token(out[i]);
  }
  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  gen_text_ = reply;
  if(!reply.empty()){
    turns_.push_back(std::make_pair(1, reply));
    append_chat("Model: ", reply);
  }
  generating_ = false;
  gen_stop_ = false;
  if(stats.end_reason==GE_CANCELLED) chat_notice_=reply.empty() ? "Stopped." : "Stopped. Partial reply kept.";
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
}

void App::on_token(int id, const char*, void* user){
  App* a = (App*)user;
  double now = gx_time_seconds();
  if(a->gen_mutex_) gx_mutex_lock(a->gen_mutex_);
  a->gen_text_ += a->engine_.tok().decode_token(id);
  if(a->gen_count_ == 0){
    a->gen_first_time_=now;
    a->gen_last_ms_ = (f32)((now - a->gen_start_time_) * 1000.0);
    a->gen_tps_ = 0.0f;
  } else {
    a->gen_last_ms_ = (f32)((now - a->gen_last_time_) * 1000.0);
    a->gen_tps_ = (f32)((double)a->gen_count_ / (now - a->gen_first_time_));
  }
  a->gen_last_time_ = now;
  a->gen_count_++;
  if(a->gen_mutex_) gx_mutex_unlock(a->gen_mutex_);
}

void App::on_prefill(u32 done,u32 total,void* user){
  App* a=(App*)user;
  if(a->gen_mutex_) gx_mutex_lock(a->gen_mutex_);
  a->gen_prompt_done_=done;a->gen_prompt_total_=total;
  if(a->gen_mutex_) gx_mutex_unlock(a->gen_mutex_);
}
void App::on_layer(u32 done,u32 total,void* user){
  App* a=(App*)user;
  if(a->gen_mutex_) gx_mutex_lock(a->gen_mutex_);
  a->gen_layer_done_=done;a->gen_layer_total_=total;
  if(a->gen_mutex_) gx_mutex_unlock(a->gen_mutex_);
}

static const char kKbSet[] = "abcdefghijklmnopqrstuvwxyz0123456789.,-+_:/?@%* ()^'";
static const int kKbCols = 7;
static const int kKbChars = sizeof(kKbSet)-1;
static const int kKbDel = kKbChars;
static const int kKbOk = kKbChars+1;
static const int kKbCells = kKbChars+2;
static const int kKbRows = (kKbCells+kKbCols-1)/kKbCols;
static const f32 kKbCellW = 96;
static const f32 kKbCellH = 32;

void App::kb_open(int mode, const char* title, const char* desc, const std::string& initial){
  kb_mode_ = mode;
  kb_title_ = title;
  kb_desc_ = desc;
  kb_text_ = initial;
  kb_cur_ = 0;
  kb_upper_ = false;
  if(gxui_syskb_start((kb_title_ + " (" + kb_desc_ + ")").c_str(), desc, initial.c_str())){
    kb_mode_ = mode | KB_SYS;
  }
}

void App::kb_draw(){
  if(kb_mode_ & KB_SYS) return;
  const f32 px=304,py=138,pw=kKbCols*kKbCellW;
  gxui_fill(0,0,1280,720,GXRGBA(18,27,23,180));gxui_fill(264,118,752,502,GX_PANEL);gxui_fill(264,118,6,502,GX_ACCENT);
  gxui_label(px,py,GX_INK,kb_title_.c_str(),1.0f);gxui_label(px,182,GX_MUTED,kb_desc_.c_str(),.65f);
  gxui_fill(px,224,pw,40,GX_INK);std::string shown=kb_text_;const size_t visible=40;
  if(shown.size()>visible) shown=shown.substr(shown.size()-visible);gxui_label(px+10,228,GX_PANEL,(shown+"_").c_str(),1.0f);
  f32 gy=282;
  for(int i=0;i<kKbCells;++i){f32 x=px+(i%kKbCols)*kKbCellW,y=gy+(i/kKbCols)*kKbCellH;bool selected=i==kb_cur_;
    gxui_fill(x,y,kKbCellW-5,kKbCellH-4,selected ? GX_INK : GXRGB(220,224,211));u32 col=selected ? GX_ACCENT : GX_INK;
    char text[8];if(i<kKbChars){char ch=kKbSet[i];if(kb_upper_ && ch>='a' && ch<='z') ch-=32;snprintf(text,sizeof(text),"%c",ch==' ' ? '_' : ch);}
    else snprintf(text,sizeof(text),"%s",i==kKbDel ? "DEL" : "OK");gxui_label(x+16,y,col,text,.8f);
  }
  gxui_prompt(292,566,GX_BTN_A,"Type");gxui_prompt(460,566,GX_BTN_B,"Cancel");gxui_prompt(652,566,GX_BTN_LB,"Case");gxui_prompt(836,566,GX_BTN_START,"Done");
}

void App::kb_input(){
  if(kb_mode_ & KB_SYS){
    char buf[512];
    int st = gxui_syskb_poll(buf, sizeof(buf));
    if(st < 0) return;
    if(st > 0){
      kb_text_ = buf;
      kb_commit();
    } else {
      kb_mode_ = KB_NONE;kb_swallow_=true;
    }
    return;
  }
  u32 p = gxui_buttons_pressed();
  if(p & GX_BTN_DLEFT) kb_cur_ = (kb_cur_ + kKbCells - 1) % kKbCells;
  if(p & GX_BTN_DRIGHT) kb_cur_ = (kb_cur_ + 1) % kKbCells;
  if(p & GX_BTN_DUP) kb_cur_ = (kb_cur_ + kKbCells - kKbCols) % kKbCells;
  if(p & GX_BTN_DDOWN) kb_cur_ = (kb_cur_ + kKbCols) % kKbCells;
  if(p & (GX_BTN_LB | GX_BTN_RB)) kb_upper_ = !kb_upper_;
  if(p & GX_BTN_B){
    kb_mode_ = KB_NONE;
    kb_swallow_ = true;
    return;
  }
  if(p & (GX_BTN_START | GX_BTN_Y)){
    kb_commit();
    return;
  }
  if(p & GX_BTN_A){
    if(kb_cur_ < kKbChars){
      if(kb_text_.size() < ((kb_mode_&0xff)==KB_ANSWER_PHRASE ? 80u : 512u)){
        char c = kKbSet[kb_cur_];
        if(kb_upper_ && c >= 'a' && c <= 'z') c = (char)(c - 32);
        kb_text_ += c;
      }
    } else if(kb_cur_ == kKbDel){
      if(!kb_text_.empty()) kb_text_.erase(kb_text_.size() - 1);
    } else {
      kb_commit();
    }
  }
}

void App::kb_commit(){
  int mode = kb_mode_ & 0xFF;
  kb_mode_ = KB_NONE;
  if(mode==KB_ANSWER_PHRASE){
    std::string key;if(!Assistant::answer_phrase(kb_text_,key)){chat_notice_="Use a phrase of 3-80 bytes, with words or numbers.";return;}
    const std::vector<Assistant::Answer>& answers=assistant_.answers();for(size_t i=0;i<answers.size();++i) if(answers[i].phrase==key){answer_sel_=(int)i;chat_notice_="That phrase already exists. Press A to edit its answer.";return;}
    answer_draft_=key;kb_open(KB_ANSWER_TEXT,"Your answer","Literal text, 1-512 bytes. B cancels without saving.","");kb_swallow_=true;return;
  } else if(mode==KB_ANSWER_TEXT){
    if(assistant_.write_answer(answer_draft_,kb_text_,chat_notice_)){
      const std::vector<Assistant::Answer>& answers=assistant_.answers();for(size_t i=0;i<answers.size();++i) if(answers[i].phrase==answer_draft_) answer_sel_=(int)i;
      answer_preview_.clear();queue_assistant_save();
    }return;
  } else if(mode==KB_ANSWER_TEST){
    std::string key,response;
    answer_preview_=assistant_.preview_answer(kb_text_,key,response) ? "Matches: "+key+"\n\n"+response : "No saved text answer matches those words.";
    answer_view_key_.clear();chat_notice_="Preview only. Commands and file-based answers are not executed; command priority still applies in chat.";return;
  } else if(mode == KB_CTX){
    int v = atoi(kb_text_.c_str());
    if(v < 256) v = 256;
    if(v > 8192) v = 8192;
    settings_.ctx_size = v;
  } else if(mode == KB_TEMP){
    f32 t = (f32)atof(kb_text_.c_str());
    if(t < 0.0f) t = 0.0f;
    if(t > 2.0f) t = 2.0f;
    settings_.temperature = t;
  } else if(mode == KB_TOPK){
    int v = atoi(kb_text_.c_str());
    if(v < 1) v = 1;
    if(v > 200) v = 200;
    settings_.topk = v;
  } else if(mode == KB_REPLY){
    settings_.reply_tokens=GX_CLAMP(atoi(kb_text_.c_str()),16,512);
  } else if(mode == KB_TOPP){
    f32 value=(f32)atof(kb_text_.c_str());if(value!=value) value=1;
    settings_.top_p=GX_CLAMP(value,0.05f,1.0f);
  } else if(mode == KB_REPEAT){
    f32 value=(f32)atof(kb_text_.c_str());if(value!=value) value=1;
    settings_.repeat_penalty=GX_CLAMP(value,1.0f,1.5f);
  } else if(mode==KB_HUB_SEARCH){
    hub_query_.text=kb_text_.substr(0,96);hub_confirm_=false;hub_files_=false;
    settings_.hub_query=hub_query_;save_settings();
    if(gx_hub_id(hub_query_.text)){HubModel model;model.id=hub_query_.text;hub_.details(model);}else hub_.search(hub_query_);return;
  } else if(mode == KB_NOTE_NEW || mode == KB_NOTE_EDIT || mode == KB_TIMER_NEW){
    if(!kb_text_.empty()){
      if(mode==KB_TIMER_NEW) local_command("set timer "+kb_text_);
      else {chat_notice_=assistant_.write_note(mode==KB_NOTE_NEW ? 0 : edit_id_,kb_text_).text;queue_assistant_save();}
    }return;
  } else if(mode == KB_CHAT){
    if(!kb_text_.empty()){
      if(assistant_mode_){assistant_send(kb_text_);return;}
      turns_.push_back(std::make_pair(0, kb_text_));
      FittedChat fitted=gx_fit_chat(engine_.tok(),turns_,kv_.seq_len,(u32)settings_.reply_tokens);
      if(!fitted.fits){
        turns_.pop_back();chat_notice_="Message template does not fit. Increase context or reduce reply limit.";return;
      }
      append_chat("You: ", kb_text_);
      start_generate(fitted.prompt);
      if(gen_thread_){
        if(fitted.truncated) chat_notice_="Message shortened to fit context; full text stays in the transcript.";
        else if(fitted.dropped_messages) chat_notice_="Older messages skipped for context; scroll up to read them.";
      }
    }
  }
  if(mode!=KB_CHAT) save_settings();
}
