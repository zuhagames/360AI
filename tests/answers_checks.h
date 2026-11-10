#include "assistant.h"
static void answers_checks(){
  Assistant a;AssistantContext c;std::string result,key,response;
  assert(a.write_answer(" Favorite GAME! ","Halo 3",result));assert(a.answers().size()==1 && a.answers()[0].phrase=="favorite game");
  assert(a.write_answer("favorite game","Halo 3\nCo-op on Friday",result));assert(a.answers().size()==1);
  assert(a.preview_answer("What is your FAVORITE GAME?",key,response) && key=="favorite game" && response=="Halo 3\nCo-op on Friday");
  assert(!a.preview_answer("favorite gameplay",key,response));
  assert(a.write_answer("game","shorter",result));assert(a.preview_answer("favorite game",key,response) && key=="favorite game");
  std::string before=a.snapshot();const std::string invalid[]={"", "ab", "!!!",std::string(81,'x'),"bad\nphrase",std::string("bad\xff",4)};
  for(int i=0;i<6;++i){assert(!a.write_answer(invalid[i],"text",result));assert(a.snapshot()==before);}
  assert(!a.write_answer("valid phrase",std::string(513,'x'),result));assert(!a.write_answer("valid phrase","bad\x01text",result));assert(a.snapshot()==before);
  assert(a.write_answer("clear chat","delete all notes",result));before=a.snapshot();
  assert(a.preview_answer("clear chat",key,response) && response=="delete all notes" && a.snapshot()==before);
  assert(a.respond("clear chat",c).text.find("Clear this chat")!=std::string::npos);a.cancel_followup();
  assert(a.respond("remember",c).action==AA_NONE);assert(a.respond("open saved answers",c).action==AA_ANSWERS);a.respond("yes",c);assert(a.note_count()==0);
  assert(a.remove_answer("Favorite GAME!",result) && a.answers().size()==2);assert(contains(a.respond("undo",c),"Undid") && a.answers().size()==3);
  before=a.snapshot();assert(!a.remove_answer("unknown phrase",result) && a.snapshot()==before);
  for(int i=3;i<16;++i){char phrase[40];snprintf(phrase,sizeof(phrase),"answer %d",i);assert(a.write_answer(phrase,"text",result));}
  assert(a.answers().size()==16 && !a.write_answer("seventeenth answer","text",result));assert(a.write_answer("game","updated at capacity",result));
  const char* path="build/tests/answers-2.4.json";remove(path);remove("build/tests/answers-2.4.json.bak");assert(a.save(path));Assistant restored;assert(restored.load(path) && restored.snapshot()==a.snapshot());
  assert(restored.preview_answer("favorite game",key,response) && response=="Halo 3\nCo-op on Friday");remove(path);remove("build/tests/answers-2.4.json.bak");
  assert(gxui_init());App app;app.test_seed();app.test_ask("open saved answers");app.test_frame(0);assert(app.test_screen()==AS_ANSWERS);
  tap(app,GX_BTN_X);assert(app.test_keyboard());app.test_keyboard_text("favorite game");assert(app.test_keyboard() && app.test_assistant().answers().empty());
  tap(app,GX_BTN_B);assert(!app.test_keyboard() && app.test_screen()==AS_ANSWERS && app.test_assistant().answers().empty());
  tap(app,GX_BTN_X);app.test_keyboard_text("favorite game");app.test_keyboard_text("Halo 3");assert(!app.test_keyboard() && app.test_assistant().answers().size()==1);
  tap(app,GX_BTN_X);app.test_keyboard_text("Favorite GAME!");assert(!app.test_keyboard() && app.test_assistant().answers().size()==1);
  tap(app,GX_BTN_A);app.test_keyboard_text("clear chat\nLiteral text only");assert(app.test_assistant().answers()[0].response=="clear chat\nLiteral text only");
  std::string unchanged=app.test_assistant().snapshot();tap(app,GX_BTN_RTHUMB);app.test_keyboard_text("what is your favorite game");assert(app.test_answer_preview().find("Literal text only")!=std::string::npos && app.test_assistant().snapshot()==unchanged);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS && app.test_assistant().answers().size()==1);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_A);assert(app.test_assistant().answers().empty());tap(app,GX_BTN_START);assert(app.test_assistant().answers().size()==1);
  tap(app,GX_BTN_A);tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS && app.test_assistant().answers().size()==1);
  tap(app,GX_BTN_B);assert(app.test_screen()==AS_TOOLS);tap(app,GX_BTN_B);assert(app.test_screen()==AS_HOME);
  // Quick tools reaches the same screen without a text command.
  tap(app,GX_BTN_Y);tap(app,GX_BTN_DDOWN);tap(app,GX_BTN_DDOWN);tap(app,GX_BTN_A);assert(app.test_screen()==AS_ANSWERS);gxui_shutdown();
  std::cout<<"2.4 saved answers: normalization, literal text, longest match, command isolation, limits, undo and persistence passed\n";
}
