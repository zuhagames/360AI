#include "assistant.h"
#include "json.h"
#include "platform.h"
#include "textstream.h"
#include <ctype.h>
#include <float.h>

namespace {
std::string trim(const std::string& s){
  size_t a=0,b=s.size();while(a<b && isspace((u8)s[a])) ++a;while(b>a && isspace((u8)s[b-1])) --b;return s.substr(a,b-a);
}
std::string lower(std::string s){for(size_t i=0;i<s.size();++i) if(s[i]>='A' && s[i]<='Z') s[i]+='a'-'A';return s;}
std::string single_line(const std::string& s){std::string out;bool space=false;for(size_t i=0;i<s.size();++i){
  if((u8)s[i]<32 || (u8)s[i]==127 || isspace((u8)s[i])) space=true;else{if(space && !out.empty()) out+=' ';space=false;out+=s[i];}}return out;}
std::string words(const std::string& s){
  std::string out;bool space=false;
  for(size_t i=0;i<s.size();++i){u8 c=(u8)s[i];if(c=='\'') continue;
    if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c>=128){
      if(space && !out.empty()) out+=' ';space=false;out+=(c>='A' && c<='Z') ? c+32 : c;
    } else space=true;
  }return out;
}
bool phrase(const std::string& normalized,const std::string& p){return (" "+normalized+" ").find(" "+p+" ")!=std::string::npos;}
bool take(const std::string& raw,const char* prefix,std::string& rest){
  std::string low=lower(raw);size_t n=strlen(prefix);
  if(low.compare(0,n,prefix)!=0 || (low.size()>n && !isspace((u8)low[n]) && low[n]!=':' && low[n]!=',')) return false;
  rest=trim(raw.substr(n));while(!rest.empty() && (rest[0]==':' || rest[0]==',')) rest=trim(rest.substr(1));return true;
}
bool finite_number(double v){return v==v && fabs(v)<1e100;}
std::string number(double v){char s[64];snprintf(s,sizeof(s),"%.10g",v==0 ? 0 : v);return s;}
std::string integer_text(int v){char s[32];snprintf(s,sizeof(s),"%d",v);return s;}
bool id_number(const std::string& s,int& value){
  if(s.empty() || s.size()>7) return false;int n=0;
  for(size_t i=0;i<s.size();++i){if(s[i]<'0' || s[i]>'9') return false;n=n*10+s[i]-'0';}
  if(n<1 || n>1000000) return false;value=n;return true;
}
void replace_phrase(std::string& text,const char* from,const char* to){
  size_t p=0,n=strlen(from);while((p=text.find(from,p))!=std::string::npos){
    if((p==0 || !isalpha((u8)text[p-1])) && (p+n==text.size() || !isalpha((u8)text[p+n]))){text.replace(p,n,to);p+=strlen(to);}else p+=n;
  }
}
class Calculator {
  std::string input_;size_t p_;int depth_,work_;double ans_;bool have_ans_;
  void ws(){while(p_<input_.size() && isspace((u8)input_[p_])) ++p_;}
  bool eat(char c){ws();if(p_<input_.size() && input_[p_]==c){++p_;return true;}return false;}
  double fail(const char* why){if(error.empty()) error=why;return 0;}
  double checked(double n){return finite_number(n) ? n : fail("Result is too large or undefined.");}
  double primary(){
    if(++work_>128 || ++depth_>32){--depth_;return fail("Expression is too complex.");}
    ws();double result=0;
    if(eat('(')){result=expression();if(!eat(')')) fail("Missing closing parenthesis.");}
    else if(p_<input_.size() && (isdigit((u8)input_[p_]) || input_[p_]=='.')){
      char* end=0;result=strtod(input_.c_str()+p_,&end);size_t next=(size_t)(end-input_.c_str());
      if(next==p_) fail("Expected a number.");else p_=next;result=checked(result);
    } else {
      size_t start=p_;while(p_<input_.size() && isalpha((u8)input_[p_])) ++p_;
      std::string name=input_.substr(start,p_-start);
      if(name=="ans") result=have_ans_ ? ans_ : fail("There is no previous answer yet.");
      else if(name=="pi") result=3.14159265358979323846;
      else if(name=="e") result=2.71828182845904523536;
      else if(name=="sqrt" || name=="abs" || name=="round"){
        if(!eat('(')) fail("Functions need parentheses, such as sqrt(81).");
        else {double v=expression();if(!eat(')')) fail("Missing closing parenthesis.");
          if(name=="sqrt") result=v<0 ? fail("Square root needs a nonnegative number.") : sqrt(v);
          else if(name=="abs") result=fabs(v);else result=v<0 ? ceil(v-0.5) : floor(v+0.5);}
      } else fail("Unknown number or function. Use numbers, ans, pi, sqrt, abs or round.");
    }
    --depth_;return result;
  }
  double power(){double a=primary();if(error.empty() && eat('^')) a=checked(pow(a,unary()));return a;}
  double unary(){
    if(++work_>128) return fail("Expression is too complex.");
    if(eat('+')) return unary();if(eat('-')) return -unary();return power();
  }
  double term(){double a=unary();while(error.empty()){
    if(eat('*')) a=checked(a*unary());else if(eat('/')){double b=unary();a=b==0 ? fail("Cannot divide by zero.") : checked(a/b);}
    else if(eat('%')){double b=unary();a=b==0 ? fail("Cannot divide by zero.") : fmod(a,b);}else break;
  }return a;}
  double expression(){double a=term();while(error.empty()){
    if(eat('+')) a=checked(a+term());else if(eat('-')) a=checked(a-term());else break;
  }return a;}
public:
  std::string error;
  Calculator(const std::string& text,double answer,bool have):input_(lower(text)),p_(0),depth_(0),work_(0),ans_(answer),have_ans_(have){
    replace_phrase(input_,"multiplied by","*");replace_phrase(input_,"divided by","/");
    replace_phrase(input_,"plus","+");replace_phrase(input_,"minus","-");replace_phrase(input_,"times","*");
    replace_phrase(input_,"squared","^2");replace_phrase(input_,"percent of","/100*");
    while(!input_.empty() && (input_[input_.size()-1]=='?' || input_[input_.size()-1]=='!')) input_.resize(input_.size()-1);
  }
  bool run(double& result){if(input_.empty() || input_.size()>256){error="Use an expression of 1-256 characters.";return false;}
    result=expression();ws();if(p_!=input_.size() && error.empty()) error="Unexpected text in the expression.";return error.empty() && finite_number(result);}
};
struct Unit {const char* name;const char* aliases;int dimension;double scale,offset;};
const Unit units[]={
  {"m","m|meter|meters|metre|metres",0,1,0},{"km","km|kilometer|kilometers|kilometre|kilometres",0,1000,0},
  {"cm","cm|centimeter|centimeters|centimetre|centimetres",0,.01,0},{"mm","mm|millimeter|millimeters",0,.001,0},
  {"miles","mile|miles|mi",0,1609.344,0},{"feet","foot|feet|ft",0,.3048,0},{"inches","inch|inches|in",0,.0254,0},
  {"yards","yard|yards|yd",0,.9144,0},{"kg","kg|kilogram|kilograms|kilo|kilos",1,1,0},
  {"g","g|gram|grams",1,.001,0},{"lb","lb|lbs|pound|pounds",1,.45359237,0},{"oz","oz|ounce|ounces",1,.028349523125,0},
  {"C","c|celsius|degrees celsius",2,1,273.15},{"F","f|fahrenheit|degrees fahrenheit",2,5.0/9,255.3722222222222},
  {"K","k|kelvin",2,1,0},{"seconds","s|sec|second|seconds",3,1,0},{"minutes","min|minute|minutes",3,60,0},
  {"hours","h|hr|hour|hours",3,3600,0},{"days","d|day|days",3,86400,0},
  {"bytes","b|byte|bytes",4,1,0},{"KB","kb|kilobyte|kilobytes",4,1000,0},{"MB","mb|megabyte|megabytes",4,1000000,0},
  {"GB","gb|gigabyte|gigabytes",4,1000000000,0},{"KiB","kib|kibibyte|kibibytes",4,1024,0},
  {"MiB","mib|mebibyte|mebibytes",4,1048576,0},{"GiB","gib|gibibyte|gibibytes",4,1073741824,0}
};
bool alias(const std::string& key,const char* aliases){return ("|"+std::string(aliases)+"|").find("|"+key+"|")!=std::string::npos;}
const Unit* unit(const std::string& name){std::string key=words(name);for(size_t i=0;i<sizeof(units)/sizeof(units[0]);++i) if(alias(key,units[i].aliases)) return units+i;return 0;}
bool scalar(const std::string& text,double& value,size_t& used){
  char* end=0;value=strtod(text.c_str(),&end);used=(size_t)(end-text.c_str());return used>0 && finite_number(value);
}
bool duration(const std::string& text,double& seconds){
  std::string s=lower(text),expanded;seconds=0;
  for(size_t i=0;i<s.size();++i){if(i && ((isdigit((u8)s[i]) && isalpha((u8)s[i-1])) || (isalpha((u8)s[i]) && isdigit((u8)s[i-1])))) expanded+=' ';expanded+=s[i];}
  s=trim(expanded);size_t pos=0;int parts=0;
  while(pos<s.size()){
    while(pos<s.size() && (isspace((u8)s[pos]) || s[pos]==',')) ++pos;
    if(s.compare(pos,4,"and ")==0) pos+=4;
    if(pos==s.size()) return false;
    double n=0;size_t used=0;std::string tail=s.substr(pos);
    if(scalar(tail,n,used)) pos+=used;
    else {
      static const char* names[]={"zero","one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen","twenty"};
      size_t end=s.find(' ',pos);if(end==std::string::npos) return false;std::string word=s.substr(pos,end-pos);bool found=false;
      if(word=="a" || word=="an"){n=1;found=true;}else if(word=="half"){n=.5;found=true;}else if(word=="quarter"){n=.25;found=true;}
      else for(int i=0;i<=20;++i) if(word==names[i]){n=i;found=true;break;}
      if(!found) return false;pos=end;
    }
    while(pos<s.size() && isspace((u8)s[pos])) ++pos;
    if(s.compare(pos,3,"an ")==0) pos+=3;else if(s.compare(pos,2,"a ")==0) pos+=2;
    size_t start=pos;while(pos<s.size() && isalpha((u8)s[pos])) ++pos;
    std::string suffix=s.substr(start,pos-start);if(suffix=="m") suffix="min";
    const Unit* u=unit(suffix);if(!u || u->dimension!=3 || !(n>0)) return false;
    seconds+=n*u->scale;if(!finite_number(seconds) || seconds>604800 || ++parts>8) return false;
    while(pos<s.size() && isspace((u8)s[pos])) ++pos;
  }
  return parts>0 && seconds>=1;
}
std::string remaining(double seconds){
  int n=(int)ceil(GX_MAX(0.0,seconds));std::string s;
  if(n>=3600){s+=integer_text(n/3600)+"h ";n%=3600;}if(n>=60){s+=integer_text(n/60)+"m ";n%=60;}
  if(n || s.empty()) s+=integer_text(n)+"s";return trim(s);
}
bool mathish(const std::string& s){
  std::string t=lower(trim(s));return !t.empty() && (isdigit((u8)t[0]) || t[0]=='-' || t[0]=='+' || t[0]=='(' || t[0]=='.' ||
    t.compare(0,3,"ans")==0 || t=="pi" || t=="e" || t.compare(0,4,"sqrt")==0 || t.compare(0,3,"abs")==0 || t.compare(0,5,"round")==0);
}
bool new_request(const std::string& raw){
  const char* commands[]={"time","date","status","help","list","show","cancel","stop","delete","forget","clear","convert","calculate","compute","work out","remember","remind","my name","call me","open","switch","use","set","quick preset","balanced preset","creative preset","what","what's","whats","who","how","export","save chat","joke","another joke"};
  std::string rest;for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);++i) if(take(raw,commands[i],rest)) return true;return false;
}
const char* help_text="360 Assistant works offline without a model. Try:\n"
  "calculate (12 + 8) * 3 | what is 20 percent of 80\n"
  "convert 5 miles to km | and 10\n"
  "set timer 5 minutes called pizza | list timers | cancel timer 1 | timer history\n"
  "remind me in 10 minutes to check the oven\n"
  "remember to clean the controller | list notes | delete note 1\n"
  "my name is Martin | what is my name | time | date | status\n"
  "quick preset | balanced preset | creative preset\n"
  "help with GPU | help with models | joke | export chat | clear chat\n"
  "Use cancel to leave a follow-up question. Timers run only while this app is open.";
struct Fact {const char* phrases;const char* response;};
const Fact facts[]={
  {"gpu|xenos|graphics processor","Xenos matrix math uses pixel shaders, with CPU weight unpacking and GPU readbacks. Auto checks numerical agreement and timing. Forced GPU can be slower. Choose Quick/Auto for short model replies."},
  {"cpu|xenon|processor","Language models still need substantial CPU work. Auto chooses the measured backend. This keyword assistant skips model inference entirely and answers only its supported intents and local knowledge."},
  {"ram|memory|hdd|streaming|hard drive","RAM holds live working data. HDD streaming repeatedly reads model weights with an 8 MB cache; it does not turn disk space into fast RAM. Use Auto/RAM for the small bundled models."},
  {"models|model|weights|gguf","SmolLM2 is the bundled chat choice. Gemma is also included. TinyStories 15M/260K are very limited story toys. This assistant needs no GGUF. Press B for the model list, or type open models."},
  {"mcp|model context protocol","MCP lets compatible clients discover and call tools. 360AI's model server exposes /mcp when enabled before loading a GGUF. Keyword assistant mode has no inference server."},
  {"api|openai|server","For model API requests, load a GGUF with OpenAI / MCP server enabled. Use POST /v1/chat/completions with JSON messages, or POST /v1/completions with a prompt. A browser GET does not generate text."},
  {"slow|speed|latency|performance","This assistant runs keyword rules without model inference. For model chat, use SmolLM2, Quick preset and Auto compute/storage. Large models, long prompts, HDD reads and forced GPU can add latency."},
  {"controller|controls|buttons","A opens the keyboard. D-pad scrolls chat; LB/RB pages; right-stick click follows the latest text. X starts a fresh chat, START exports it, and B returns to models. Y cancels a model reply."},
  {"timer|timers|reminder|reminders","Try set timer 5 minutes called pizza, list timers, or cancel timer 1. Up to eight timers run while 360AI is open. Alerts appear in chat; timers do not survive closing/restarting the app."},
  {"notes|note|remember","Try remember to buy batteries, list notes, or delete note 1. Up to 32 notes and your name can be saved in 360AI-assistant.json. Notes are plain text on your drive. Clearing chat leaves saved notes intact."},
  {"calculator|math|calculate","Use calculate (12 + 8) * 3, sqrt(81), 2^8, abs(-5) or 20 percent of 80. ans refers to the last successful result. % is remainder; use percent of for percentages."},
  {"conversion|conversions|units|convert","Convert lengths, masses, temperatures, time and byte storage: convert 5 miles to km. Then say and 10 to reuse those units. MB/GB use decimal bytes; MiB/GiB use binary bytes. No currency or live exchange rates."},
  {"context|tokens|token","Model context contains prompt and reply tokens. A smaller context/reply budget limits waiting but cannot guarantee higher tokens per second. The keyword assistant has bounded rule-based follow-up state instead of a model context."},
  {"temperature|top k|top p|sampling","Model temperature, top-k, top-p and repetition penalty control sampling. They do not affect this deterministic keyword assistant. Quick/Balanced/Creative change the saved model settings."},
  {"export|save chat","Type export chat or press START to write 360AI-chat.txt beside the app. Export replaces the previous export. Saved notes are separate from the chat transcript."},
  {"voice|siri|microphone|speech","This mode is a text-based keyword/intent assistant. Use the controller keyboard. Speech recognition, microphone input and spoken replies are not implemented."},
  {"weather|news|internet|search|currency|stock price","I have no live lookup in this offline mode. I can calculate, convert units, manage local notes/timers and explain supported Xbox app features. I cannot provide current weather, news or prices."},
  {"who made you|credits|canyons|zuhagames","360AI is made by ZuhaGames & Canyons, expanded from Canyons' gemma.xex. AI tools assisted development, debugging and documentation."},
  {"who are you|what are you|what can you do","I am 360 Assistant, a local keyword and intent assistant inside 360AI. Type help for commands. I do not run a language model or understand arbitrary questions."}
};
size_t match_aliases(const std::string& normalized,const char* aliases){
  size_t best=0,start=0;std::string a=aliases;
  for(;;){size_t end=a.find('|',start);std::string p=a.substr(start,end==std::string::npos ? end : end-start);
    if(phrase(normalized,p)) best=GX_MAX(best,p.size());if(end==std::string::npos) break;start=end+1;}return best;
}
bool one_edit(const std::string& a,const std::string& b){
  if(a.size()+1<b.size() || b.size()+1<a.size()) return false;size_t i=0,j=0;int edits=0;
  while(i<a.size() && j<b.size()){if(a[i]==b[j]){++i;++j;continue;}if(++edits>1) return false;
    if(a.size()==b.size()){if(i+1<a.size() && a[i]==b[j+1] && a[i+1]==b[j]){i+=2;j+=2;}else{++i;++j;}}
    else if(a.size()<b.size()) ++j;else ++i;
  }return edits+(i<a.size() || j<b.size())==1;
}
}

Assistant::Assistant():answer_(0),has_answer_(false),dirty_(false),next_note_(1),next_timer_(1),joke_(0),pending_(P_NONE){}
void Assistant::reset_conversation(){pending_=P_NONE;pending_label_.clear();last_from_.clear();last_to_.clear();has_answer_=false;}
AssistantReply Assistant::calculate(const std::string& text){
  if(trim(text).empty()){pending_=P_CALC;return AssistantReply("What should I calculate? Try (12 + 8) * 3.");}
  Calculator calc(text,answer_,has_answer_);double result=0;
  if(!calc.run(result)) return AssistantReply("Calculation: "+calc.error);
  answer_=result;has_answer_=true;pending_=P_NONE;return AssistantReply("Result: "+number(result));
}
AssistantReply Assistant::convert(const std::string& text,bool followup){
  std::string input=lower(trim(text));while(!input.empty() && (input[input.size()-1]=='?' || input[input.size()-1]=='!')) input.resize(input.size()-1);
  if(input.find(" to ")==std::string::npos){size_t p=input.rfind(" in ");if(p!=std::string::npos) input.replace(p,4," to ");}
  double value=0;size_t used=0;if(!scalar(input,value,used)) return AssistantReply("Use convert 5 miles to km. Supported: length, mass, temperature, time and byte storage.");
  const Unit *from=0,*to=0;
  if(followup && trim(input.substr(used)).empty()){from=unit(last_from_);to=unit(last_to_);}
  else {size_t split=input.find(" to ",used);if(split!=std::string::npos){from=unit(trim(input.substr(used,split-used)));to=unit(trim(input.substr(split+4)));}}
  if(!from || !to || from->dimension!=to->dimension) return AssistantReply("Use compatible units, such as convert 5 miles to km or convert 32 F to C. MB means bytes, not bits.");
  double base=value*from->scale+from->offset,result=(base-to->offset)/to->scale;
  if(!finite_number(result) || (from->dimension==2 && base<-0.00000001)) return AssistantReply("That conversion is outside the supported numeric range.");
  last_from_=from->name;last_to_=to->name;return AssistantReply(number(value)+" "+from->name+" = "+number(result)+" "+to->name);
}
AssistantReply Assistant::add_note(const std::string& text){
  std::string note=trim(text);if(note.empty()){pending_=P_NOTE;return AssistantReply("What should I remember? Your next message will become a note; cancel leaves this question.");}
  if(note.size()>512) return AssistantReply("Keep a note within 512 bytes.");
  if(notes_.size()>=32 || next_note_>1000000){pending_=P_NONE;return AssistantReply("The 32-note notebook is full. List notes and delete one first.");}
  Note n;n.id=next_note_++;n.text=note;notes_.push_back(n);dirty_=true;pending_=P_NONE;
  return AssistantReply("Saved note "+integer_text(n.id)+": "+note);
}
AssistantReply Assistant::add_timer(const std::string& text,const AssistantContext& context){
  std::string input=trim(text),label=pending_label_.empty() ? "Timer" : pending_label_;pending_label_.clear();size_t at=lower(input).find(" called ");
  if(at==std::string::npos) at=lower(input).find(" named ");
  if(at!=std::string::npos){label=trim(input.substr(at+7));input=trim(input.substr(0,at));}
  while(!input.empty() && (input[input.size()-1]=='?' || input[input.size()-1]=='!')) input.resize(input.size()-1);
  std::string rest;if(take(input,"for",rest)) input=rest;
  double seconds=0;if(!duration(input,seconds)){pending_=P_TIMER;pending_label_=label;return AssistantReply("For how long? Try 5 minutes, 1h 30m, or half an hour. Range: 1 second to 7 days. Optional: called pizza.");}
  if(label.empty() || label.size()>64) return AssistantReply("Use a timer label of 1-64 bytes.");
  if(timers_.size()>=8 || next_timer_>1000000){pending_=P_NONE;return AssistantReply("Eight timers are already active. List timers and cancel one first.");}
  if(!finite_number(context.now) || !finite_number(context.now+seconds)) return AssistantReply("Timer clock unavailable.");
  Timer t;t.id=next_timer_++;t.due=context.now+seconds;t.label=label;timers_.push_back(t);pending_=P_NONE;
  return AssistantReply("Timer "+integer_text(t.id)+" ("+label+") set for "+remaining(seconds)+". Keep 360AI open; ask list timers to check it.");
}
void Assistant::poll(double now,std::vector<std::string>& out){
  out.clear();for(size_t i=0;i<timers_.size();){if(now>=timers_[i].due){
    std::string alert="Timer "+integer_text(timers_[i].id)+" finished: "+timers_[i].label;out.push_back(alert);timer_history_.push_back(alert);
    if(timer_history_.size()>8) timer_history_.erase(timer_history_.begin());timers_.erase(timers_.begin()+i);
  }else ++i;}
}
AssistantReply Assistant::respond(const std::string& text,const AssistantContext& context){
  if(text.size()>512) return AssistantReply("Keep a request within 512 bytes.");
  std::string raw=single_line(gx_utf8_clean(text)),rest;
  if(take(raw,"hey 360ai",rest) || take(raw,"hey assistant",rest) || take(raw,"360ai",rest) || take(raw,"assistant",rest)) raw=rest;
  for(int polite=0;polite<2;++polite){if(take(raw,"please",rest) || take(raw,"can you",rest) || take(raw,"could you",rest) || take(raw,"would you",rest)) raw=rest;else break;}
  std::string norm=words(raw);if(norm.empty()) return AssistantReply("Ready. Type help for commands.");
  if(take(raw,"don't forget to",rest) || take(raw,"do not forget to",rest)){pending_=P_NONE;pending_label_.clear();return add_note(rest);}
  if(norm=="dont" || norm.compare(0,5,"dont ")==0 || norm.compare(0,7,"do not ")==0 ||
     norm=="never" || norm.compare(0,6,"never ")==0 || norm.compare(0,4,"not ")==0){
    pending_=P_NONE;pending_label_.clear();return AssistantReply("I have not changed anything. State the single command you want me to run, or type help.");}
  if(norm=="cancel" || norm=="never mind" || norm=="nevermind" || norm=="stop"){
    bool had=pending_!=P_NONE;pending_=P_NONE;pending_label_.clear();return AssistantReply(had ? "Cancelled the follow-up. Existing notes and timers are unchanged." : "No follow-up is waiting. Use cancel timer followed by its number to stop a timer.");}
  if(norm=="help" || norm=="commands" || norm=="show commands" || norm=="what can you do"){pending_=P_NONE;pending_label_.clear();return AssistantReply(help_text);}
  if(pending_==P_CLEAR_NOTES || pending_==P_CLEAR_CHAT || pending_==P_TIMER_CANCEL){
    Pending command=pending_;pending_=P_NONE;
    if(norm=="yes" || norm=="confirm" || norm=="yes please"){
      if(command==P_CLEAR_CHAT) return AssistantReply("Chat cleared. Saved notes and active timers are kept.",AA_CLEAR_CHAT);
      if(command==P_TIMER_CANCEL){timers_.clear();return AssistantReply("All timers cancelled.");}
      notes_.clear();dirty_=true;return AssistantReply("All notes deleted. Your saved name is kept.");
    }
    if(norm=="no" || norm=="no thanks") return AssistantReply("Cancelled. Nothing was deleted or cleared.");
    // A different request cancels the pending destructive operation.
  } else if(pending_!=P_NONE && new_request(raw)){pending_=P_NONE;pending_label_.clear();}
  else if(pending_!=P_NONE){
    Pending p=pending_;pending_=P_NONE;
    if(p==P_TIMER) return add_timer(raw,context);
    if(p==P_NOTE) return add_note(raw);
    if(p==P_CALC) return calculate(raw);
    if(p==P_NAME){if(raw.size()>64) return AssistantReply("Keep your name within 64 bytes.");name_=raw;dirty_=true;return AssistantReply("I'll call you "+name_+".");}
  }
  if(norm=="yes" || norm=="no" || norm=="confirm") return AssistantReply("There is no confirmation waiting. Type help for commands.");
  if(norm=="hi" || norm=="hello" || norm=="hey" || norm=="good morning" || norm=="good evening")
    return AssistantReply("Hello"+(name_.empty() ? std::string("") : " "+name_)+". Ready for calculations, conversions, timers, notes and Xbox help.");
  if(norm=="thanks" || norm=="thank you") return AssistantReply("You're welcome.");
  if(norm=="how are you" || norm=="how are you doing") return AssistantReply("Ready to help. My supported commands work offline without loading a model.");
  if(norm=="my name" || norm=="what is my name" || norm=="whats my name" || norm=="who am i") return AssistantReply(name_.empty() ? "You haven't told me your name. Say my name is Martin, for example." : "Your name is "+name_+".");
  if(take(raw,"my name is",rest) || take(raw,"call me",rest)){
    if(rest.empty()){pending_=P_NAME;return AssistantReply("What should I call you?");}
    if(rest.size()>64) return AssistantReply("Keep your name within 64 bytes.");name_=rest;dirty_=true;return AssistantReply("I'll call you "+name_+".");
  }
  if(norm=="forget my name"){name_.clear();dirty_=true;return AssistantReply("Your saved name has been removed.");}
  if(norm=="time" || norm=="what time is it" || norm=="whats the time") return AssistantReply(context.clock.empty() ? "Console clock unavailable." : "Console clock: "+context.clock);
  if(norm=="date" || norm=="what is the date" || norm=="whats todays date" || norm=="what day is it") return AssistantReply(context.date.empty() ? "Console date unavailable." : "Console date: "+context.date);
  if(norm=="status" || norm=="system status" || norm=="free memory" || norm=="version"){
    static const char* profiles[]={"Custom","Quick","Balanced","Creative"};
    return AssistantReply("360AI "+context.version+" | offline keyword assistant\nFree memory: "+number(context.free_bytes/1048576.0)+" MiB\nSaved model preset: "+profiles[GX_CLAMP(context.profile,0,3)]+"\nActive timers: "+integer_text((int)timers_.size())+" | saved notes: "+integer_text((int)notes_.size()));
  }
  if(norm=="list models" || norm=="show models" || norm=="available models"){
    std::string result="GGUF files beside the app:";for(size_t i=0;i<context.models.size() && i<32;++i) result+="\n"+context.models[i];
    if(context.models.empty()) result+=" none. This assistant needs no model.";return AssistantReply(result);
  }
  if(norm=="open models" || norm=="switch to models" || norm=="model list") return AssistantReply("Opening the model list.",AA_MODELS);
  if(norm=="export chat" || norm=="save chat" || norm=="save conversation") return AssistantReply("Export requested.",AA_EXPORT);
  if(norm=="clear chat" || norm=="new chat" || norm=="clear conversation"){
    pending_=P_CLEAR_CHAT;return AssistantReply("Clear this chat transcript? Reply yes or no. Saved notes and timers are kept.");}
  const char* presets[]={"quick","balanced","creative"};
  for(int i=0;i<3;++i) if(norm==std::string(presets[i])+" preset" || norm=="use "+std::string(presets[i])+" preset" || norm=="set "+std::string(presets[i])+" preset")
    return AssistantReply("Model preset selected.",AA_PROFILE,i+1);
  if(norm=="list timers" || norm=="show timers" || norm=="show my timers" || norm=="timers" || norm=="how long is left" || norm=="time left"){
    if(timers_.empty()) return AssistantReply(timer_history_.empty() ? "No active timers. Try set timer 5 minutes called pizza." : "No active timers. Ask timer history for recent completed alerts.");
    std::string result="Active timers (keep 360AI open):";for(size_t i=0;i<timers_.size();++i) result+="\n"+integer_text(timers_[i].id)+": "+timers_[i].label+" - "+remaining(timers_[i].due-context.now);return AssistantReply(result);
  }
  if(norm=="timer history" || norm=="recent alerts" || norm=="show alerts"){
    if(timer_history_.empty()) return AssistantReply("No completed timer alerts in this app session.");
    std::string result="Recent timer alerts:";for(size_t i=0;i<timer_history_.size();++i) result+="\n"+timer_history_[i];return AssistantReply(result);
  }
  if(norm=="cancel all timers" || norm=="stop all timers"){pending_=P_TIMER_CANCEL;return AssistantReply("Cancel every active timer? Reply yes or no.");}
  if(take(raw,"cancel timer",rest) || take(raw,"stop timer",rest)){
    int id=0;if(!id_number(rest,id)) return AssistantReply("Use cancel timer followed by its number, such as cancel timer 1.");
    for(size_t i=0;i<timers_.size();++i) if(timers_[i].id==id){timers_.erase(timers_.begin()+i);return AssistantReply("Timer "+integer_text(id)+" cancelled.");}return AssistantReply("That timer is not active. Try list timers.");
  }
  if(take(raw,"remind me in",rest)){
    size_t at=lower(rest).find(" to ");
    if(at==std::string::npos) return AssistantReply("Use remind me in 5 minutes to check the oven.");
    return add_timer(rest.substr(0,at)+" called "+rest.substr(at+4),context);
  }
  if(take(raw,"remind me to",rest)){
    size_t at=lower(rest).rfind(" in ");
    if(at==std::string::npos){pending_=P_TIMER;pending_label_=rest;return AssistantReply("In how long should I remind you? Try 5 minutes. Keep 360AI open for the alert.");}
    return add_timer(rest.substr(at+4)+" called "+rest.substr(0,at),context);
  }
  if(take(raw,"set a timer",rest) || take(raw,"set timer",rest) || take(raw,"start a timer",rest) || take(raw,"start timer",rest) || take(raw,"timer",rest)) return add_timer(rest,context);
  if(norm=="list notes" || norm=="show notes" || norm=="show my notes" || norm=="show me my notes" || norm=="notes" || norm=="what did i ask you to remember"){
    if(notes_.empty()) return AssistantReply("No saved notes. Try remember to buy batteries.");
    std::string result="Saved notes:";for(size_t i=0;i<notes_.size();++i) result+="\n"+integer_text(notes_[i].id)+": "+notes_[i].text;return AssistantReply(result);
  }
  if(norm=="delete all notes" || norm=="forget all notes" || norm=="clear notes"){pending_=P_CLEAR_NOTES;return AssistantReply("Delete all saved notes? Reply yes or no.");}
  if(take(raw,"delete note",rest) || take(raw,"forget note",rest)){
    int id=0;if(!id_number(rest,id)) return AssistantReply("Use delete note followed by its number, such as delete note 1.");
    for(size_t i=0;i<notes_.size();++i) if(notes_[i].id==id){notes_.erase(notes_.begin()+i);dirty_=true;return AssistantReply("Note "+integer_text(id)+" deleted.");}return AssistantReply("That note does not exist. Try list notes.");
  }
  if(take(raw,"remember that",rest) || take(raw,"remember",rest) || take(raw,"add note",rest) || take(raw,"take a note",rest) || take(raw,"note",rest)) return add_note(rest);
  if(take(raw,"convert",rest)) return convert(rest,false);
  if(take(raw,"and",rest) || take(raw,"what about",rest)){if(!last_from_.empty()) return convert(rest,true);}
  if(take(raw,"calculate",rest) || take(raw,"compute",rest) || take(raw,"work out",rest)) return calculate(rest);
  if((take(raw,"what is",rest) || take(raw,"whats",rest)) && mathish(rest)){
    if(lower(rest).find(" to ")!=std::string::npos || lower(rest).find(" in ")!=std::string::npos) return convert(rest,false);
    return calculate(rest);
  }
  if(mathish(raw)) return calculate(raw);
  if(norm=="joke" || norm=="tell me a joke" || norm=="another joke"){
    static const char* jokes[]={"Why did the computer get cold? It left its Windows open.","I asked my hard drive for a joke. It said it needed a moment to seek.","Why do programmers prefer dark mode? Light attracts bugs.","My calculator and I have problems, but we always work them out."};
    return AssistantReply(jokes[(joke_++)%4]);
  }
  size_t best=0;std::string response;
  for(size_t i=0;i<rules_.size();++i) for(size_t j=0;j<rules_[i].phrases.size();++j)
    if(phrase(norm,rules_[i].phrases[j]) && rules_[i].phrases[j].size()>best){best=rules_[i].phrases[j].size();response=rules_[i].response;}
  for(size_t i=0;i<sizeof(facts)/sizeof(facts[0]);++i){size_t score=match_aliases(norm,facts[i].phrases);if(score>best){best=score;response=facts[i].response;}}
  if(best) return AssistantReply(response);
  std::string first=norm.substr(0,norm.find(' '));const char* commands[]={"calculate","convert","timer","remember","notes","status"};
  if(first.size()>=4) for(int i=0;i<6;++i) if(one_edit(first,commands[i])) return AssistantReply("Did you mean "+std::string(commands[i])+"? Type help for examples. I have not run a command.");
  return AssistantReply("I couldn't match a supported intent. Try help, calculate 8 * 7, convert 5 miles to km, set timer 5 minutes, or remember a note. For open-ended conversation, use a GGUF chat model.");
}

bool Assistant::parse_state(const std::string& text){
  Json j;if(!Json::parse(text,j) || j.type!=Json::OBJECT || j.get("version").type!=Json::NUMBER || j.get("version").number!=1 ||
    j.get("name").type!=Json::STRING || j.get("name").text.size()>64 || !gx_utf8_valid(j.get("name").text) ||
    j.get("notes").type!=Json::ARRAY || j.get("notes").items.size()>32) return false;
  std::vector<Note> notes;int next=1;
  for(size_t i=0;i<j.get("notes").items.size();++i){const Json& item=j.get("notes").items[i];const Json& id=item.get("id");const Json& body=item.get("text");
    if(item.type!=Json::OBJECT || id.type!=Json::NUMBER || id.number<1 || id.number>1000000 || floor(id.number)!=id.number ||
      body.type!=Json::STRING || trim(body.text).empty() || body.text.size()>512 || !gx_utf8_valid(body.text)) return false;
    Note note;note.id=(int)id.number;note.text=body.text;for(size_t n=0;n<notes.size();++n) if(notes[n].id==note.id) return false;
    next=GX_MAX(next,note.id+1);notes.push_back(note);
  }
  name_=j.get("name").text;notes_=notes;next_note_=next;dirty_=false;return true;
}
bool Assistant::load(const char* path){
  std::string text;if(gx_read_text_file(path,text,65536) && parse_state(text)) return true;
  std::string backup=std::string(path)+".bak";return gx_read_text_file(backup.c_str(),text,65536) && parse_state(text);
}
bool Assistant::save(const char* path){
  Json j=Json::object();j["version"]=Json(1.0);j["name"]=Json(name_);j["notes"]=Json::array();
  for(size_t i=0;i<notes_.size();++i){Json n=Json::object();n["id"]=Json((double)notes_[i].id);n["text"]=Json(notes_[i].text);j["notes"].items.push_back(n);}
  std::string next=j.dump()+"\n",previous;Assistant check;if(next.size()>65536 || !check.parse_state(next)) return false;
  if(gx_read_text_file(path,previous,65536) && check.parse_state(previous)){
    std::string backup=std::string(path)+".bak";if(!gx_write_text_file(backup.c_str(),previous)) return false;
  }
  if(!gx_write_text_file(path,next)) return false;dirty_=false;return true;
}
bool Assistant::load_rules(const char* path){
  std::string text;Json j;if(!gx_read_text_file(path,text,65536) || !Json::parse(text,j) || j.type!=Json::OBJECT ||
    j.get("version").type!=Json::NUMBER || j.get("version").number!=1 || j.get("rules").type!=Json::ARRAY || j.get("rules").items.size()>64) return false;
  std::vector<Rule> rules;
  for(size_t i=0;i<j.get("rules").items.size();++i){const Json& n=j.get("rules").items[i];const Json& triggers=n.get("phrases");const Json& response=n.get("response");
    if(n.type!=Json::OBJECT || triggers.type!=Json::ARRAY || triggers.items.empty() || triggers.items.size()>8 || response.type!=Json::STRING ||
      response.text.empty() || response.text.size()>2000 || !gx_utf8_valid(response.text)) return false;
    Rule rule;rule.response=response.text;
    for(size_t p=0;p<triggers.items.size();++p){const Json& t=triggers.items[p];if(t.type!=Json::STRING || t.text.size()>80 || words(t.text).empty() || !gx_utf8_valid(t.text)) return false;rule.phrases.push_back(words(t.text));}
    rules.push_back(rule);
  }
  rules_=rules;return true;
}
