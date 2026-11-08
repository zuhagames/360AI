#include "json.h"
#include "textstream.h"
#include <float.h>

const Json& Json::get(const char* key) const{
  static const Json nil;
  std::map<std::string,Json>::const_iterator it=fields.find(key);
  return it==fields.end() ? nil : it->second;
}
static std::string quoted(const std::string& s){
  std::string o="\"";
  for(size_t i=0;i<s.size();++i){
    unsigned char c=(unsigned char)s[i];
    if(c=='"'||c=='\\'){ o+='\\'; o+=c; }
    else if(c=='\b') o+="\\b";
    else if(c=='\f') o+="\\f";
    else if(c=='\n') o+="\\n";
    else if(c=='\r') o+="\\r";
    else if(c=='\t') o+="\\t";
    else if(c<32){ char b[7]; snprintf(b,sizeof(b),"\\u%04x",(unsigned)c); o+=b; }
    else o+=c;
  }
  return o+'"';
}
std::string Json::dump() const{
  switch(type){
    case STRING: return quoted(text);
    case NUMBER: { char b[64]; snprintf(b,sizeof(b),"%.17g",number); return b; }
    case BOOL: return flag ? "true" : "false";
    case ARRAY: {
      std::string s="["; for(size_t i=0;i<items.size();++i){ if(i) s+=','; s+=items[i].dump(); } return s+"]";
    }
    case OBJECT: {
      std::string s="{";
      for(std::map<std::string,Json>::const_iterator it=fields.begin();it!=fields.end();++it){
        if(it!=fields.begin()) s+=','; s+=quoted(it->first)+":"+it->second.dump();
      }
      return s+"}";
    }
    default: return "null";
  }
}
namespace {
struct Parser{
  const std::string& s; size_t p, nodes;
  Parser(const std::string& input):s(input),p(0),nodes(0){}
  void ws(){ while(p<s.size() && (s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t')) ++p; }
  bool eat(char c){ ws(); if(p<s.size() && s[p]==c){ ++p; return true; } return false; }
  int hex(char c){ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }
  bool hex4(u32& c){ c=0; for(int i=0;i<4;++i){ if(p>=s.size()) return false; int n=hex(s[p++]); if(n<0) return false; c=(c<<4)|n; } return true; }
  void utf8(std::string& o,u32 c){
    if(c<128) o+=(char)c;
    else if(c<2048){ o+=(char)(192|(c>>6)); o+=(char)(128|(c&63)); }
    else if(c<65536){ o+=(char)(224|(c>>12)); o+=(char)(128|((c>>6)&63)); o+=(char)(128|(c&63)); }
    else { o+=(char)(240|(c>>18)); o+=(char)(128|((c>>12)&63)); o+=(char)(128|((c>>6)&63)); o+=(char)(128|(c&63)); }
  }
  bool string(std::string& o){
    if(!eat('"')) return false; o.clear();
    while(p<s.size()){
      unsigned char c=(unsigned char)s[p++];
      if(c=='"') return true;
      if(c<32) return false;
      if(c!='\\'){ o+=c; continue; }
      if(p>=s.size()) return false;
      c=(unsigned char)s[p++];
      switch(c){
        case '"': case '\\': case '/': o+=c; break;
        case 'b': o+='\b'; break; case 'f': o+='\f'; break;
        case 'n': o+='\n'; break; case 'r': o+='\r'; break; case 't': o+='\t'; break;
        case 'u': {
          u32 cp; if(!hex4(cp)) return false;
          if(cp>=0xd800 && cp<=0xdbff){
            if(p+2>s.size() || s[p]!='\\' || s[p+1]!='u') return false; p+=2;
            u32 low; if(!hex4(low)||low<0xdc00||low>0xdfff) return false;
            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
          } else if(cp>=0xdc00 && cp<=0xdfff) return false;
          utf8(o,cp); break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool value(Json& j,int depth){
    if(depth>32 || ++nodes>32768) return false; ws(); if(p>=s.size()) return false;
    char c=s[p];
    if(c=='"'){ j.type=Json::STRING; return string(j.text); }
    if(c=='{' || c=='['){
      ++p; j.type=c=='{' ? Json::OBJECT : Json::ARRAY; char end=c=='{' ? '}' : ']';
      if(eat(end)) return true;
      do {
        std::string key; if(c=='{' && (!string(key)||!eat(':')||j.fields.find(key)!=j.fields.end())) return false;
        Json child; if(!value(child,depth+1)) return false;
        if(c=='{') j.fields[key]=child; else j.items.push_back(child);
        if(eat(end)) return true;
      } while(eat(','));
      return false;
    }
    if(s.compare(p,4,"null")==0){ p+=4; return true; }
    if(s.compare(p,4,"true")==0){ p+=4; j=Json::boolean(true); return true; }
    if(s.compare(p,5,"false")==0){ p+=5; j=Json::boolean(false); return true; }
    size_t start=p;
    if(s[p]=='-') ++p;
    if(p>=s.size()) return false;
    if(s[p]=='0') ++p;
    else { if(s[p]<'1'||s[p]>'9') return false; while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; }
    if(p<s.size()&&s[p]=='.'){
      size_t digits=++p; while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; if(p==digits) return false;
    }
    if(p<s.size()&&(s[p]=='e'||s[p]=='E')){
      ++p; if(p<s.size()&&(s[p]=='+'||s[p]=='-')) ++p;
      size_t digits=p; while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; if(p==digits) return false;
    }
    j.type=Json::NUMBER; j.number=strtod(s.substr(start,p-start).c_str(),0);
    return j.number<=DBL_MAX && j.number>=-DBL_MAX;
  }
};
}
bool Json::parse(const std::string& s,Json& out){
  if(s.size()>1024*1024 || !gx_utf8_valid(s)) return false;
  Parser p(s); Json j; if(!p.value(j,0)) return false; p.ws(); if(p.p!=s.size()) return false;
  out=j; return true;
}
