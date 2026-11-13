static void finder_checks(){
  std::vector<size_t> rows,again;assert(Assistant::find_starters("",rows) && rows.size()==250);
  for(size_t i=0;i<250;++i){assert(rows[i]==i);assert(Assistant::find_starters(Assistant::starter_phrase(i),again) && !again.empty() && again[0]==i);}
  const char* added_categories[]={"Gaming ideas","Tech basics","Creative prompts","Planning","Conversation"};
  for(size_t group=0;group<5;++group){assert(Assistant::find_starters(added_categories[group],again));
    for(size_t i=150+group*20;i<170+group*20;++i) assert(std::find(again.begin(),again.end(),i)!=again.end());}
  assert(Assistant::find_starters("COMFORTABLE?!",rows) && rows.size()==1 && rows[0]==96);
  assert(Assistant::find_starters("tea",rows) && std::find(rows.begin(),rows.end(),30)!=rows.end());
  assert(Assistant::find_starters("cancel timer",rows) && !rows.empty());assert(Assistant::find_starters("TIMER, cancel",again) && rows==again);
  assert(Assistant::find_starters("noteb",rows) && rows.empty());assert(Assistant::find_starters("zzzxnonexistent",rows) && rows.empty());
  assert(Assistant::find_starters("comfortable",rows));std::vector<size_t> previous=rows;
  const std::string invalid[]={std::string(97,'x'),"bad\nquery",std::string("bad\xff",4),"a b c d e f g h i j k l m n o p q"};
  for(int i=0;i<4;++i){assert(!Assistant::find_starters(invalid[i],rows) && rows==previous);}
  assert(Assistant::find_starters("note note note note note note note note note note note note note note note note note",rows));
  Assistant commands;AssistantContext context;commands.respond("remember",context);AssistantReply action=commands.respond("find answers comfortable",context);assert(action.action==AA_FIND_ANSWERS && action.argument=="comfortable" && commands.note_count()==0);commands.respond("yes",context);assert(commands.note_count()==0);
  assert(commands.respond("don't find answers notes",context).action==AA_NONE);assert(commands.respond("find answers "+std::string(97,'x'),context).action==AA_NONE);
  assert(gxui_init());App app;app.test_seed();app.test_ask("open saved answers");app.test_frame(0);std::string before=app.test_assistant().snapshot();
  tap(app,GX_BTN_START);assert(app.test_keyboard());app.test_keyboard_text("comfortable");assert(app.test_starter_matches()==1 && app.test_starter_index()==96 && app.test_assistant().snapshot()==before);
  tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_CHAT && !app.test_keyboard() && app.test_chat_text().find(Assistant::starter_response(96))!=std::string::npos);tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS && app.test_starter_matches()==1 && app.test_starter_index()==96);
  tap(app,GX_BTN_A);app.test_keyboard_text("My comfortable reply");assert(app.test_assistant().answers().size()==1 && app.test_assistant().answers()[0].phrase==Assistant::starter_phrase(96));tap(app,GX_BTN_LTHUMB);tap(app,GX_BTN_RTHUMB);assert(app.test_chat_text().find("My comfortable reply")!=std::string::npos);tap(app,GX_BTN_B);
  tap(app,GX_BTN_START);tap(app,GX_BTN_B);assert(app.test_starter_matches()==1 && app.test_starter_index()==96);
  tap(app,GX_BTN_START);app.test_keyboard_text(std::string(97,'x'));assert(app.test_starter_matches()==1 && app.test_starter_index()==96);
  tap(app,GX_BTN_START);app.test_keyboard_text("zzzxnonexistent");assert(app.test_starter_matches()==0);before=app.test_assistant().snapshot();tap(app,GX_BTN_A|GX_BTN_DDOWN);tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_ANSWERS && !app.test_keyboard() && app.test_assistant().snapshot()==before);
  tap(app,GX_BTN_Y);assert(app.test_starter_matches()==250 && app.test_starter_index()==0);
  tap(app,GX_BTN_START);app.test_keyboard_text("model");assert(app.test_starter_matches()>6);tap(app,GX_BTN_DRIGHT);assert(app.test_starter_selection()==6);tap(app,GX_BTN_DLEFT);assert(app.test_starter_selection()==0);
  tap(app,GX_BTN_START);app.test_keyboard_text("");assert(app.test_starter_matches()==250);tap(app,GX_BTN_RTHUMB);tap(app,GX_BTN_B);tap(app,GX_BTN_B);assert(app.test_screen()==AS_TOOLS);tap(app,GX_BTN_B);tap(app,GX_BTN_A);assert(app.test_screen()==AS_CHAT);tap(app,GX_BTN_B);assert(app.test_screen()==AS_HOME);
  app.test_ask("search answers comfortable");assert(app.test_screen()==AS_ANSWERS && app.test_starter_mode() && app.test_starter_matches()==1 && app.test_starter_index()==96);gxui_shutdown();
  std::cout<<"2.7 answer finder: all exact ranks, whole words, multiword filters, bounded rejection, filtered customization, empty results, direct asking and return navigation passed\n";
}
