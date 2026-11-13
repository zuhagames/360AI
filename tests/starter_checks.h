#include <map>
static void starter_checks(){
  assert(Assistant::starter_count()==250);Assistant a;AssistantContext context;std::string before=a.snapshot(),key,response;
  std::map<std::string,int> categories,phrases;
  for(size_t i=0;i<sizeof(gx_replies)/sizeof(gx_replies[0]);++i){std::string aliases=gx_replies[i].phrases;size_t at=0;for(;;){size_t end=aliases.find('|',at);phrases[aliases.substr(at,end==std::string::npos ? end : end-at)]=1;if(end==std::string::npos) break;at=end+1;}}
  for(size_t i=0;i<250;++i){std::string phrase=Assistant::starter_phrase(i),reply=Assistant::starter_response(i);assert(Assistant::answer_phrase(phrase,key) && key==phrase && !phrases[phrase]);phrases[phrase]=1;
    assert(!reply.empty() && reply.size()<=512 && reply.find("{name}")==std::string::npos);++categories[Assistant::starter_category(i)];
    AssistantReply actual=a.respond(phrase,context);assert(actual.action==AA_NONE && actual.text==reply);
    std::string uppercase=phrase;for(size_t j=0;j<uppercase.size();++j) if(uppercase[j]>='a' && uppercase[j]<='z') uppercase[j]-=32;
    assert(a.respond(uppercase+"?!",context).text==reply);
    assert(a.respond("unrelated "+phrase+" unrelated",context).text!=reply);
    assert(a.preview_answer(phrase,key,response,true) && key==phrase && response==reply);
    assert(!a.preview_answer(phrase,key,response));
  }
  assert(categories.size()==15);size_t new_categories=0;
  for(std::map<std::string,int>::const_iterator i=categories.begin();i!=categories.end();++i){
    bool added=i->first=="Gaming ideas" || i->first=="Tech basics" || i->first=="Creative prompts" || i->first=="Planning" || i->first=="Conversation";
    assert(i->second==(added ? 20 : 15));if(added) ++new_categories;
  }assert(new_categories==5);
  const size_t starts[]={0,15,30,45,60,75,90,105,120,135,150,170,190,210,230};
  for(size_t group=0;group<15;++group){size_t end=group+1<15 ? starts[group+1] : 250;
    for(size_t i=starts[group];i<end;++i){assert(Assistant::starter_category_step(i,-1)==(group ? starts[group-1] : i));
      assert(Assistant::starter_category_step(i,1)==(group+1<15 ? starts[group+1] : i));assert(Assistant::starter_category_step(i,0)==i);}
  }
  assert(Assistant::starter_category_step(250,1)==250 && Assistant::starter_category_step(999,-1)==250);
  for(size_t i=150;i<250;++i){Assistant pending;pending.respond("remember",context);assert(pending.respond(Assistant::starter_phrase(i),context).text==Assistant::starter_response(i));pending.respond("yes",context);assert(pending.note_count()==0 && pending.answers().empty());}
  assert(a.snapshot()==before && a.note_count()==0 && a.timer_count()==0 && a.answers().empty());assert(!strcmp(Assistant::starter_phrase(250),"") && !strcmp(Assistant::starter_response(999),""));
  std::string phrase=Assistant::starter_phrase(0),result;a.respond("remember",context);assert(a.respond(phrase,context).text==Assistant::starter_response(0));a.respond("yes",context);assert(a.note_count()==0);
  assert(a.write_answer(phrase,"My customized reply",result));assert(a.respond(phrase,context).text=="My customized reply");assert(a.preview_answer(phrase,key,response,true) && response=="My customized reply");
  assert(a.remove_answer(phrase,result) && a.respond(phrase,context).text==Assistant::starter_response(0));
  assert(gxui_init());App app;app.test_seed();app.test_ask("open saved answers");app.test_frame(0);assert(app.test_starter_mode());
  tap(app,GX_BTN_DUP|GX_BTN_DLEFT);assert(app.test_starter_index()==0);
  tap(app,GX_BTN_DRIGHT);assert(app.test_starter_selection()==15);tap(app,GX_BTN_DLEFT);assert(app.test_starter_selection()==0);
  for(size_t group=1;group<15;++group){tap(app,GX_BTN_DRIGHT);assert(app.test_starter_index()==starts[group]);}
  for(int i=0;i<19;++i) tap(app,GX_BTN_DDOWN);assert(app.test_starter_index()==249);tap(app,GX_BTN_DRIGHT);assert(app.test_starter_index()==249);
  tap(app,GX_BTN_DDOWN|GX_BTN_DRIGHT);assert(app.test_starter_index()==249);
  before=app.test_assistant().snapshot();tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_CHAT && app.test_chat_text().find(Assistant::starter_response(249))!=std::string::npos && app.test_assistant().snapshot()==before);tap(app,GX_BTN_B);assert(app.test_starter_index()==249);
  for(int group=13;group>=0;--group){tap(app,GX_BTN_DLEFT);assert(app.test_starter_index()==starts[group]);}
  before=app.test_assistant().snapshot();tap(app,GX_BTN_A);assert(app.test_keyboard());tap(app,GX_BTN_B);assert(app.test_starter_mode() && app.test_assistant().snapshot()==before);
  tap(app,GX_BTN_RTHUMB);assert(app.test_screen()==AS_CHAT && !app.test_keyboard() && app.test_chat_text().find(Assistant::starter_response(0))!=std::string::npos && app.test_assistant().snapshot()==before);tap(app,GX_BTN_B);assert(app.test_screen()==AS_ANSWERS);
  tap(app,GX_BTN_A);app.test_keyboard_text("My controller reply");assert(!app.test_starter_mode() && app.test_assistant().answers().size()==1 && app.test_assistant().answers()[0].phrase==phrase);
  tap(app,GX_BTN_LTHUMB);assert(app.test_starter_mode());tap(app,GX_BTN_A);tap(app,GX_BTN_B);assert(app.test_assistant().answers()[0].response=="My controller reply");
  tap(app,GX_BTN_Y);assert(!app.test_starter_mode());tap(app,GX_BTN_Y);tap(app,GX_BTN_A);assert(app.test_assistant().answers().empty());tap(app,GX_BTN_LTHUMB);tap(app,GX_BTN_RTHUMB);assert(app.test_chat_text().find(Assistant::starter_response(0))!=std::string::npos);tap(app,GX_BTN_B);
  tap(app,GX_BTN_Y);tap(app,GX_BTN_START);assert(app.test_assistant().answers().size()==1);tap(app,GX_BTN_LTHUMB);
  for(int i=1;i<16;++i){char command[64];snprintf(command,sizeof(command),"teach personal answer %d: text",i);app.test_ask(command);}assert(app.test_assistant().answers().size()==16);
  tap(app,GX_BTN_DRIGHT);tap(app,GX_BTN_A);assert(!app.test_keyboard());tap(app,GX_BTN_DLEFT);tap(app,GX_BTN_A);assert(app.test_keyboard());tap(app,GX_BTN_B);gxui_shutdown();
  std::cout<<"2.7 starters: exactly 250 unique answers, 15 variable-size categories, punctuation/case, exact matching, zero actions, overrides, controller browsing and capacity isolation passed\n";
}
