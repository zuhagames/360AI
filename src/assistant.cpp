#include "assistant.h"
#include "json.h"
#include "platform.h"
#include "textstream.h"
#include "replies.h"
#include <ctype.h>
#include <float.h>
#include <map>

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
bool valid_text(const std::string& text,bool multiline=false){
  if(!gx_utf8_valid(text)) return false;
  for(size_t i=0;i<text.size();++i){u8 c=(u8)text[i];if(c==127 || (c<32 && !(multiline && (c=='\n' || c=='\t')))) return false;}return true;
}
std::string number(double v){char s[64];snprintf(s,sizeof(s),"%.10g",v==0 ? 0 : v);return s;}
std::string integer_text(int v){char s[32];snprintf(s,sizeof(s),"%d",v);return s;}
std::string command_tail(std::string text){
  for(int pass=0;pass<2;++pass){text=trim(text);
    while(!text.empty() && (text[text.size()-1]=='?' || text[text.size()-1]=='!' || text[text.size()-1]=='.' || text[text.size()-1]==',')) text.resize(text.size()-1);
    if(text.size()>=7 && lower(text.substr(text.size()-7))==" please") text.resize(text.size()-7);
    else if(text.size()>=7 && lower(text.substr(text.size()-7))==",please") text.resize(text.size()-7);
  }
  return trim(text);
}
bool id_number(const std::string& text,int& value){
  std::string s=command_tail(text);
  if(s.empty() || s.size()>7) return false;int n=0;
  for(size_t i=0;i<s.size();++i){if(s[i]<'0' || s[i]>'9') return false;n=n*10+s[i]-'0';}
  if(n<1 || n>1000000) return false;value=n;return true;
}
bool word_at(const std::string& s,size_t pos,std::string& word,size_t& end,bool hyphen=false){
  while(pos<s.size() && (isspace((u8)s[pos]) || (hyphen && s[pos]=='-'))) ++pos;size_t start=pos;
  while(pos<s.size() && isalpha((u8)s[pos])) ++pos;word=lower(s.substr(start,pos-start));end=pos;return pos>start;
}
int number_word(const std::string& word){
  static const char* small[]={"zero","one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
  static const char* tens[]={"twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};
  for(int i=0;i<20;++i) if(word==small[i]) return i;for(int i=0;i<8;++i) if(word==tens[i]) return (i+2)*10;return -1;
}
bool under_hundred(const std::string& s,size_t& pos,double& value){
  std::string word;size_t end=pos;if(!word_at(s,pos,word,end)) return false;int n=number_word(word);if(n<0) return false;pos=end;value=n;
  if(n>=20){size_t next=pos;if(word_at(s,pos,word,next,true)){int ones=number_word(word);if(ones>0 && ones<10){value+=ones;pos=next;}}}return true;
}
bool number_group(const std::string& s,size_t& pos,double& value){
  if(!under_hundred(s,pos,value)) return false;std::string word;size_t end=pos;
  if(value>0 && value<10 && word_at(s,pos,word,end) && word=="hundred"){
    value*=100;pos=end;size_t tail=pos,after=tail;
    if(word_at(s,tail,word,after) && word=="and") tail=after;
    double rest=0;if(under_hundred(s,tail,rest)){value+=rest;pos=tail;}
  }return true;
}
bool english_number(const std::string& s,size_t start,double& value,size_t& end){
  std::string word;size_t first=start;if(!word_at(s,start,word,first)) return false;
  if(word=="a" || word=="an" || word=="half" || word=="quarter"){
    value=word=="half" ? .5 : (word=="quarter" ? .25 : 1);end=first;return true;
  }
  size_t pos=start;if(!number_group(s,pos,value)) return false;
  size_t next=pos;if(word_at(s,pos,word,next) && word=="thousand" && value>0){
    value*=1000;pos=next;size_t tail=pos,after=tail;if(word_at(s,tail,word,after) && word=="and") tail=after;
    double rest=0;if(number_group(s,tail,rest)){value+=rest;pos=tail;}
  }
  next=pos;if(word_at(s,pos,word,next) && word=="point"){
    size_t fraction=next;double divisor=10;int digits=0;
    while(digits<9 && word_at(s,fraction,word,next)){int digit=number_word(word);if(digit<0 || digit>9) break;value+=digit/divisor;divisor*=10;fraction=next;++digits;}
    if(digits) pos=fraction;
  }
  end=pos;return true;
}
std::string numeric_words(const std::string& input){
  std::string output;for(size_t pos=0;pos<input.size();){
    double value=0;size_t end=pos;
    if(isalpha((u8)input[pos]) && (pos==0 || !isalnum((u8)input[pos-1])) && english_number(input,pos,value,end)){
      output+=' ';output+=number(value);output+=' ';pos=end;
    } else output+=input[pos++];
  }return output;
}
void replace_phrase(std::string& text,const char* from,const char* to){
  size_t p=0,n=strlen(from);while((p=text.find(from,p))!=std::string::npos){
    if((p==0 || !isalpha((u8)text[p-1])) && (p+n==text.size() || !isalpha((u8)text[p+n]))){text.replace(p,n,to);p+=strlen(to);}else p+=n;
  }
}
class Calculator {
  std::string input_;size_t p_;int depth_,work_;double ans_;bool have_ans_,too_long_;
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
  Calculator(const std::string& text,double answer,bool have):input_(lower(text)),p_(0),depth_(0),work_(0),ans_(answer),have_ans_(have),too_long_(text.size()>256){
    replace_phrase(input_,"multiplied by","*");replace_phrase(input_,"divided by","/");
    replace_phrase(input_,"plus","+");replace_phrase(input_,"minus","-");replace_phrase(input_,"times","*");
    replace_phrase(input_,"squared","^2");replace_phrase(input_,"percent of","/100*");
    input_=numeric_words(input_);
    while(!input_.empty() && (input_[input_.size()-1]=='?' || input_[input_.size()-1]=='!')) input_.resize(input_.size()-1);
  }
  bool run(double& result){if(input_.empty() || too_long_ || input_.size()>1024){error="Use an expression of 1-256 characters.";return false;}
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
      size_t end=pos;if(!english_number(s,pos,n,end)) return false;pos=end;
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
  if(!finite_number(seconds)) return "unknown";
  if(seconds>604800) return "over 7 days";
  int n=(int)ceil(GX_MAX(0.0,seconds));std::string s;
  if(n>=3600){s+=integer_text(n/3600)+"h ";n%=3600;}if(n>=60){s+=integer_text(n/60)+"m ";n%=60;}
  if(n || s.empty()) s+=integer_text(n)+"s";return trim(s);
}
bool mathish(const std::string& s){
  std::string t=lower(trim(s));if(t.empty()) return false;
  if(t[0]=='-' || t[0]=='+' || t[0]=='(' || t[0]=='.') return true;
  double n=0;size_t end=0;
  if(!scalar(t,n,end)){
    if(end) return true; // Numeric overflow is still a calculation error.
    size_t token=0;while(token<t.size() && isalpha((u8)t[token])) ++token;
    std::string name=t.substr(0,token),tail=trim(t.substr(token));
    if(name=="sqrt" || name=="abs" || name=="round") return !tail.empty() && tail[0]=='(';
    if(name=="ans" || name=="pi" || name=="e") end=token;
    else if(name=="a" || name=="an" || !english_number(t,0,n,end)) return false;
  }
  // A number or function prefix inside a phrase is not an expression:
  // "answer to life", "half life" and "one question" may be taught answers.
  std::string tail=trim(t.substr(end)),unused;if(tail.empty()) return true;
  if(tail[0]=='+' || tail[0]=='-' || tail[0]=='*' || tail[0]=='/' || tail[0]=='%' || tail[0]=='^' || tail[0]=='?' || tail[0]=='!') return true;
  const char* operators[]={"plus","minus","times","multiplied by","divided by","squared","percent of"};
  for(size_t i=0;i<sizeof(operators)/sizeof(operators[0]);++i) if(take(words(tail),operators[i],unused)) return true;
  size_t split=tail.find(" to ");if(split==std::string::npos) split=tail.find(" in ");
  return split!=std::string::npos && unit(tail.substr(0,split))!=0;
}
const char* fixed_reply(const std::string& normalized){
  struct Index {
    std::map<std::string,const char*> entries;
    Index(){for(size_t i=0;i<sizeof(gx_replies)/sizeof(gx_replies[0]);++i){std::string aliases=gx_replies[i].phrases;size_t start=0;
      for(;;){size_t end=aliases.find('|',start);entries[aliases.substr(start,end==std::string::npos ? end : end-start)]=gx_replies[i].answer;
        if(end==std::string::npos) break;start=end+1;}}}
  };
  static const Index index;std::map<std::string,const char*>::const_iterator found=index.entries.find(normalized);
  return found==index.entries.end() ? 0 : found->second;
}
bool new_request(const std::string& raw){
  // Use the same punctuation/word normalization as exact command dispatch.
  // Keep payloads untouched: this only decides whether a follow-up is interrupted.
  const char* commands[]={"time","date","status","help","list","show","cancel","stop","delete","forget","clear","convert","calculate","compute","work out","remember","remind","my name","call me","open","switch","use","set","quick preset","balanced preset","creative preset","what","whats","who","how","export","save chat","joke","another joke","find notes","search notes","read note","edit note","change note","teach","learn","undo","pause","resume","restart","extend","check timer","timer","add note","add time","note","take a note","notes","timers","version","system status","free memory","available models","model list","save conversation","new chat","recent alerts","hi","hello","hey","thanks","thank you","good morning","good evening","tell me a joke","give me","add another minute"};
  std::string normalized=words(raw),rest;if(fixed_reply(normalized)) return true;
  for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);++i) if(take(normalized,commands[i],rest)) return true;return false;
}
const char* help_text="360 Assistant works offline without a model. Try:\n"
  "calculate (12 + 8) * 3 | what is 20 percent of 80\n"
  "convert 5 miles to km | and 10\n"
  "set timer twenty five minutes called pizza | list timers | pause timer pizza\n"
  "resume it | extend timer pizza by 2 minutes | cancel timer 1 | timer history\n"
  "remind me in 10 minutes to check the oven\n"
  "remember to clean the controller | find notes controller | read note 1\n"
  "edit note 1: buy batteries | delete note 1 | undo\n"
  "teach favorite game: Halo 3 | list learned answers | forget answer favorite game\n"
  "open saved answers (controller editor and text-match preview)\n"
  "my name is Martin | what is my name | time | date | status\n"
  "quick preset | balanced preset | creative preset\n"
  "help with GPU | help with models | find models | open updates | joke | export chat | clear chat\n"
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
  {"timer|timers|reminder|reminders","Try set timer 5 minutes called pizza, pause timer pizza, resume it, extend timer pizza by 2 minutes, list timers, or cancel timer 1. Up to eight timers run while 360AI is open. Timers do not survive restarting the app."},
  {"notes|note|remember","Try remember to buy batteries, find notes batteries, read note 1, edit note 1: buy a controller, or delete note 1. Undo restores the last notes/name/taught-answer change. Up to 32 notes save as plain text beside the app. Clearing chat keeps notes."},
  {"teach|learned answers|custom answers","Open Saved answers from Quick tools to add, edit, preview or delete up to 16 local text replies. You can also type teach favorite game: Halo 3. Built-in commands keep priority. Saved answers cannot run commands."},
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

Assistant::Assistant():answer_(0),has_answer_(false),dirty_(false),next_note_(1),next_timer_(1),joke_(0),last_timer_(0),edit_note_(0),pending_timer_action_(TA_STATUS),pending_extra_(0),pending_(P_NONE){}
void Assistant::reset_conversation(){pending_=P_NONE;pending_label_.clear();last_from_.clear();last_to_.clear();has_answer_=false;last_timer_=0;edit_note_=0;}
AssistantReply Assistant::calculate(const std::string& text){
  if(trim(text).empty()){pending_=P_CALC;return AssistantReply("What should I calculate? Try (12 + 8) * 3.");}
  Calculator calc(text,answer_,has_answer_);double result=0;
  if(!calc.run(result)) return AssistantReply("Calculation: "+calc.error);
  answer_=result;has_answer_=true;pending_=P_NONE;return AssistantReply("Result: "+number(result));
}
AssistantReply Assistant::convert(const std::string& text,bool followup){
  std::string input=numeric_words(lower(trim(text)));while(!input.empty() && (input[input.size()-1]=='?' || input[input.size()-1]=='!')) input.resize(input.size()-1);
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
  remember_undo();
  Note n;n.id=next_note_++;n.text=note;notes_.push_back(n);dirty_=true;pending_=P_NONE;
  return AssistantReply("Saved note "+integer_text(n.id)+": "+note);
}
AssistantReply Assistant::add_timer(const std::string& text,const AssistantContext& context){
  std::string input=trim(text),label=pending_label_.empty() ? "Timer" : pending_label_;pending_label_.clear();size_t at=lower(input).find(" called ");
  if(at==std::string::npos) at=lower(input).find(" named ");
  if(at!=std::string::npos){label=trim(input.substr(at+7));input=trim(input.substr(0,at));}
  input=command_tail(input);
  std::string rest;if(take(input,"for",rest)) input=rest;
  double seconds=0;if(!duration(input,seconds)){pending_=P_TIMER;pending_label_=label;return AssistantReply("For how long? Try 5 minutes, 1h 30m, or half an hour. Range: 1 second to 7 days. Optional: called pizza.");}
  if(words(label).empty() || label.size()>64) return AssistantReply("Use a timer label of 1-64 bytes containing a letter or number.");
  if(timers_.size()>=8 || next_timer_>1000000){pending_=P_NONE;return AssistantReply("Eight timers are already active. List timers and cancel one first.");}
  if(!finite_number(context.now) || !finite_number(context.now+seconds)) return AssistantReply("Timer clock unavailable.");
  Timer t;t.id=next_timer_++;t.due=context.now+seconds;t.duration=seconds;t.left=0;t.paused=false;t.label=label;
  timers_.push_back(t);last_timer_=t.id;pending_=P_NONE;
  return AssistantReply("Timer "+integer_text(t.id)+" ("+label+") set for "+remaining(seconds)+". Keep 360AI open; ask list timers to check it.");
}
void Assistant::poll(double now,std::vector<std::string>& out){
  out.clear();if(!finite_number(now)) return;
  for(size_t i=0;i<timers_.size();){if(!timers_[i].paused && now>=timers_[i].due){
    std::string alert="Timer "+integer_text(timers_[i].id)+" finished: "+timers_[i].label;out.push_back(alert);timer_history_.push_back(alert);
    if(timer_history_.size()>8) timer_history_.erase(timer_history_.begin());if(last_timer_==timers_[i].id) last_timer_=0;timers_.erase(timers_.begin()+i);
  }else ++i;}
}
AssistantReply Assistant::timer_action(const std::string& selector,TimerAction action,const AssistantContext& context,double extra){
  pending_=P_NONE;
  if(!finite_number(context.now)) return AssistantReply("Timer clock unavailable.");
  if(timers_.empty()) return AssistantReply("No active timers. Try set timer 5 minutes called tea.");
  std::string selection=lower(command_tail(selector)),rest;
  bool implicit=trim(selector).empty() || selection=="timer";
  if(implicit) selection.clear();
  size_t found=timers_.size(),matches=0;std::string choices;
  // Prefer the complete label before removing optional articles or a trailing
  // "timer". Otherwise labels such as "the tea" and "study timer" disappear.
  for(int pass=0;pass<5;++pass){
    std::string key=words(selection);int id=0;bool numeric=id_number(selection,id),all=selection.empty() && implicit;
    if(key=="it" || key=="that" || key=="this"){id=last_timer_;numeric=true;}
    bool digits=!key.empty();for(size_t n=0;n<key.size();++n) if(key[n]<'0' || key[n]>'9') digits=false;
    if(digits && !numeric) break; // A malformed number, such as -1, is not a label guess.
    for(size_t i=0;i<timers_.size();++i){
      bool match=numeric ? timers_[i].id==id : (all || (!key.empty() && words(timers_[i].label)==key));
      if(match){found=i;++matches;choices+="\n"+integer_text(timers_[i].id)+": "+timers_[i].label;}
    }
    if(matches || numeric || all) break;
    if(take(selection,"the",rest) || take(selection,"my",rest) || take(selection,"timer",rest)) selection=rest;
    else if(selection.size()>6 && selection.substr(selection.size()-6)==" timer") selection=trim(selection.substr(0,selection.size()-6));
    else break;
    if(selection.empty()) break; // Unknown selectors never become "all".
  }
  if(matches!=1){pending_=P_TIMER_SELECT;pending_timer_action_=action;pending_extra_=extra;
    return AssistantReply(matches ? "Which timer? Reply with its exact number or label:"+choices : "I couldn't find that timer. Reply with an exact timer number or label, or cancel.");}
  Timer& t=timers_[found];
  if(!t.paused && context.now>=t.due) return AssistantReply("That timer has already finished. Check timer history after the alert.");
  last_timer_=t.id;std::string name="Timer "+integer_text(t.id)+" ("+t.label+")";
  if(action==TA_CANCEL){timers_.erase(timers_.begin()+found);last_timer_=0;return AssistantReply(name+" cancelled.");}
  if(action==TA_PAUSE){if(!t.paused){t.left=t.due-context.now;t.paused=true;}return AssistantReply(name+" paused with "+remaining(t.left)+" left.");}
  if(action==TA_RESUME){if(t.paused){t.due=context.now+t.left;t.paused=false;}return AssistantReply(name+" running with "+remaining(t.due-context.now)+" left.");}
  if(action==TA_RESTART){t.due=context.now+t.duration;t.paused=false;t.left=0;return AssistantReply(name+" restarted for "+remaining(t.duration)+".");}
  if(action==TA_EXTEND){
    if(!(extra>=1) || !finite_number(extra) || t.duration+extra>604800) return AssistantReply("The total timer duration cannot exceed 7 days.");
    t.duration+=extra;if(t.paused) t.left+=extra;else t.due+=extra;
    return AssistantReply(name+" extended by "+remaining(extra)+"; "+remaining(t.paused ? t.left : t.due-context.now)+" left"+(t.paused ? " (paused)." : "."));
  }
  return AssistantReply(name+(t.paused ? " paused: " : ": ")+remaining(t.paused ? t.left : t.due-context.now)+" left.");
}
AssistantReply Assistant::search_notes(const std::string& query){
  std::string key=words(query);if(key.empty()){pending_=P_NOTE_SEARCH;return AssistantReply("Which words should I find in your notes?");}
  if(key.size()>128) return AssistantReply("Keep a note search within 128 bytes.");
  std::string result;int count=0;
  for(size_t i=0;i<notes_.size();++i){std::string body=words(notes_[i].text);bool match=true;size_t start=0;
    while(start<key.size()){size_t end=key.find(' ',start);std::string term=key.substr(start,end==std::string::npos ? end : end-start);
      if(!phrase(body,term)){match=false;break;}if(end==std::string::npos) break;start=end+1;}
    if(match){++count;result+="\n"+integer_text(notes_[i].id)+": "+notes_[i].text;}
  }
  return AssistantReply(count ? "Matching notes:"+result : "No notes match those words.");
}
AssistantReply Assistant::edit_note(int id,const std::string& text){
  for(size_t i=0;i<notes_.size();++i) if(notes_[i].id==id){
    std::string body=trim(text);if(body.empty()){pending_=P_NOTE_EDIT;edit_note_=id;return AssistantReply("What should note "+integer_text(id)+" say? You can also use edit note "+integer_text(id)+": new text.");}
    if(body.size()>512) return AssistantReply("Keep a note within 512 bytes.");
    if(body!=notes_[i].text){remember_undo();notes_[i].text=body;dirty_=true;}
    return AssistantReply("Note "+integer_text(id)+": "+body);
  }return AssistantReply("That note does not exist. Try list notes.");
}
AssistantReply Assistant::teach(const std::string& text){
  size_t colon=text.find(':');if(colon==std::string::npos) return AssistantReply("Use teach favorite game: Halo 3. The answer is saved locally and can be undone.");
  std::string result;write_answer(text.substr(0,colon),text.substr(colon+1),result);return AssistantReply(result);
}
bool Assistant::answer_phrase(const std::string& text,std::string& key){
  key.clear();if(text.size()>512 || !valid_text(text)) return false;key=words(text);return key.size()>=3 && key.size()<=80;
}
bool Assistant::write_answer(const std::string& phrase_text,const std::string& answer_text,std::string& result){
  std::string key,response=trim(answer_text);
  if(!answer_phrase(phrase_text,key) || response.empty() || response.size()>512 || !valid_text(response,true)){result="Use a phrase of 3-80 bytes and valid answer text of 1-512 bytes.";return false;}
  size_t found=answers_.size();for(size_t i=0;i<answers_.size();++i) if(answers_[i].phrase==key){found=i;break;}
  if(found==answers_.size() && answers_.size()>=16){result="All 16 taught-answer slots are full. Use forget answer followed by a phrase first.";return false;}
  if(found==answers_.size() || answers_[found].response!=response){
    remember_undo();if(found==answers_.size()){Answer a;a.phrase=key;a.response=response;answers_.push_back(a);}else answers_[found].response=response;dirty_=true;
  }result="Learned answer for \""+key+"\". Built-in commands still take priority.";return true;
}
bool Assistant::remove_answer(const std::string& phrase_text,std::string& result){
  std::string key;if(answer_phrase(phrase_text,key)) for(size_t i=0;i<answers_.size();++i) if(answers_[i].phrase==key){remember_undo();answers_.erase(answers_.begin()+i);dirty_=true;result="Forgot the answer for \""+key+"\". Use undo to restore it.";return true;}
  result="No taught answer has that exact phrase. Try list learned answers.";return false;
}
bool Assistant::preview_answer(const std::string& text,std::string& key,std::string& response) const{
  key.clear();response.clear();if(text.size()>512 || !valid_text(text)) return false;std::string norm=words(text);
  for(size_t i=0;i<answers_.size();++i) if(phrase(norm,answers_[i].phrase) && answers_[i].phrase.size()>key.size()){key=answers_[i].phrase;response=answers_[i].response;}
  return !key.empty();
}
std::string Assistant::state_text() const{
  Json j=Json::object();j["version"]=Json(2.0);j["name"]=Json(name_);j["next_note"]=Json((double)next_note_);j["notes"]=Json::array();j["answers"]=Json::array();
  for(size_t i=0;i<notes_.size();++i){Json n=Json::object();n["id"]=Json((double)notes_[i].id);n["text"]=Json(notes_[i].text);j["notes"].items.push_back(n);}
  for(size_t i=0;i<answers_.size();++i){Json a=Json::object();a["phrase"]=Json(answers_[i].phrase);a["response"]=Json(answers_[i].response);j["answers"].items.push_back(a);}
  return j.dump()+"\n";
}
void Assistant::remember_undo(){undo_state_=state_text();if(undo_state_.size()>65536) undo_state_.clear();}
AssistantReply Assistant::respond(const std::string& text,const AssistantContext& context){
  if(text.size()>512){pending_=P_NONE;pending_label_.clear();return AssistantReply("Keep a request within 512 bytes. Any waiting follow-up has been cancelled.");}
  std::string raw=single_line(gx_utf8_clean(text)),rest;
  if(take(raw,"hey 360ai",rest) || take(raw,"hey assistant",rest) || take(raw,"360ai",rest) || take(raw,"assistant",rest)) raw=rest;
  for(int polite=0;polite<2;++polite){if(fixed_reply(words(raw))) break;
    if(take(raw,"please",rest) || take(raw,"can you",rest) || take(raw,"could you",rest) || take(raw,"would you",rest)) raw=rest;else break;}
  std::string norm=words(raw);if(norm.empty()){pending_=P_NONE;pending_label_.clear();return AssistantReply("Ready. Type help for commands.");}
  if(take(raw,"don't forget to",rest) || take(raw,"do not forget to",rest)){pending_=P_NONE;pending_label_.clear();return add_note(rest);}
  if(norm=="dont" || norm=="do not" || norm.compare(0,5,"dont ")==0 || norm.compare(0,7,"do not ")==0 ||
     norm=="never" || norm=="not" || norm.compare(0,6,"never ")==0 || norm.compare(0,4,"not ")==0){
    pending_=P_NONE;pending_label_.clear();return AssistantReply("I have not changed anything. State the single command you want me to run, or type help.");}
  if(norm=="cancel" || norm=="never mind" || norm=="nevermind" || norm=="stop"){
    bool had=pending_!=P_NONE;pending_=P_NONE;pending_label_.clear();return AssistantReply(had ? "Cancelled the follow-up. Existing notes and timers are unchanged." : "No follow-up is waiting. Use cancel timer followed by its number to stop a timer.");}
  if(norm=="help" || norm=="commands" || norm=="show commands" || norm=="what can you do"){pending_=P_NONE;pending_label_.clear();return AssistantReply(help_text);}
  if(norm=="open saved answers" || norm=="open learned answers" || norm=="open answers" || norm=="manage answers"){
    pending_=P_NONE;pending_label_.clear();return AssistantReply("Opening Saved answers. Add, edit and preview your own text replies.",AA_ANSWERS);
  }
  if(norm=="find models" || norm=="get models" || norm=="open model library" || norm=="open hugging face"){
    pending_=P_NONE;pending_label_.clear();return AssistantReply("Opening the Hugging Face model library. Search GGUF text-generation models and choose a file to download.",AA_MODEL_LIBRARY);
  }
  if(norm=="open updates" || norm=="check for updates" || norm=="update 360ai" || norm=="update the app"){
    pending_=P_NONE;pending_label_.clear();return AssistantReply("Opening Updates. Press A to check or download; installation needs a restart.",AA_UPDATES);
  }
  if(pending_==P_CLEAR_NOTES || pending_==P_CLEAR_CHAT || pending_==P_TIMER_CANCEL){
    Pending command=pending_;pending_=P_NONE;
    if(norm=="yes" || norm=="confirm" || norm=="yes please"){
      if(command==P_CLEAR_CHAT) return AssistantReply("Chat cleared. Saved notes and active timers are kept.",AA_CLEAR_CHAT);
      if(command==P_TIMER_CANCEL){timers_.clear();last_timer_=0;return AssistantReply("All timers cancelled.");}
      if(!notes_.empty()){remember_undo();notes_.clear();dirty_=true;}return AssistantReply("All notes deleted. Your saved name is kept. Use undo to restore the last change.");
    }
    if(norm=="no" || norm=="no thanks") return AssistantReply("Cancelled. Nothing was deleted or cleared.");
    // A different request cancels the pending destructive operation.
  } else if(pending_!=P_NONE && new_request(raw)){pending_=P_NONE;pending_label_.clear();}
  else if(pending_!=P_NONE){
    Pending p=pending_;pending_=P_NONE;
    if(p==P_TIMER) return add_timer(raw,context);
    if(p==P_NOTE) return add_note(raw);
    if(p==P_CALC) return calculate(raw);
    if(p==P_TIMER_SELECT) return timer_action(raw,pending_timer_action_,context,pending_extra_);
    if(p==P_NOTE_SEARCH) return search_notes(raw);
    if(p==P_NOTE_EDIT) return edit_note(edit_note_,raw);
    if(p==P_NAME){if(raw.size()>64) return AssistantReply("Keep your name within 64 bytes.");if(name_!=raw){remember_undo();name_=raw;dirty_=true;}return AssistantReply("I'll call you "+name_+".");}
  }
  if(norm=="yes" || norm=="no" || norm=="confirm") return AssistantReply("There is no confirmation waiting. Type help for commands.");
  if(norm=="undo" || norm=="undo last change"){
    if(undo_state_.empty()) return AssistantReply("No saved-data change to undo in this session. Undo applies to notes, your name and taught answers.");
    int highwater=next_note_;std::string snapshot=undo_state_;if(!parse_state(snapshot)) return AssistantReply("The undo snapshot could not be restored.");
    next_note_=GX_MAX(next_note_,highwater);dirty_=true;undo_state_.clear();return AssistantReply("Undid the last saved-data change. Timers and chat are unchanged.");
  }
  // Saved text answers can personalize small talk, while command dispatch below
  // still keeps priority over FAQ matches and cannot be replaced by taught text.
  const char* canned=fixed_reply(norm);
  if(canned){
    size_t best=0;std::string custom;
    for(size_t i=0;i<answers_.size();++i) if(phrase(norm,answers_[i].phrase) && answers_[i].phrase.size()>best){best=answers_[i].phrase.size();custom=answers_[i].response;}
    for(size_t i=0;i<rules_.size();++i) for(size_t p=0;p<rules_[i].phrases.size();++p) if(phrase(norm,rules_[i].phrases[p]) && rules_[i].phrases[p].size()>best){best=rules_[i].phrases[p].size();custom=rules_[i].response;}
    if(best) return AssistantReply(custom);
    std::string response=canned;size_t at=response.find("{name}");if(at!=std::string::npos) response.replace(at,6,name_.empty() ? "there" : name_);
    return AssistantReply(response);
  }
  if(norm=="my name" || norm=="what is my name" || norm=="whats my name" || norm=="who am i") return AssistantReply(name_.empty() ? "You haven't told me your name. Say my name is Martin, for example." : "Your name is "+name_+".");
  if(take(raw,"my name is",rest) || take(raw,"call me",rest)){
    if(rest.empty()){pending_=P_NAME;return AssistantReply("What should I call you?");}
    if(rest.size()>64) return AssistantReply("Keep your name within 64 bytes.");if(name_!=rest){remember_undo();name_=rest;dirty_=true;}return AssistantReply("I'll call you "+name_+".");
  }
  if(norm=="forget my name"){if(!name_.empty()){remember_undo();name_.clear();dirty_=true;}return AssistantReply("Your saved name has been removed.");}
  if(norm=="time" || norm=="what time is it" || norm=="whats the time") return AssistantReply(context.clock.empty() ? "Console clock unavailable." : "Console clock: "+context.clock);
  if(norm=="date" || norm=="what is the date" || norm=="whats todays date" || norm=="what day is it") return AssistantReply(context.date.empty() ? "Console date unavailable." : "Console date: "+context.date);
  if(norm=="status" || norm=="system status" || norm=="free memory" || norm=="version"){
    static const char* profiles[]={"Custom","Quick","Balanced","Creative"};
    return AssistantReply("360AI "+context.version+" | offline keyword assistant\nFree memory: "+number(context.free_bytes/1048576.0)+" MiB\nSaved model preset: "+profiles[GX_CLAMP(context.profile,0,3)]+"\nActive timers: "+integer_text((int)timers_.size())+" | saved notes: "+integer_text((int)notes_.size())+" | taught answers: "+integer_text((int)answers_.size()));
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
    std::string result="Active timers (keep 360AI open):";for(size_t i=0;i<timers_.size();++i) result+="\n"+integer_text(timers_[i].id)+": "+timers_[i].label+" - "+remaining(timers_[i].paused ? timers_[i].left : timers_[i].due-context.now)+(timers_[i].paused ? " (paused)" : "");return AssistantReply(result);
  }
  if(norm=="timer history" || norm=="recent alerts" || norm=="show alerts"){
    if(timer_history_.empty()) return AssistantReply("No completed timer alerts in this app session.");
    std::string result="Recent timer alerts:";for(size_t i=0;i<timer_history_.size();++i) result+="\n"+timer_history_[i];return AssistantReply(result);
  }
  if(norm=="cancel all timers" || norm=="stop all timers"){pending_=P_TIMER_CANCEL;return AssistantReply("Cancel every active timer? Reply yes or no.");}
  if(take(raw,"cancel timer",rest) || take(raw,"stop timer",rest)) return timer_action(rest,TA_CANCEL,context);
  if(norm=="cancel it" || norm=="stop it") return timer_action("it",TA_CANCEL,context);
  if(take(raw,"pause",rest)) return timer_action(rest,TA_PAUSE,context);
  if(take(raw,"resume",rest)) return timer_action(rest,TA_RESUME,context);
  if(take(raw,"restart",rest)) return timer_action(rest,TA_RESTART,context);
  if(take(raw,"timer status",rest) || take(raw,"check timer",rest) || take(raw,"how long on timer",rest)) return timer_action(rest,TA_STATUS,context);
  if(take(raw,"extend",rest) || take(raw,"add time to",rest)){
    size_t by=lower(rest).rfind(" by ");double extra=0;
    if(by==std::string::npos || !duration(command_tail(rest.substr(by+4)),extra)) return AssistantReply("Use extend timer pizza by 2 minutes, or extend it by thirty seconds.");
    return timer_action(rest.substr(0,by),TA_EXTEND,context,extra);
  }
  if(take(raw,"remind me in",rest)){
    size_t at=lower(rest).find(" to ");
    if(at==std::string::npos) return AssistantReply("Use remind me in 5 minutes to check the oven.");
    return add_timer(rest.substr(0,at)+" called "+rest.substr(at+4),context);
  }
  if(take(raw,"give me",rest)){double seconds=0;if(duration(command_tail(rest),seconds)) return add_timer(rest,context);}
  if(norm=="add another minute") return timer_action("it",TA_EXTEND,context,60);
  if(take(raw,"how long until",rest)) return timer_action(rest,TA_STATUS,context);
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
  if(take(raw,"find notes about",rest) || take(raw,"search notes for",rest) || take(raw,"find notes",rest) || take(raw,"search notes",rest)) return search_notes(rest);
  if(take(raw,"read note",rest) || take(raw,"show note",rest)){
    int id=0;if(!id_number(rest,id)) return AssistantReply("Use read note followed by its number, such as read note 1.");
    for(size_t i=0;i<notes_.size();++i) if(notes_[i].id==id) return AssistantReply("Note "+integer_text(id)+": "+notes_[i].text);
    return AssistantReply("That note does not exist. Try list notes.");
  }
  if(take(raw,"edit note",rest) || take(raw,"change note",rest)){
    size_t colon=rest.find(':');int id=0;
    if(!id_number(rest.substr(0,colon),id)) return AssistantReply("Use edit note 1: new text, or edit note 1 for a follow-up.");
    return edit_note(id,colon==std::string::npos ? "" : rest.substr(colon+1));
  }
  if(take(raw,"delete note",rest) || take(raw,"forget note",rest)){
    int id=0;if(!id_number(rest,id)) return AssistantReply("Use delete note followed by its number, such as delete note 1.");
    for(size_t i=0;i<notes_.size();++i) if(notes_[i].id==id){remember_undo();notes_.erase(notes_.begin()+i);dirty_=true;return AssistantReply("Note "+integer_text(id)+" deleted. Use undo to restore it.");}return AssistantReply("That note does not exist. Try list notes.");
  }
  if(norm=="list learned answers" || norm=="show learned answers"){
    if(answers_.empty()) return AssistantReply("No taught answers. Try teach favorite game: Halo 3.");
    std::string result="Taught answers:";for(size_t i=0;i<answers_.size();++i) result+="\n"+answers_[i].phrase+": "+answers_[i].response;return AssistantReply(result);
  }
  if(take(raw,"forget answer",rest) || take(raw,"delete answer",rest)){
    std::string result;remove_answer(command_tail(rest),result);return AssistantReply(result);
  }
  if(take(raw,"teach",rest) || take(raw,"learn",rest)) return teach(rest);
  if(take(raw,"remember that",rest) || take(raw,"remember",rest) || take(raw,"add note",rest) || take(raw,"take a note",rest) || take(raw,"note",rest)) return add_note(rest);
  if(take(raw,"convert",rest)) return convert(rest,false);
  if(take(raw,"and",rest) || take(raw,"what about",rest)){if(!last_from_.empty()) return convert(rest,true);}
  if(take(raw,"calculate",rest) || take(raw,"compute",rest) || take(raw,"work out",rest)) return calculate(rest);
  if((take(raw,"what is",rest) || take(raw,"what's",rest) || take(raw,"whats",rest)) && mathish(rest)){
    if(lower(rest).find(" to ")!=std::string::npos || lower(rest).find(" in ")!=std::string::npos) return convert(rest,false);
    return calculate(rest);
  }
  if(mathish(raw)) return calculate(raw);
  if(norm=="joke" || norm=="tell me a joke" || norm=="another joke"){
    static const char* jokes[]={"Why did the computer get cold? It left its Windows open.","I asked my hard drive for a joke. It said it needed a moment to seek.","Why do programmers prefer dark mode? Light attracts bugs.","My calculator and I have problems, but we always work them out."};
    const char* joke=jokes[joke_];joke_=(joke_+1)%4;return AssistantReply(joke);
  }
  size_t best=0;std::string response;
  for(size_t i=0;i<answers_.size();++i) if(phrase(norm,answers_[i].phrase) && answers_[i].phrase.size()>best){best=answers_[i].phrase.size();response=answers_[i].response;}
  for(size_t i=0;i<rules_.size();++i) for(size_t j=0;j<rules_[i].phrases.size();++j)
    if(phrase(norm,rules_[i].phrases[j]) && rules_[i].phrases[j].size()>best){best=rules_[i].phrases[j].size();response=rules_[i].response;}
  for(size_t i=0;i<sizeof(facts)/sizeof(facts[0]);++i){size_t score=match_aliases(norm,facts[i].phrases);if(score>best){best=score;response=facts[i].response;}}
  if(best) return AssistantReply(response);
  std::string first=norm.substr(0,norm.find(' '));const char* commands[]={"calculate","convert","timer","remember","notes","status"};
  if(first.size()>=4) for(int i=0;i<6;++i) if(one_edit(first,commands[i])) return AssistantReply("Did you mean "+std::string(commands[i])+"? Type help for examples. I have not run a command.");
  return AssistantReply("I couldn't match a supported intent. Try help, calculate 8 * 7, convert 5 miles to km, set timer 5 minutes, or remember a note. For open-ended conversation, use a GGUF chat model.");
}

bool Assistant::parse_state(const std::string& text){
  Json j;if(text.size()>65536 || !Json::parse(text,j) || j.type!=Json::OBJECT || j.get("version").type!=Json::NUMBER || (j.get("version").number!=1 && j.get("version").number!=2) ||
    j.get("name").type!=Json::STRING || j.get("name").text.size()>64 || !valid_text(j.get("name").text) ||
    j.get("notes").type!=Json::ARRAY || j.get("notes").items.size()>32) return false;
  std::vector<Note> notes;int next=1;
  for(size_t i=0;i<j.get("notes").items.size();++i){const Json& item=j.get("notes").items[i];const Json& id=item.get("id");const Json& body=item.get("text");
    if(item.type!=Json::OBJECT || id.type!=Json::NUMBER || id.number<1 || id.number>1000000 || floor(id.number)!=id.number ||
      body.type!=Json::STRING || trim(body.text).empty() || body.text.size()>512 || !valid_text(body.text,true)) return false;
    Note note;note.id=(int)id.number;note.text=body.text;for(size_t n=0;n<notes.size();++n) if(notes[n].id==note.id) return false;
    next=GX_MAX(next,note.id+1);notes.push_back(note);
  }
  std::vector<Answer> answers;
  if(j.get("version").number==2){
    const Json& counter=j.get("next_note");const Json& learned=j.get("answers");
    if(counter.type!=Json::NUMBER || !finite_number(counter.number) || floor(counter.number)!=counter.number || counter.number<next || counter.number>1000001 ||
      learned.type!=Json::ARRAY || learned.items.size()>16) return false;next=(int)counter.number;
    for(size_t i=0;i<learned.items.size();++i){const Json& a=learned.items[i];const Json& key=a.get("phrase");const Json& response=a.get("response");
      if(a.type!=Json::OBJECT || key.type!=Json::STRING || key.text.size()<3 || key.text.size()>80 || !valid_text(key.text) || words(key.text)!=key.text ||
        response.type!=Json::STRING || trim(response.text).empty() || response.text.size()>512 || !valid_text(response.text,true)) return false;
      for(size_t n=0;n<answers.size();++n) if(answers[n].phrase==key.text) return false;
      Answer answer;answer.phrase=key.text;answer.response=response.text;answers.push_back(answer);
    }
  }
  name_=j.get("name").text;notes_=notes;answers_=answers;next_note_=next;dirty_=false;return true;
}
bool Assistant::load(const char* path){
  std::string text;bool loaded=gx_read_text_file(path,text,65536) && parse_state(text);
  if(!loaded){std::string backup=std::string(path)+".bak";loaded=gx_read_text_file(backup.c_str(),text,65536) && parse_state(text);}
  if(loaded){undo_state_.clear();pending_=P_NONE;pending_label_.clear();}return loaded;
}
bool Assistant::save(const char* path){
  if(!write_snapshot(path,state_text())) return false;dirty_=false;return true;
}
bool Assistant::write_snapshot(const char* path,const std::string& next){
  std::string previous;Assistant check;if(!check.parse_state(next)) return false;
  if(gx_read_text_file(path,previous,65536) && check.parse_state(previous)){
    std::string backup=std::string(path)+".bak";if(!gx_write_text_file(backup.c_str(),previous)) return false;
  }
  return gx_write_text_file(path,next);
}
AssistantReply Assistant::write_note(int id,const std::string& text){
  pending_=P_NONE;pending_label_.clear();
  if(text.size()>512) return AssistantReply("Keep a note within 512 bytes.");
  std::string clean=single_line(gx_utf8_clean(text));
  if(trim(clean).empty()) return AssistantReply("A note needs some text.");
  return id ? edit_note(id,clean) : add_note(clean);
}
bool Assistant::load_rules(const char* path){
  std::string text;Json j;if(!gx_read_text_file(path,text,65536) || !Json::parse(text,j) || j.type!=Json::OBJECT ||
    j.get("version").type!=Json::NUMBER || j.get("version").number!=1 || j.get("rules").type!=Json::ARRAY || j.get("rules").items.size()>64) return false;
  std::vector<Rule> rules;
  for(size_t i=0;i<j.get("rules").items.size();++i){const Json& n=j.get("rules").items[i];const Json& triggers=n.get("phrases");const Json& response=n.get("response");
    if(n.type!=Json::OBJECT || triggers.type!=Json::ARRAY || triggers.items.empty() || triggers.items.size()>8 || response.type!=Json::STRING ||
      trim(response.text).empty() || response.text.size()>2000 || !valid_text(response.text,true)) return false;
    Rule rule;rule.response=response.text;
    for(size_t p=0;p<triggers.items.size();++p){const Json& t=triggers.items[p];if(t.type!=Json::STRING || t.text.size()>80 || words(t.text).empty() || !valid_text(t.text)) return false;rule.phrases.push_back(words(t.text));}
    rules.push_back(rule);
  }
  rules_=rules;return true;
}
