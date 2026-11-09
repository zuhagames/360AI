#include "replies.h"
#include "assistant_store.h"
static void tap(App& app,u32 button){app.test_frame(0);app.test_frame(button);app.test_frame(0);}
static void dashboard_checks(){
  Assistant replies;AssistantContext context;context.now=100;
  size_t variants=0;
  for(size_t i=0;i<sizeof(gx_replies)/sizeof(gx_replies[0]);++i){std::string aliases=gx_replies[i].phrases;size_t start=0;
    for(;;){size_t end=aliases.find('|',start);std::string input=aliases.substr(start,end==std::string::npos ? end : end-start);
      AssistantReply result=replies.respond(input,context);assert(result.action==AA_NONE && !result.text.empty() && result.text.find("couldn't match")==std::string::npos);
      assert(result.text.find("{name}")==std::string::npos);++variants;if(end==std::string::npos) break;start=end+1;}}
  assert(contains(replies.respond("hello",context),"Hello, there"));
  replies.respond("my name is Martin",context);assert(contains(replies.respond("HELLO!",context),"Hello, Martin"));
  replies.respond("remember",context);assert(contains(replies.respond("can you help me",context),"Quick tools"));assert(replies.note_count()==0);
  replies.respond("remember original",context);replies.respond("hello delete all notes",context);assert(replies.note_count()==1);
  assert(contains(replies.respond("shelloworld",context),"couldn't match"));
  assert(contains(replies.respond("can you calculate 8 * 7",context),"Result: 56"));
  replies.respond("remember",context);assert(contains(replies.respond("give me five minutes",context),"set for 5m"));
  assert(contains(replies.respond("add another minute",context),"extended"));
  std::string literal(512,'x');assert(contains(replies.write_note(0,literal),"Saved note"));
  assert(contains(replies.write_note(2,"status?"),"status?"));assert(replies.note_count()==2);
  assert(contains(replies.write_note(2,std::string(513,'x')),"512"));

  const char* file="build/tests/assistant-queue.json";remove(file);remove("build/tests/assistant-queue.json.bak");
  Assistant saved;AssistantStore writer;assert(writer.start(file));
  saved.write_note(0,"first");std::string first=saved.snapshot();saved.write_note(0,"second");saved.saved_snapshot(first);assert(saved.dirty());
  for(int n=0;n<12;++n){saved.write_note(0,"queued note");assert(writer.submit(saved.snapshot()));}
  std::string latest=saved.snapshot();writer.stop();std::string completed;bool ok=false;
  assert(writer.poll(completed,ok) && ok && completed==latest);saved.saved_snapshot(completed);assert(!saved.dirty());
  assert(!writer.submit(latest));Assistant restored;assert(restored.load(file) && restored.note_count()==14);
  remove(file);remove("build/tests/assistant-queue.json.bak");
  AssistantStore failed;assert(failed.start("build/tests/no-such-assistant-dir-2-0/state.json"));assert(failed.submit(latest));failed.stop();assert(failed.poll(completed,ok) && !ok);

  assert(gxui_init());App app;app.test_seed();app.test_frame(0);assert(app.test_screen()==AS_HOME);
  tap(app,GX_BTN_A);assert(app.test_screen()==AS_CHAT);app.test_ask("delete all notes");tap(app,GX_BTN_B);assert(app.test_screen()==AS_HOME);
  tap(app,GX_BTN_A);app.test_ask("yes");assert(app.test_assistant().note_count()==2);tap(app,GX_BTN_B);
  tap(app,GX_BTN_Y);assert(app.test_screen()==AS_TOOLS);tap(app,GX_BTN_A);assert(app.test_screen()==AS_CHAT && app.test_keyboard());
  tap(app,GX_BTN_B);assert(app.test_screen()==AS_CHAT && !app.test_keyboard());tap(app,GX_BTN_B);
  tap(app,GX_BTN_DRIGHT);tap(app,GX_BTN_A);assert(app.test_screen()==AS_NOTES);
  tap(app,GX_BTN_X);assert(app.test_keyboard());tap(app,GX_BTN_A);tap(app,GX_BTN_START);assert(!app.test_keyboard() && app.test_assistant().note_count()==3);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_B);assert(app.test_screen()==AS_NOTES && app.test_assistant().note_count()==3);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_A);assert(app.test_assistant().note_count()==2);tap(app,GX_BTN_START);assert(app.test_assistant().note_count()==3);
  tap(app,GX_BTN_B);tap(app,GX_BTN_DLEFT);tap(app,GX_BTN_DDOWN);tap(app,GX_BTN_A);assert(app.test_screen()==AS_TIMERS);
  tap(app,GX_BTN_A);assert(app.test_assistant().timers()[0].paused);double left=app.test_assistant().timers()[0].left;
  tap(app,GX_BTN_RB);assert(app.test_assistant().timers()[0].left==left+60);
  tap(app,GX_BTN_LB);assert(!app.test_assistant().timers()[0].paused);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_A);assert(app.test_assistant().timer_count()==1);
  tap(app,GX_BTN_B);tap(app,GX_BTN_DRIGHT);tap(app,GX_BTN_A);assert(app.test_screen()==AS_BROWSE);
  tap(app,GX_BTN_A);assert(app.test_screen()==AS_SETTINGS);tap(app,GX_BTN_B);assert(app.test_screen()==AS_BROWSE);tap(app,GX_BTN_B);assert(app.test_screen()==AS_HOME);
  gxui_shutdown();std::cout<<"2.0: "<<variants<<" fixed reply variants, literal notes, background persistence and native controller navigation passed\n";
}
