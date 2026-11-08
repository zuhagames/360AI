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
  backend_mode = 0;
  weight_mode = 0;
  reply_tokens = 48;
}

App::App(){
  screen_ = AS_BROWSE;
  file_sel_ = 0;
  file_scroll_ = 0;
  set_sel_ = 0;
  kb_mode_ = KB_NONE;
  kb_cur_ = 0;
  kb_upper_ = false;
  kb_swallow_ = false;
  model_loaded_ = false;
  assistant_mode_ = false;
  timer_notice_until_ = 0;
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
  load_label_[0] = 0;
  gen_mutex_ = gx_mutex_create();
  eng_mutex_ = gx_mutex_create();
  gen_thread_ = load_thread_ = 0;
}

App::~App(){
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
  assistant_.load(kAssistantPath);
  if(!assistant_.load_rules("game:\\assistant-rules.json")) assistant_.load_rules("game:\\assistant-rules.example.json");
  gx_set_debug_level(settings_.debug_level);
  if(!gxui_init()) return 1;

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
  for(size_t i = 0; i + 1 < files_.size(); ++i){
    for(size_t j = 0; j + 1 - i < files_.size(); ++j){
      if(files_[j].name > files_[j+1].name){
        gx_file_entry t = files_[j];
        files_[j] = files_[j+1];
        files_[j+1] = t;
      }
    }
  }

  for(size_t i=0;i<files_.size();++i)
    if(files_[i].name==settings_.last_model){file_sel_=(int)i;break;}
  // A fresh install should start with the smaller conversational model.
  if(settings_.last_model.empty()) for(size_t i=0;i<files_.size();++i)
    if(files_[i].name=="SmolLM2-135M-Instruct-Q8_0.gguf"){file_sel_=(int)i;break;}

  bool running = true;
  while(running){
    assistant_poll();
    gxui_begin_frame(0xFF101018);
    switch(screen_){
      case AS_BROWSE:    browse_frame();    break;
      case AS_SETTINGS:  settings_frame();  break;
      case AS_LOADING:   loading_frame();   break;
      case AS_CHAT:      chat_frame();      break;
    }
    if(!timer_notice_.empty() && gx_time_seconds()<timer_notice_until_){
      gxui_fill(36,610,1208,50,GXRGB(30,65,25));
      gxui_text_shadow(46,620,GXRGB(220,255,200),"%s",timer_notice_.c_str());
    }
    gxui_end_frame();

    u32 p = gxui_buttons_pressed();
    bool kb_sw = kb_swallow_;
    kb_swallow_ = false;
    if(!kb_sw && kb_mode_ == KB_NONE && screen_ == AS_CHAT){
      if(p & (GX_BTN_B | GX_BTN_BACK)){
        gen_stop_ = true;
        server_stop();
        gx_thread_join(gen_thread_); gen_thread_ = 0;
        engine_.reset();
        kv_ = KVCache();
        gx_log(GX_DEBUG_ALL, "unload: free memory %.0f MB",
               gx_physical_free() / (1024.0f * 1024.0f));
        model_loaded_ = false;
        assistant_mode_ = false;
        turns_.clear();
        chat_log_.clear();
        chat_notice_.clear();
        chat_view_=ChatView();
        screen_ = AS_BROWSE;
      }
    }
    if(!kb_sw && kb_mode_ == KB_NONE &&
       (p & (GX_BTN_B | GX_BTN_BACK)) &&
       screen_ != AS_CHAT && screen_ != AS_LOADING){
      if(screen_ == AS_SETTINGS) screen_ = AS_BROWSE;
    }
    if(!loading_ && load_done_ && screen_ == AS_LOADING){
      load_done_ = false;
      if(load_ok_){
        model_loaded_ = true;
        if(engine_.tok().completion_only()) chat_notice_="Story toy: continues your text. Try: Once upon a time";
        screen_ = AS_CHAT;
        if(settings_.server_on) server_start(&engine_, &kv_, &settings_, eng_mutex_);
      } else {
        if(chat_notice_.empty()) chat_notice_="Model load failed. Check 360AI.log for details.";
        screen_ = AS_SETTINGS;
      }
    }
    gx_sleep_ms(10);
  }

  server_stop();
  gen_stop_ = true;
  gx_thread_join(gen_thread_); gen_thread_ = 0;
  gx_thread_join(load_thread_); load_thread_ = 0;
  gxui_shutdown();
  return 0;
}

void App::browse_frame(){
  f32 cw = gxui_char_w(), lh = gxui_line_h();
  gxui_text_shadow(40, 30, GX_ACCENT, "360AI %s", GX_VERSION);
  gxui_text_shadow(40, 30 + lh, GXRGB(180,180,190),
                   "Gemma, SmolLM2, Llama, Qwen2/2.5 from %s", kBrowseDir);
  gxui_text_shadow(40,720-lh*2,GX_ACCENT,"X: 360 Assistant (instant, no model)   A: selected GGUF");
  if(gxui_buttons_pressed() & GX_BTN_X){open_assistant();return;}

  if(files_.empty()){
    gxui_text_shadow(40, 30 + lh*3, GXRGB(220,120,120),
                     "No .gguf files found next to 360AI.xex.");
    gxui_text_shadow(40, 30 + lh*4, GXRGB(150,150,160),
                     "Press X for the keyword assistant, or copy a GGUF beside the app.");
    return;
  }

  if(file_sel_ >= (int)files_.size()) file_sel_ = (int)files_.size() - 1;
  if(file_sel_ < 0) file_sel_ = 0;

  int visible = 12;
  if(file_sel_ < file_scroll_) file_scroll_ = file_sel_;
  if(file_sel_ >= file_scroll_ + visible) file_scroll_ = file_sel_ - visible + 1;

  f32 y = 30 + lh*3;
  for(int i = file_scroll_; i < (int)files_.size() && i < file_scroll_ + visible; ++i){
    u32 col = (i == file_sel_) ? GXRGB(255,255,255) : GXRGB(150,150,160);
    if(i == file_sel_){
      gxui_fill(36, y - 2, 900, lh + 2, GX_ACCENT_A(60));
      gxui_text_shadow(40, y, GXRGB(255,255,255), "> %s", files_[i].name.c_str());
    } else {
      gxui_text_shadow(40, y, col, "  %s", files_[i].name.c_str());
    }
    f32 mb = (f32)files_[i].size / (1024.0f*1024.0f);
    gxui_text_shadow(1030, y, GXRGB(120,140,120), "%.0f MB", mb);
    y += lh;
  }

  const gx_file_entry& selected=files_[file_sel_];
  double gb=(double)selected.size/1000000000.0;
  double reserve=gb>=1.0 ? ceil(gb) : ceil(gb*10.0)/10.0;
  gxui_text_shadow(40,720-lh*4,GXRGB(180,200,160),"Needs %.1f GB free on HDD/USB (%.2f GB of model files)",reserve,gb);
  const char* hint=selected.size>320u*1024u*1024u ?
    "Large model: HDD streaming recommended; generation can be very slow." :
    "Small model: RAM loading recommended. HDD streaming is optional.";
  if(selected.name=="stories260K.gguf") hint="Smallest toy (1.2 MB). Mostly nonsense; continues stories, not a chat assistant.";
  else if(selected.name=="stories15M-q4_0.gguf") hint="Small story toy (19 MB). Weak English story completion; no instruction chat.";
  else if(selected.name=="SmolLM2-135M-Instruct-Q8_0.gguf") hint="Recommended for chat. Small instruction model; use RAM or Auto storage.";
  gxui_text_shadow(40,720-lh*3,GXRGB(150,150,160),"%s",hint);

  u32 p = gxui_buttons_pressed();
  if(p & GX_BTN_DUP) file_sel_--;
  if(p & GX_BTN_DDOWN) file_sel_++;
  file_sel_=GX_CLAMP(file_sel_,0,(int)files_.size()-1);
  if(p & GX_BTN_A){
    model_path_ = std::string(kBrowseDir) + "\\" + files_[file_sel_].name;
    model_name_ = files_[file_sel_].name;
    settings_.last_model=model_name_;
    save_settings();
    screen_ = AS_SETTINGS;
  }
}

void App::settings_frame(){
  f32 lh = gxui_line_h();
  gxui_text_shadow(40, 30, GX_ACCENT, "Settings");
  gxui_text_shadow(40, 30 + lh, GXRGB(150,150,160), "Model: %s", model_name_.c_str());

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

  f32 y = 30 + lh*3;
  for(int i = 0; i < 13; ++i){
    if(i == set_sel_){
      gxui_fill(36, y - 2, 700, lh + 2, GX_ACCENT_A(60));
      gxui_text_shadow(40, y, GXRGB(255,255,255), "> %s", line[i]);
    } else {
      gxui_text_shadow(40, y, GXRGB(160,160,170), "  %s", line[i]);
    }
    y += lh;
  }

  f32 my = gx_physical_free() / (1024.0f*1024.0f);
  if(!chat_notice_.empty()) gxui_text_shadow(40,720-lh*5,GXRGB(220,180,120),"%s",chat_notice_.c_str());
  else if(set_sel_==11) gxui_text_shadow(40,720-lh*5,GXRGB(160,200,140),"Quick: 256 ctx/48 reply | Balanced: 512/128 | Creative: 512/192");
  gxui_text_shadow(40,720-lh*4,GXRGB(150,150,160),"GPU: %s",gx_gpu_diagnostic());
  gxui_text_shadow(40, 720 - lh*3, GXRGB(120,140,120), "Free memory: %.0f MB", my);

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
  f32 lh = gxui_line_h();
  f32 cx = 640, cy = 300;
  gxui_text_shadow(cx - 120, cy, GX_ACCENT, "Loading model...");
  gxui_text_shadow(cx - 120, cy + lh*2, GXRGB(180,180,190), "%s", model_name_.c_str());

  f32 bw = 480, bx = cx - bw/2, by = cy + lh*4;
  gxui_fill(bx, by, bw, 18, GXRGBA(40,40,60,200));
  f32 frac = 0.0f;
  if(load_total_ > 0) frac = (f32)load_cur_ / (f32)load_total_;
  if(frac < 0) frac = 0;
  if(frac > 1) frac = 1;
  gxui_fill(bx, by, bw * frac, 18, GXRGBA(80,160,90,220));
  gxui_text_shadow(cx - 120, by + 26, GXRGB(150,150,160), "%s", load_label_);

  f32 my = gx_physical_free() / (1024.0f*1024.0f);
  gxui_text_shadow(cx - 120, by + lh*2, GXRGB(120,140,120), "Free memory: %.0f MB", my);
}

void App::append_chat(const std::string& role, const std::string& text){
  chat_log_ += role;
  chat_log_ += text;
  chat_log_ += "\n";
}

void App::chat_frame(){
  f32 lh = gxui_line_h(), cw = gxui_char_w();
  gxui_text_shadow(40, 20, GX_ACCENT,assistant_mode_ ? "Assistant" : "Chat");
  gxui_text_shadow(200, 20, GXRGB(150,150,160), "%s", model_name_.c_str());
  if(settings_.server_on && !assistant_mode_){
    gxui_text_shadow(400, 20, GXRGB(120,180,120), "%s", server_status_text().c_str());
  }

  if(gen_mutex_) gx_mutex_lock(gen_mutex_);
  std::string body = chat_log_,notice=chat_notice_;
  if(generating_){
    body += "Model: ";
    body += gen_text_;
  }
  if(gen_mutex_) gx_mutex_unlock(gen_mutex_);

  int max_lines = (int)((720.0f - (20.0f + lh*2) - lh*5) / lh);
  if(max_lines < 1) max_lines = 1;
  chat_view_.update(body,(int)(1180.0f/cw),max_lines);
  u32 p=gxui_buttons_pressed();
  if(!kb_mode_){
    if(p & GX_BTN_DUP) chat_view_.scroll(-1);
    if(p & GX_BTN_DDOWN) chat_view_.scroll(1);
    if(p & GX_BTN_LB) chat_view_.scroll(-max_lines);
    if(p & GX_BTN_RB) chat_view_.scroll(max_lines);
    if(p & GX_BTN_RTHUMB) chat_view_.latest();
  }
  const std::vector<std::string>& lines=chat_view_.lines();
  int start=chat_view_.top();
  gxui_text_shadow(40,20+lh,GXRGB(120,140,150),
    "UP/DOWN: scroll  LB/RB: page  R-stick: latest | %s",
    chat_view_.following() ? "following reply" : "reading history");
  f32 y = 20 + lh*2;
  for(int i = start; i < (int)lines.size() && i < start+max_lines; ++i){
    const std::string& L = lines[i];
    u32 col = GXRGB(200,200,210);
    if(L.compare(0,5,"You: ")==0) col = GXRGB(140,190,255);
    else if(L.compare(0,7,"Model: ")==0) col = GXRGB(180,220,160);
    else if(L.compare(0,11,"Assistant: ")==0) col = GXRGB(180,220,160);
    gxui_text_md(40, y, col, L.c_str());
    y += lh;
  }

  if(!notice.empty()) gxui_text_shadow(40,720-lh*4,GXRGB(180,200,150),"%s",notice.c_str());
  gxui_text_shadow(40,720-lh,GXRGB(120,130,140),assistant_mode_ ?
    "A: ask   X: new chat   START: save chat   B: models" : (settings_.chat_on ?
    "A: message   X: new chat   START: save chat   Y: stop   B: models" : "Monitoring API server   B: models"));
  if(generating_){
    if(gen_mutex_) gx_mutex_lock(gen_mutex_);
    f32 tps = gen_tps_;
    f32 lat = gen_last_ms_;
    int cnt = gen_count_;
    u32 done=gen_prompt_done_,total=gen_prompt_total_;
    u32 layer=gen_layer_done_,layers=gen_layer_total_;
    if(gen_mutex_) gx_mutex_unlock(gen_mutex_);
    if(cnt == 0){
      gxui_text_shadow(40,720-lh*2,GX_ACCENT,"Prompt: %u/%u tokens | layer %u/%u | %.0fs",done,total,layer,layers,gx_time_seconds()-gen_start_time_);
    } else {
      gxui_text_shadow(40,720-lh*2,GX_ACCENT,"Generating: %.1ft/s %.0fms | layer %u/%u",(double)tps,(double)lat,layer,layers);
    }
  } else {
    gxui_text_shadow(40, 720 - lh*2, GXRGB(90,90,100), "idle");
  }
  if(assistant_mode_) gxui_text_shadow(40,720-lh*3,GXRGB(140,160,140),"Offline keyword assistant | no GGUF | %u active timers",(unsigned)assistant_.timer_count());
  else if(!engine_.storage_ok()) gxui_text_shadow(40,720-lh*3,GXRGB(240,130,120),"Disk read failed. Check HDD/USB and reload the model.");
  else gxui_text_shadow(40,720-lh*3,GXRGB(140,160,140),"%s | %s",engine_.backend_note(),engine_.disk_streaming() ? "HDD/USB stream" : "RAM weights");

  if(kb_mode_){
    kb_draw();
    kb_input();
    return;
  }

  if(generating_ && (p & GX_BTN_Y)){
    gen_stop_ = true;
    chat_notice_="Stopping after the current operation...";
  } else if((settings_.chat_on || assistant_mode_) && !generating_){
    if(p & GX_BTN_X) new_chat();
    else if(p & GX_BTN_START) export_chat();
    else if(p & GX_BTN_A) kb_open(KB_CHAT,assistant_mode_ ? "360 Assistant" : (engine_.tok().completion_only() ? "Continue a story" : "Your message"),
      assistant_mode_ ? "Type help, a question or a command" : (engine_.tok().completion_only() ? "Write a story opening, e.g. Once upon a time" : "Type your message to the model"),"");
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
  assistant_mode_=true;model_name_="360 Assistant - keywords and intents";
  turns_.clear();chat_log_.clear();chat_notice_.clear();chat_view_=ChatView();assistant_.reset_conversation();
  append_chat("Assistant: ","Ready without loading a model. Type help for calculations, conversions, notes, timers and Xbox commands.");
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
  if(assistant_.dirty() && !assistant_.save(kAssistantPath)) chat_notice_="Notes/name are in memory only: could not save 360AI-assistant.json.";
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
static const f32 kKbCellW = 56;
static const f32 kKbCellH = 26;

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
  f32 lh = gxui_line_h(), cw = gxui_char_w();
  f32 pw = kKbCols * kKbCellW;
  f32 px = (1280 - pw) * 0.5f;
  f32 py = 170;
  if(kb_mode_ & KB_SYS){
    return;
  }
  f32 ph = lh * 4 + kKbRows * kKbCellH + 40;
  gxui_fill(px - 20, py - 20, pw + 40, ph, GXRGBA(14,16,28,244));
  gxui_text_shadow(px, py, GX_ACCENT, "%s", kb_title_.c_str());
  gxui_text_shadow(px, py + lh, GXRGB(180,180,190), "%s", kb_desc_.c_str());

  f32 by = py + lh * 2;
  gxui_fill(px, by, pw, lh, GXRGBA(36,44,72,235));
  int vis = (int)((pw - 12) / cw);
  std::string shown = kb_text_;
  if((int)shown.size() + 1 > vis){
    shown = shown.substr(shown.size() + 1 - (size_t)vis);
  }
  gxui_text_shadow(px + 6, by + 4, GXRGB(255,255,255), "%s_", shown.c_str());

  f32 gy = by + lh + 8;
  for(int i = 0; i < kKbCells; ++i){
    f32 cx = px + (i % kKbCols) * kKbCellW;
    f32 cy = gy + (i / kKbCols) * kKbCellH;
    bool sel = (i == kb_cur_);
    u32 bg = sel ? GX_ACCENT_A(235) : GXRGBA(30,34,50,225);
    gxui_fill(cx, cy, kKbCellW - 4, kKbCellH - 4, bg);
    u32 col = sel ? GXRGB(255,255,255) : GXRGB(170,175,190);
    if(i < kKbChars){
      char c = kKbSet[i];
      if(c == ' '){
        gxui_text_shadow(cx + 18, cy + 3, col, "_");
      } else {
        if(kb_upper_ && c >= 'a' && c <= 'z') c = (char)(c - 32);
        gxui_text_shadow(cx + 18, cy + 3, col, "%c", c);
      }
    } else if(i == kKbDel){
      gxui_text_shadow(cx + 2, cy + 3, col, "DEL");
    } else {
      gxui_text_shadow(cx + 10, cy + 3, GXRGB(160,220,160), "OK");
    }
  }
  gxui_text_shadow(px, gy + kKbRows * kKbCellH + 6, GXRGB(120,120,130),
                   "A: enter    B: cancel    LB: case    START: done");
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
      kb_mode_ = KB_NONE;
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
      if(kb_text_.size() < 512){
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
  if(mode == KB_CTX){
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
