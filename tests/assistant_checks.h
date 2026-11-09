#include "assistant.h"

static AssistantReply ask(Assistant& a,const char* text,AssistantContext& c){return a.respond(text,c);}
static bool contains(const AssistantReply& r,const char* text){return r.text.find(text)!=std::string::npos;}
static void assistant_followup_checks(){
  AssistantContext c;c.now=100;c.clock="12:34:56";c.date="2026-11-08";c.version="test";
  const char* interrupts[]={"status?","time?","date?","notes","timers","timer history","recent alerts","version","free memory","system status","available models","hello","tell me a joke"};
  const char* prompts[]={"remember","edit note 1","calculate","search notes","set timer","my name is"};
  for(size_t p=0;p<sizeof(prompts)/sizeof(prompts[0]);++p) for(size_t i=0;i<sizeof(interrupts)/sizeof(interrupts[0]);++i){
    Assistant a;ask(a,"remember original content",c);ask(a,prompts[p],c);AssistantReply r=ask(a,interrupts[i],c);
    assert(r.action==AA_NONE && a.note_count()==1 && a.timer_count()==0);
    assert(!contains(r,"Saved note ") && !contains(r,"Calculation:") && !contains(r,"What should") && !contains(r,"For how long"));
    assert(contains(ask(a,"read note 1",c),"original content"));assert(contains(ask(a,"my name",c),"haven't told"));
  }
  Assistant a;ask(a,"remember original",c);ask(a,"remember",c);assert(ask(a,"model list",c).action==AA_MODELS);assert(a.note_count()==1);
  ask(a,"remember",c);assert(ask(a,"save conversation",c).action==AA_EXPORT);assert(a.note_count()==1);
  ask(a,"remember",c);assert(contains(ask(a,"take a note replacement",c),"note 2: replacement"));
  ask(a,"edit note 1: status?",c);assert(contains(ask(a,"read note 1",c),"status?")); // Explicit payload stays literal.
  const char* labels[]={"the tea","my tea","study timer","timer tea","the timer timer","the","my"};
  for(size_t i=0;i<sizeof(labels)/sizeof(labels[0]);++i){
    Assistant timers;ask(timers,("set timer 5 minutes called "+std::string(labels[i])).c_str(),c);ask(timers,"set timer 2 minutes called spare",c);
    assert(contains(ask(timers,("pause timer "+std::string(labels[i])).c_str(),c),"paused"));
    assert(!contains(ask(timers,"check timer 2",c),"paused"));
    assert(contains(ask(timers,("resume timer "+std::string(labels[i])).c_str(),c),"running"));
    assert(contains(ask(timers,("cancel timer "+std::string(labels[i])).c_str(),c),"cancelled"));assert(timers.timer_count()==1);
  }
  Assistant timers;ask(timers,"set timer 5 minutes called tea",c);
  assert(contains(ask(timers,"pause ---",c),"couldn't find"));ask(timers,"cancel",c);
  assert(contains(ask(timers,"cancel timer ???",c),"couldn't find"));assert(timers.timer_count()==1);ask(timers,"cancel",c);
  assert(!contains(ask(timers,"check timer 1",c),"paused"));
  assert(contains(ask(timers,"pause timer?",c),"paused")); // Explicit empty selector may choose the sole timer.
  ask(timers,"set timer 5 minutes called ---",c);assert(timers.timer_count()==1);
  ask(timers,"set timer 5 minutes called extra",c);ask(timers,"pause timer",c);
  assert(contains(ask(timers,"timer history",c),"No completed"));
  assert(contains(ask(timers,"yes",c),"no confirmation"));
  ask(timers,"set timer 5 minutes called 1",c);ask(timers,"cancel timer -1",c);assert(timers.timer_count()==3);ask(timers,"cancel",c);
  const char* triggers[]={"answer to life","round table","half life","one question","2 apples"};
  for(size_t i=0;i<sizeof(triggers)/sizeof(triggers[0]);++i){ask(a,("teach "+std::string(triggers[i])+": a taught reply").c_str(),c);assert(contains(ask(a,triggers[i],c),"a taught reply"));}
  assert(contains(ask(a,"what is one plus two",c),"Result: 3"));
  assert(contains(ask(a,"what is five miles in km",c),"8.04672 km"));
  assert(contains(ask(a,"ans / 2",c),"Result: 1.5"));
  assert(contains(ask(a,"pi * 0",c),"Result: 0"));
  assert(contains(ask(a,"e^0",c),"Result: 1"));
  assert(contains(ask(a,"sqrt(81)",c),"Result: 9"));
  assert(contains(ask(a,"1e309",c),"too large"));

  // Valid hand-edited saves may use compact JSON newline/tab escapes. Loading
  // one must not expand its next save/undo snapshot beyond the 64 KiB limit.
  const char* file="build/tests/assistant-multiline.json";remove(file);remove("build/tests/assistant-multiline.json.bak");
  std::string json="{\"version\":1,\"name\":\"Legacy\",\"notes\":[";
  for(int i=1;i<=32;++i){if(i>1) json+=',';char id[20];snprintf(id,sizeof(id),"%d",i);json+="{\"id\":"+std::string(id)+",\"text\":\"x";
    for(int n=0;n<511;++n) json+=(n%2 ? "\\t" : "\\n");json+="\"}";}
  json+="]}";assert(json.size()<65536 && gx_write_text_file(file,json));Assistant saved;assert(saved.load(file));
  assert(saved.save(file));assert(contains(ask(saved,"delete note 32",c),"deleted"));
  assert(contains(ask(saved,"undo",c),"Undid") && saved.note_count()==32);assert(saved.save(file));
  Assistant restored;assert(restored.load(file) && restored.note_count()==32);remove(file);remove("build/tests/assistant-multiline.json.bak");
  std::cout<<"Assistant follow-up interruption, literal timer labels and multiline save/undo regressions passed\n";
}
static void assistant_debug_checks(){
  Assistant a;AssistantContext c;c.now=100;
  assert(contains(ask(a,"calculate twenty-five plus one hundred and five",c),"Result: 130"));
  assert(contains(ask(a,"calculate one thousand two hundred / three",c),"Result: 400"));
  assert(contains(ask(a,"calculate two point five times four",c),"Result: 10"));
  assert(contains(ask(a,"what's seven squared?",c),"Result: 49"));
  assert(contains(ask(a,"convert five miles to km",c),"8.04672 km"));
  assert(contains(ask(a,"and two",c),"3.218688 km"));
  assert(contains(ask(a,"set timer twenty five minutes called tea",c),"25m"));
  ask(a,"cancel timer -1",c);assert(a.timer_count()==1);ask(a,"cancel",c);
  assert(contains(ask(a,"cancel timer 1, please?",c),"cancelled"));assert(!a.timer_count());
  assert(contains(ask(a,"set timer one hundred and five seconds called tea",c),"1m 45s"));
  c.now=110;assert(contains(ask(a,"pause timer tea",c),"1m 35s"));
  std::vector<std::string> alerts;a.poll(1000,alerts);assert(alerts.empty() && a.timer_count()==1);
  c.now=1000;assert(contains(ask(a,"check timer tea",c),"paused"));
  assert(contains(ask(a,"extend it by thirty seconds",c),"2m 5s"));
  assert(contains(ask(a,"resume it",c),"2m 5s"));
  a.poll(1124,alerts);assert(alerts.empty());a.poll(1125,alerts);assert(alerts.size()==1 && !a.timer_count());
  c.now=1200;ask(a,"set timer 20 seconds called red",c);ask(a,"set timer 40 seconds called blue",c);
  assert(contains(ask(a,"restart timer red",c),"20s"));
  assert(contains(ask(a,"pause timer",c),"Which timer"));
  assert(contains(ask(a,"blue",c),"paused"));
  assert(contains(ask(a,"list timers",c),"(paused)"));
  assert(contains(ask(a,"extend timer blue by 8 days",c),"Use extend"));
  assert(contains(ask(a,"check timer blue",c),"40s"));
  assert(contains(ask(a,"cancel timer red?",c),"cancelled"));
  assert(contains(ask(a,"pause it",c),"couldn't find")); // Never retarget a stale pronoun to the remaining timer.
  ask(a,"cancel",c);assert(contains(ask(a,"resume timer blue",c),"40s"));
  assert(contains(ask(a,"restart it",c),"40s"));
  ask(a,"cancel timer blue",c);assert(!a.timer_count());
  ask(a,"set timer 7 days called long",c);assert(contains(ask(a,"extend it by 1 second",c),"cannot exceed"));
  ask(a,"cancel timer long",c);
  ask(a,"set timer 5 minutes called same",c);ask(a,"set timer 10 minutes called same",c);
  assert(contains(ask(a,"cancel timer same",c),"Which timer"));assert(a.timer_count()==2);
  assert(contains(ask(a,"7?",c),"cancelled"));assert(a.timer_count()==1);
  ask(a,"cancel all timers",c);ask(a,"yes",c);assert(!a.timer_count());
  ask(a,"set timer 1 second called expired",c);c.now+=2;
  assert(contains(ask(a,"restart it",c),"already finished"));a.poll(c.now,alerts);assert(alerts.size()==1);

  ask(a,"remember controller batteries",c);ask(a,"remember player score 500",c);
  assert(contains(ask(a,"read note 1?",c),"controller batteries"));
  assert(contains(ask(a,"find notes about score player",c),"player score 500"));
  assert(contains(ask(a,"find notes about score player",c),"Matching notes"));
  assert(contains(ask(a,"find notes score controller",c),"No notes match"));
  ask(a,"search notes",c);assert(contains(ask(a,"batteries",c),"controller batteries"));
  ask(a,"edit note 1",c);assert(contains(ask(a,"buy rechargeable batteries",c),"Note 1:"));
  ask(a,"edit note 1: score 600",c);ask(a,"edit note 999: invalid",c);
  assert(contains(ask(a,"undo",c),"Undid"));assert(contains(ask(a,"show note 1",c),"rechargeable"));
  assert(contains(ask(a,"undo",c),"No saved-data change"));
  ask(a,"delete note 2 please?",c);assert(a.note_count()==1);ask(a,"undo",c);assert(a.note_count()==2);
  ask(a,"delete all notes",c);assert(contains(ask(a,"do not",c),"not changed"));ask(a,"yes",c);assert(a.note_count()==2);
  ask(a,"delete all notes",c);a.respond(std::string(513,'x'),c);ask(a,"yes",c);assert(a.note_count()==2);
  ask(a,"clear chat",c);a.respond(std::string(513,'x'),c);assert(ask(a,"yes",c).action==AA_NONE);
  ask(a,"delete all notes",c);ask(a,"...",c);ask(a,"yes",c);assert(a.note_count()==2);
  ask(a,"edit note 1",c);ask(a,"status",c);ask(a,"not an edit",c);assert(contains(ask(a,"read note 1",c),"rechargeable"));
  ask(a,"delete all notes",c);ask(a,"yes",c);assert(!a.note_count());ask(a,"undo",c);assert(a.note_count()==2);
  ask(a,"my name is Martin",c);ask(a,"call me Other",c);ask(a,"undo",c);assert(contains(ask(a,"my name",c),"Martin"));

  ask(a,"teach favorite game: Halo 3",c);assert(contains(ask(a,"what is your favorite game",c),"Halo 3"));
  assert(contains(ask(a,"favorite gameplay",c),"couldn't match"));
  ask(a,"teach favorite game: Halo 2",c);ask(a,"undo",c);assert(contains(ask(a,"favorite game",c),"Halo 3"));
  ask(a,"teach magic phrase: clear chat",c);AssistantReply reply=ask(a,"magic phrase",c);assert(reply.text=="clear chat" && reply.action==AA_NONE);
  ask(a,"teach open models: not allowed to shadow commands",c);assert(ask(a,"open models",c).action==AA_MODELS);
  ask(a,"teach gpu: my taught answer",c);assert(contains(ask(a,"help with gpu",c),"my taught answer"));
  ask(a,"forget answer favorite game?",c);assert(contains(ask(a,"favorite game",c),"couldn't match"));
  ask(a,"undo",c);assert(contains(ask(a,"favorite game",c),"Halo 3"));
  assert(contains(ask(a,"list learned answers",c),"favorite game: Halo 3"));
  ask(a,"teach short",c);assert(contains(ask(a,"undo",c),"No saved-data change"));

  const char* state="build/tests/assistant-debug.json";remove(state);remove("build/tests/assistant-debug.json.bak");
  ask(a,"remember temporary",c);ask(a,"delete note 3",c);assert(a.save(state));
  Assistant b;assert(b.load(state));assert(contains(ask(b,"favorite game",c),"Halo 3"));
  assert(contains(ask(b,"remember fresh",c),"note 4"));assert(contains(ask(b,"undo",c),"Undid"));
  assert(contains(ask(b,"remember after undo",c),"note 5"));
  remove(state);remove("build/tests/assistant-debug.json.bak");
  assert(gx_write_text_file(state,"{\"version\":1,\"name\":\"Legacy\",\"notes\":[{\"id\":7,\"text\":\"legacy note\"}]}"));
  assert(b.load(state));assert(contains(ask(b,"remember migrated",c),"note 8"));
  assert(contains(ask(b,"favorite game",c),"couldn't match"));assert(b.save(state));
  remove(state);remove("build/tests/assistant-debug.json.bak");
  const char* corrupt[]={
    "{\"version\":1,\"name\":\"bad\\u0000name\",\"notes\":[]}",
    "{\"version\":1,\"name\":\"bad\\rname\",\"notes\":[]}",
    "{\"version\":1,\"name\":\"\",\"notes\":[{\"id\":1,\"text\":\"bad\\u001btext\"}]}",
    "{\"version\":2,\"name\":\"\",\"next_note\":0,\"notes\":[],\"answers\":[]}",
    "{\"version\":2,\"name\":\"\",\"next_note\":1,\"notes\":[{\"id\":1,\"text\":\"x\"}],\"answers\":[]}",
    "{\"version\":2,\"name\":\"\",\"next_note\":1,\"notes\":[],\"answers\":[{\"phrase\":\"same\",\"response\":\"a\"},{\"phrase\":\"same\",\"response\":\"b\"}]}",
    "{\"version\":2,\"name\":\"\",\"next_note\":1,\"notes\":[],\"answers\":[{\"phrase\":\"Upper Case\",\"response\":\"x\"}]}",
    "{\"version\":2,\"name\":\"\",\"next_note\":1,\"notes\":[],\"answers\":[{\"phrase\":\"valid\",\"response\":\"bad\\u0000text\"}]}"
  };
  for(size_t i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);++i){
    char fixture[96];snprintf(fixture,sizeof(fixture),"build/tests/assistant-corrupt-%u.json",(unsigned)i);remove(fixture);
    assert(gx_write_text_file(fixture,corrupt[i]));assert(!b.load(fixture));assert(b.note_count()==2);assert(contains(ask(b,"my name",c),"Legacy"));remove(fixture);
  }
  const char* rules="build/tests/assistant-debug-rules.json";
  assert(gx_write_text_file(rules,"{\"version\":1,\"rules\":[{\"phrases\":[\"favorite game\"],\"response\":\"custom file\"}]}"));
  assert(a.load_rules(rules));assert(contains(ask(a,"favorite game",c),"Halo 3")); // Taught answers win equal FAQ matches.
  assert(gx_write_text_file(rules,"{\"version\":1,\"rules\":[{\"phrases\":[\"favorite game\"],\"response\":\"bad\\u0000response\"}]}"));
  assert(!a.load_rules(rules));remove(rules);
  Assistant bounded;for(int i=0;i<16;++i){char command[80];snprintf(command,sizeof(command),"teach custom phrase %d: reply",i);ask(bounded,command,c);}
  assert(contains(ask(bounded,"teach overflow phrase: nope",c),"16"));ask(bounded,"forget answer custom phrase 0",c);
  assert(contains(ask(bounded,"teach replacement phrase: ok",c),"Learned"));

  // Deterministic hostile syntax: random punctuation/UTF-8 fragments must never dispatch app actions.
  unsigned int seed=0x360a1;const char* alphabet="0123456789()+-*/%^.,!?_\x80\xff";
  for(int i=0;i<2000;++i){seed=seed*1664525u+1013904223u;size_t n=seed%513;std::string input;
    for(size_t j=0;j<n;++j){seed=seed*1664525u+1013904223u;input+=alphabet[seed%strlen(alphabet)];}
    AssistantReply r=bounded.respond("calculate "+input,c);assert(r.action==AA_NONE && r.text.size()<4096);}
  assert(contains(bounded.respond("calculate "+std::string(100,'(')+"1"+std::string(100,')'),c),"complex"));
  assert(contains(bounded.respond("calculate "+std::string(257,' ')+"one",c),"Result: 1")); // Whitespace is normalized before the expression bound.
  std::cout<<"Assistant debugging regressions, timer controls, note edits, taught answers and malformed-input checks passed\n";
}
static void assistant_checks(){
  Assistant a;AssistantContext c;c.now=100;c.version="test";c.clock="12:34:56";c.date="2026-11-08";c.free_bytes=128u*1048576;
  assert(contains(ask(a,"help",c),"without a model"));
  assert(contains(ask(a,"this",c),"couldn't match")); // 'hi' must not match inside 'this'.
  assert(contains(ask(a,"calcluate 4+4",c),"not run a command"));
  assert(contains(ask(a,"HEY 360AI, please calculate 2 + 3 * 4",c),"Result: 14"));
  assert(contains(ask(a,"what is (2 + 3) * 4?",c),"Result: 20"));
  assert(contains(ask(a,"calculate -2^2",c),"Result: -4"));
  assert(contains(ask(a,"calculate 2^3^2",c),"Result: 512"));
  assert(contains(ask(a,"calculate sqrt(81) + abs(-5)",c),"Result: 14"));
  assert(contains(ask(a,"what is 20 percent of 80",c),"Result: 16"));
  assert(contains(ask(a,"calculate ans / 4",c),"Result: 4"));
  assert(contains(ask(a,"calculate 1/0",c),"zero"));
  assert(contains(ask(a,"calculate ans",c),"Result: 4")); // Failed calculations keep ans.
  assert(contains(ask(a,"calculate sqrt(-1)",c),"nonnegative"));
  assert(contains(ask(a,"calculate 1e309",c),"too large"));
  assert(contains(ask(a,"calculate 1 + bad",c),"Unknown"));
  assert(contains(ask(a,"calculate (1+2",c),"parenthesis"));
  assert(contains(ask(a,"calculate",c),"What should I calculate"));
  assert(contains(ask(a,"8 times 7",c),"Result: 56"));
  assert(contains(ask(a,"convert 1 mile to km",c),"1.609344 km"));
  assert(contains(ask(a,"and 10",c),"16.09344 km"));
  assert(contains(ask(a,"what is 32 F in C?",c)," = 0 C"));
  assert(contains(ask(a,"convert -40 celsius to fahrenheit",c),"-40 F"));
  assert(contains(ask(a,"convert -1 kelvin to celsius",c),"outside"));
  assert(contains(ask(a,"convert 1 kg to km",c),"compatible"));
  assert(contains(ask(a,"convert 1 MiB to bytes",c),"1048576 bytes"));
  assert(contains(ask(a,"convert 1 MB to bytes",c),"1000000 bytes"));
  assert(contains(ask(a,"convert 3.2bananas to km",c),"compatible"));
  assert(contains(ask(a,"my name is Martin",c),"Martin"));
  assert(contains(ask(a,"what's my name?",c),"Martin"));
  assert(contains(ask(a,"remember to buy batteries",c),"note 1"));
  assert(contains(ask(a,"don't forget to clean the controller",c),"note 2"));
  assert(a.note_count()==2 && a.dirty());
  ask(a,"don't delete note 1",c);assert(a.note_count()==2);
  ask(a,"delete note 1 and 2",c);assert(a.note_count()==2);
  assert(contains(ask(a,"show me my notes",c),"buy batteries"));
  ask(a,"delete all notes",c);ask(a,"no",c);assert(a.note_count()==2);
  ask(a,"delete all notes",c);ask(a,"status",c);ask(a,"yes",c);assert(a.note_count()==2);
  ask(a,"delete note 1",c);assert(a.note_count()==1);
  ask(a,"remember don't leave the controller on",c);assert(a.note_count()==2);
  ask(a,"delete note 3",c);assert(a.note_count()==1);
  ask(a,"remember",c);ask(a,"check batteries",c);assert(a.note_count()==2);
  assert(contains(ask(a,"time",c),"12:34:56"));assert(contains(ask(a,"date",c),"2026-11-08"));
  assert(contains(ask(a,"status",c),"128 MiB"));
  assert(contains(ask(a,"weather",c),"no live lookup"));
  assert(contains(ask(a,"microphone",c),"not implemented"));
  assert(contains(ask(a,"help with gpu",c),"pixel shaders"));
  assert(ask(a,"use quick preset",c).action==AA_PROFILE);
  assert(ask(a,"use quick preset",c).value==1);
  assert(ask(a,"don't use quick preset",c).action==AA_NONE);
  assert(ask(a,"clear chat",c).action==AA_NONE);
  assert(ask(a,"yes",c).action==AA_CLEAR_CHAT);
  assert(ask(a,"yes",c).action==AA_NONE);
  assert(ask(a,"export chat",c).action==AA_EXPORT);
  assert(ask(a,"open models",c).action==AA_MODELS);
  std::vector<std::string> alerts;
  assert(contains(ask(a,"SET   TIMER 1m30s called tea",c),"1m 30s"));
  assert(a.timer_count()==1);
  a.poll(189.9,alerts);assert(alerts.empty());a.poll(190,alerts);assert(alerts.size()==1 && alerts[0].find("tea")!=std::string::npos);
  a.poll(200,alerts);assert(alerts.empty() && !a.timer_count());
  ask(a,"set timer",c);assert(!a.timer_count());ask(a,"half an hour",c);assert(a.timer_count()==1);
  ask(a,"cancel timer 2",c);assert(!a.timer_count());
  ask(a,"remind me to check the oven",c);ask(a,"two minutes",c);assert(a.timer_count()==1);
  assert(contains(ask(a,"list timers",c),"check the oven"));
  ask(a,"cancel all timers",c);ask(a,"no",c);assert(a.timer_count()==1);
  ask(a,"cancel all timers",c);ask(a,"yes",c);assert(!a.timer_count());
  ask(a,"remind me in 3 seconds to stretch",c);assert(a.timer_count()==1);
  a.poll(103,alerts);assert(alerts.size()==1 && alerts[0].find("stretch")!=std::string::npos);
  ask(a,"set timer 0 seconds",c);ask(a,"cancel",c);assert(!a.timer_count());
  ask(a,"set timer -2 minutes",c);ask(a,"cancel",c);assert(!a.timer_count());
  ask(a,"set timer 8 days",c);ask(a,"cancel",c);assert(!a.timer_count());
  ask(a,"set timer 5 minutes and delete all notes",c);assert(!a.timer_count());ask(a,"cancel",c);
  for(int i=0;i<8;++i) ask(a,"set timer 10 seconds",c);assert(a.timer_count()==8);
  ask(a,"set timer 1 second",c);assert(a.timer_count()==8);
  a.reset_conversation();assert(a.timer_count()==8 && a.note_count()==2);
  a.poll(110,alerts);assert(alerts.size()==8);
  ask(a,"set timer",c);assert(contains(ask(a,"status",c),"offline keyword assistant"));
  const char* file="build/tests/assistant-state.json";
  remove(file);remove("build/tests/assistant-state.json.bak");
  assert(a.save(file) && !a.dirty());Assistant restored;assert(restored.load(file) && restored.note_count()==2);
  assert(contains(ask(restored,"what's my name",c),"Martin"));
  ask(a,"remember a third note",c);assert(a.save(file));
  assert(gx_write_text_file(file,"{broken"));assert(restored.load(file) && restored.note_count()==2);
  assert(a.save(file));assert(gx_write_text_file(file,"{}"));assert(restored.load(file) && restored.note_count()==2);
  remove(file);remove("build/tests/assistant-state.json.bak");
  assert(!a.save("build/tests/no-directory/assistant.json"));
  assert(gx_write_text_file(file,"{\"version\":1,\"name\":\"\",\"notes\":[{\"id\":1,\"text\":\"a\"},{\"id\":1,\"text\":\"b\"}]}"));
  assert(!restored.load(file));remove(file);
  const char* rules="build/tests/assistant-rules.json";
  assert(gx_write_text_file(rules,"{\"version\":1,\"rules\":[{\"phrases\":[\"favorite game\"],\"response\":\"Halo is a custom reply.\"}]}"));
  assert(a.load_rules(rules));assert(contains(ask(a,"what is your favorite game",c),"custom reply"));
  assert(contains(ask(a,"favorite gameplay",c),"couldn't match"));
  assert(gx_write_text_file(rules,"{\"version\":1,\"rules\":[{\"phrases\":[\"\"],\"response\":\"bad\"}]}"));
  assert(!a.load_rules(rules));assert(contains(ask(a,"favorite game",c),"custom reply"));remove(rules);
  Assistant limits;for(int i=0;i<32;++i) ask(limits,"remember bounded note",c);assert(limits.note_count()==32);
  ask(limits,"remember too many",c);assert(limits.note_count()==32);
  assert(contains(limits.respond(std::string(513,'x'),c),"512"));
  assert(limits.respond("don't export chat",c).action==AA_NONE);
  assistant_debug_checks();
  assistant_followup_checks();
  std::cout<<"Keyword assistant intent, math, units, timer, notebook and persistence checks passed\n";
}
