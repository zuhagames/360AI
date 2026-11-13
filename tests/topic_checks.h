static void pick_answer_topic(App& app,int row){
  tap(app,GX_BTN_BACK);assert(app.test_screen()==AS_ANSWER_TOPICS);
  while(app.test_topic_selection()<row) tap(app,GX_BTN_DDOWN);
  while(app.test_topic_selection()>row) tap(app,GX_BTN_DUP);
  tap(app,GX_BTN_A);assert(app.test_screen()==AS_ANSWERS && app.test_starter_mode());
}
static void topic_checks(){
  assert(gxui_init());App app;app.test_seed();app.test_ask("open saved answers");app.test_frame(0);
  std::string before=app.test_assistant().snapshot();
  tap(app,GX_BTN_BACK);assert(app.test_screen()==AS_ANSWER_TOPICS);tap(app,GX_BTN_DUP|GX_BTN_DLEFT);assert(app.test_topic_selection()==0);
  tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS && app.test_starter_index()==0);
  pick_answer_topic(app,1);assert(app.test_starter_matches()==0 && app.test_recent_count()==0);
  tap(app,GX_BTN_DUP|GX_BTN_DLEFT|GX_BTN_A);tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_ANSWERS && !app.test_keyboard() && app.test_assistant().snapshot()==before);
  tap(app,GX_BTN_Y);assert(app.test_starter_matches()==250);
  for(int group=0;group<15;++group){pick_answer_topic(app,group+2);size_t start=group<10 ? group*15 : 150+(group-10)*20,count=group<10 ? 15 : 20;
    assert(app.test_starter_matches()==count && app.test_starter_index()==start);tap(app,GX_BTN_DUP|GX_BTN_DLEFT);assert(app.test_starter_index()==start);
    tap(app,GX_BTN_DDOWN|GX_BTN_DRIGHT);assert(app.test_starter_index()==start+7);
    for(int n=0;n<4;++n) tap(app,GX_BTN_DRIGHT);assert(app.test_starter_index()==start+count-1);
    tap(app,GX_BTN_DDOWN|GX_BTN_DRIGHT);assert(app.test_starter_index()==start+count-1);
    tap(app,GX_BTN_BACK);tap(app,GX_BTN_B);assert(app.test_starter_index()==start+count-1 && app.test_starter_matches()==count);
    tap(app,GX_BTN_START);tap(app,GX_BTN_B);assert(app.test_starter_index()==start+count-1 && app.test_starter_matches()==count);
  }
  tap(app,GX_BTN_BACK);tap(app,GX_BTN_DDOWN|GX_BTN_DRIGHT);assert(app.test_topic_selection()==16);tap(app,GX_BTN_BACK);assert(app.test_screen()==AS_ANSWERS);
  tap(app,GX_BTN_START);app.test_keyboard_text(std::string(97,'x'));assert(app.test_starter_matches()==20);
  tap(app,GX_BTN_START);app.test_keyboard_text("comfortable");assert(app.test_starter_matches()==1 && app.test_starter_index()==96);
  tap(app,GX_BTN_BACK);tap(app,GX_BTN_B);assert(app.test_starter_index()==96 && app.test_starter_matches()==1);
  pick_answer_topic(app,0);assert(app.test_starter_matches()==250 && app.test_starter_index()==0);
  for(size_t i=0;i<14;++i){assert(app.test_starter_index()==i);tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_CHAT);tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS && app.test_starter_index()==i);if(i<13) tap(app,GX_BTN_DDOWN);}
  assert(app.test_recent_count()==12 && app.test_assistant().snapshot()==before);
  pick_answer_topic(app,1);assert(app.test_starter_matches()==12 && app.test_starter_index()==13);
  tap(app,GX_BTN_DDOWN);assert(app.test_starter_index()==12);tap(app,GX_BTN_RTHUMB);tap(app,GX_BTN_B);assert(app.test_starter_index()==12 && app.test_starter_selection()==1);
  pick_answer_topic(app,1);assert(app.test_starter_index()==12);tap(app,GX_BTN_DDOWN);assert(app.test_starter_index()==13);tap(app,GX_BTN_DRIGHT);assert(app.test_starter_selection()==7);
  for(int i=0;i<3;++i) tap(app,GX_BTN_DRIGHT);assert(app.test_starter_selection()==11 && app.test_starter_index()==2);
  pick_answer_topic(app,1);tap(app,GX_BTN_A);app.test_keyboard_text("My recent override");assert(!app.test_starter_mode() && app.test_assistant().answers().size()==1 && app.test_assistant().answers()[0].phrase==Assistant::starter_phrase(12));
  tap(app,GX_BTN_LTHUMB);tap(app,GX_BTN_RTHUMB);assert(app.test_chat_text().find("My recent override")!=std::string::npos);tap(app,GX_BTN_B);assert(app.test_recent_count()==12);
  before=app.test_assistant().snapshot();tap(app,GX_BTN_BACK);tap(app,GX_BTN_X);assert(app.test_recent_count()==0);tap(app,GX_BTN_B);
  assert(app.test_starter_matches()==0 && app.test_assistant().snapshot()==before && app.test_chat_text().find("My recent override")!=std::string::npos);
  tap(app,GX_BTN_Y);assert(app.test_starter_matches()==250);tap(app,GX_BTN_LTHUMB);tap(app,GX_BTN_BACK);assert(app.test_screen()==AS_TOOLS);
  {App fresh;assert(fresh.test_recent_count()==0);}
  gxui_shutdown();std::cout<<"2.8 topics: all 15 groups, empty recents, bounded deduplicated history, scoped paging, cancellation, overrides and back/diagonal input passed\n";
}
