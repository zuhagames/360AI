#ifndef GEMMA_XEX_APP_H
#define GEMMA_XEX_APP_H

#include "common.h"
#include "platform.h"
#include "engine.h"
#include "chat.h"

enum app_screen{
  AS_BROWSE=0,
  AS_SETTINGS,
  AS_LOADING,
  AS_CHAT
};

enum debug_cycle{
  DC_ALL=0, DC_WARN, DC_ERROR, DC_NONE, DC_COUNT
};

enum kb_kind{
  KB_NONE=0, KB_CTX, KB_TEMP, KB_TOPK, KB_CHAT, KB_REPLY, KB_SYS=0x100
};

struct AppSettings{
  int ctx_size;
  f32 temperature;
  int topk;
  int debug_level;
  bool server_on;
  bool chat_on;
  int backend_mode; // Auto / CPU / forced GPU
  int weight_mode; // Auto / RAM / HDD stream
  int reply_tokens;
  std::string last_model;

  AppSettings();
  bool load(const char* path);
  bool save(const char* path) const;
};

class App{
public:
  App();
  ~App();

  int run();

private:
  app_screen screen_;
  AppSettings settings_;
  std::string model_path_;
  std::string model_name_;

  std::vector<gx_file_entry> files_;
  int file_sel_;
  int file_scroll_;

  int set_sel_;

  int kb_mode_;
  int kb_cur_;
  bool kb_upper_;
  bool kb_swallow_;
  std::string kb_text_;
  std::string kb_title_;
  std::string kb_desc_;

  Engine engine_;
  KVCache kv_;
  bool model_loaded_;

  volatile bool loading_;
  volatile bool load_ok_;
  volatile bool load_done_;
  char load_label_[128];
  volatile int load_cur_;
  volatile int load_total_;

  volatile bool generating_;
  volatile bool gen_stop_;
  std::string gen_text_;
  double gen_start_time_;
  double gen_last_time_;
  double gen_first_time_;
  u32 gen_prompt_done_,gen_prompt_total_;
  int gen_count_;
  f32 gen_last_ms_;
  f32 gen_tps_;
  gx_mutex_t gen_mutex_;
  gx_mutex_t eng_mutex_;
  gx_thread_t gen_thread_, load_thread_;
  std::vector<std::pair<int,std::string> > turns_;
  std::string chat_log_;
  std::string user_input_;
  std::string chat_notice_;
  ChatView chat_view_;

  void browse_frame();
  void settings_frame();
  void loading_frame();
  void chat_frame();
  void save_settings();
  void new_chat();
  void export_chat();

  void kb_open(int mode, const char* title, const char* desc, const std::string& initial);
  void kb_draw();
  void kb_input();
  void kb_commit();

  void start_load();
  static void load_thread(void* arg);
  static void load_progress_cb(const char* name, int cur, int total);
  static App* s_load_app;
  void do_load();

  void start_generate(const std::string& prompt);
  static void gen_thread(void* arg);
  void do_generate(const std::vector<int>& prompt);
  static void on_token(int id, const char* text, void* user);
  static void on_prefill(u32 done,u32 total,void* user);

  void append_chat(const std::string& role, const std::string& text);
};

#endif
