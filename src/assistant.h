#ifndef GX_ASSISTANT_H
#define GX_ASSISTANT_H
#include "common.h"

enum AssistantAction { AA_NONE, AA_PROFILE, AA_EXPORT, AA_CLEAR_CHAT, AA_MODELS };
struct AssistantReply {
  std::string text;
  AssistantAction action;
  int value;
  AssistantReply(const std::string& t="",AssistantAction a=AA_NONE,int v=0):text(t),action(a),value(v){}
};
struct AssistantContext {
  double now;
  size_t free_bytes;
  std::string version,clock,date;
  std::vector<std::string> models;
  int profile;
  AssistantContext():now(0),free_bytes(0),profile(0){}
};

// Bounded, deterministic text intents. No model inference, code execution,
// network access or speech recognition. App actions are returned to the caller.
class Assistant {
public:
  Assistant();
  AssistantReply respond(const std::string& text,const AssistantContext& context);
  void poll(double now,std::vector<std::string>& notifications);
  void reset_conversation();
  bool load(const char* path);
  bool save(const char* path);
  bool load_rules(const char* path);
  bool dirty() const { return dirty_; }
  size_t timer_count() const { return timers_.size(); }
  size_t note_count() const { return notes_.size(); }
private:
  struct Note { int id;std::string text; };
  struct Timer { int id;double due,duration,left;bool paused;std::string label; };
  struct Answer { std::string phrase,response; };
  struct Rule { std::vector<std::string> phrases;std::string response; };
  enum Pending { P_NONE,P_TIMER,P_NOTE,P_CALC,P_NAME,P_CLEAR_NOTES,P_CLEAR_CHAT,P_TIMER_CANCEL,P_TIMER_SELECT,P_NOTE_SEARCH,P_NOTE_EDIT };
  enum TimerAction { TA_STATUS,TA_CANCEL,TA_PAUSE,TA_RESUME,TA_RESTART,TA_EXTEND };
  std::vector<Note> notes_;
  std::vector<Timer> timers_;
  std::vector<std::string> timer_history_;
  std::vector<Rule> rules_;
  std::vector<Answer> answers_;
  std::string name_,last_from_,last_to_,pending_label_;
  double answer_;
  bool has_answer_,dirty_;
  int next_note_,next_timer_,joke_;
  int last_timer_,edit_note_;
  TimerAction pending_timer_action_;
  double pending_extra_;
  std::string undo_state_;
  Pending pending_;
  AssistantReply add_timer(const std::string& text,const AssistantContext& context);
  AssistantReply add_note(const std::string& text);
  AssistantReply calculate(const std::string& text);
  AssistantReply convert(const std::string& text,bool followup);
  AssistantReply timer_action(const std::string& selector,TimerAction action,const AssistantContext& context,double extra=0);
  AssistantReply search_notes(const std::string& query);
  AssistantReply edit_note(int id,const std::string& text);
  AssistantReply teach(const std::string& text);
  std::string state_text() const;
  void remember_undo();
  bool parse_state(const std::string& text);
};
#endif
