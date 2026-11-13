#ifndef GEMMA_XEX_APP_H
#define GEMMA_XEX_APP_H

#include "common.h"
#include "platform.h"
#include "engine.h"
#include "chat.h"
#include "assistant.h"
#include "assistant_store.h"
#include "updater.h"
#include "hub.h"

enum app_screen{
  AS_BROWSE=0,
  AS_SETTINGS,
  AS_LOADING,
  AS_CHAT,
  AS_HOME,
  AS_NOTES,
  AS_TIMERS,
  AS_TOOLS,
  AS_UPDATES,
  AS_HUB,
  AS_ANSWERS
};

enum debug_cycle{
  DC_ALL=0, DC_WARN, DC_ERROR, DC_NONE, DC_COUNT
};

enum kb_kind{
  KB_NONE=0, KB_CTX, KB_TEMP, KB_TOPK, KB_CHAT, KB_REPLY, KB_TOPP, KB_REPEAT, KB_NOTE_NEW, KB_NOTE_EDIT, KB_TIMER_NEW, KB_HUB_SEARCH, KB_ANSWER_PHRASE, KB_ANSWER_TEXT, KB_ANSWER_TEST, KB_ANSWER_SEARCH, KB_SYS=0x100
};

struct AppSettings{
  int ctx_size;
  f32 temperature;
  f32 top_p,repeat_penalty;
  int topk;
  int debug_level;
  bool server_on;
  bool chat_on;
  bool auto_update;
  bool models_smallest_first;
  int backend_mode; // Auto / CPU / forced GPU
  int weight_mode; // Auto / RAM / HDD stream
  int reply_tokens;
  std::string last_model;
  HubQuery hub_query;

  AppSettings();
  bool load(const char* path);
  bool save(const char* path) const;
  void apply_profile(int profile);
  int profile() const;
};

class App{
public:
  App();
  ~App();

  int run();
#ifndef _XBOX
  void test_frame(u32 buttons);
  app_screen test_screen() const {return screen_;}
  void test_seed();
  bool test_capture(const char* screen,const char* path);
  const Assistant& test_assistant() const {return assistant_;}
  bool test_keyboard() const {return kb_mode_!=KB_NONE;}
  void test_ask(const char* text) {assistant_send(text);}
  void test_hub_data(const HubView& data){hub_.test_view(data);screen_=AS_HUB;hub_files_=true;hub_file_sel_=0;hub_confirm_=false;}
  bool test_hub_confirmation() const {return hub_confirm_;}
  std::string test_model_name(int i) const {return files_[i].name;}
  int test_model_selection() const {return file_sel_;}
  void test_add_model(const char* name,u64 bytes){gx_file_entry f;f.name=name;f.size=bytes;files_.push_back(f);}
  void test_keyboard_text(const std::string& text){kb_text_=text;kb_commit();}
  const std::string& test_answer_preview() const {return answer_preview_;}
  bool test_starter_mode() const {return answer_starters_;}
  int test_starter_selection() const {return starter_sel_;}
  size_t test_starter_matches() const {return starter_rows_.size();}
  size_t test_starter_index() const {return selected_starter();}
  const std::string& test_chat_text() const {return chat_log_;}
#endif

private:
  app_screen screen_;
  AppSettings settings_;
  std::string model_path_;
  std::string model_name_;

  std::vector<gx_file_entry> files_;
  int file_sel_;
  int file_scroll_;

  int set_sel_;
  int home_sel_,tool_sel_,note_sel_,timer_sel_,confirm_id_,edit_id_;
  int answer_sel_;
  int starter_sel_;
  bool answer_starters_;
  bool answer_chat_return_;
  std::vector<size_t> starter_rows_;
  std::string starter_query_;
  std::string answer_draft_,answer_confirm_,answer_view_key_,answer_preview_;
  ChatView answer_view_;

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
  bool assistant_mode_;
  Assistant assistant_;
  AssistantStore assistant_store_;
  Updater updater_;
  ModelHub hub_;
  HubQuery hub_query_;
  HubView hub_view_;
  int hub_sel_,hub_file_sel_;
  bool hub_files_,hub_confirm_;
  bool update_auto_checked_;
  bool update_restart_;
  double update_auto_at_;
  std::string save_status_;
  std::string timer_notice_;
  double timer_notice_until_;

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
  u32 gen_layer_done_,gen_layer_total_;
  int gen_count_;
  f32 gen_last_ms_;
  f32 gen_tps_;
  gx_mutex_t gen_mutex_;
  gx_mutex_t eng_mutex_;
  gx_thread_t gen_thread_, load_thread_;
  std::vector<std::pair<int,std::string> > turns_;
  std::string chat_log_;
  u64 chat_revision_,view_revision_;
  int view_tokens_;
  bool view_generating_;
  std::string user_input_;
  std::string chat_notice_;
  ChatView chat_view_;
  ChatView note_view_;
  int note_view_id_;

  void browse_frame();
  void refresh_models();
  void sort_models();
  void hub_frame();
  void open_hub();
  void frame();
  void home_frame();
  void notes_frame();
  void timers_frame();
  void tools_frame();
  void answers_frame();
  void open_answers();
  size_t selected_starter() const;
  bool search_answers(const std::string& query);
  void updates_frame();
  void restart_for_update();
  void shell(const char* title,const char* subtitle);
  void home();
  void local_command(const std::string& command);
  void queue_assistant_save();
  void poll_assistant_save();
  void settings_frame();
  void loading_frame();
  void chat_frame();
  void save_settings();
  void new_chat();
  void export_chat();
  void open_assistant();
  void assistant_send(const std::string& text);
  void assistant_poll();
  AssistantContext assistant_context() const;

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
  static void on_layer(u32 done,u32 total,void* user);

  void append_chat(const std::string& role, const std::string& text);
};

#endif
