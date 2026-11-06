#include "app.h"
#include "json.h"

static bool integer(const Json& j,const char* key,int low,int high,int& value){
  if(!j.has(key)) return true;
  const Json& n=j.get(key);
  if(n.type!=Json::NUMBER || n.number<low || n.number>high || floor(n.number)!=n.number) return false;
  value=(int)n.number;return true;
}
static bool boolean(const Json& j,const char* key,bool& value){
  if(!j.has(key)) return true;
  const Json& n=j.get(key);if(n.type!=Json::BOOL) return false;
  value=n.flag;return true;
}
static bool parse_settings(const std::string& text,AppSettings& out){
  Json j;if(!Json::parse(text,j) || j.type!=Json::OBJECT) return false;
  AppSettings s;int version=1;
  if(!integer(j,"version",1,1,version) ||
     !integer(j,"context",256,8192,s.ctx_size) ||
     !integer(j,"top_k",1,200,s.topk) ||
     !integer(j,"debug_level",0,3,s.debug_level) ||
     !integer(j,"backend",0,2,s.backend_mode) ||
     !integer(j,"weight_storage",0,2,s.weight_mode) ||
     !integer(j,"reply_tokens",16,512,s.reply_tokens) ||
     !boolean(j,"server",s.server_on) || !boolean(j,"console_chat",s.chat_on)) return false;
  if(j.has("temperature")){
    const Json& n=j.get("temperature");
    if(n.type!=Json::NUMBER || n.number<0 || n.number>2) return false;
    s.temperature=(f32)n.number;
  }
  out=s;return true;
}
bool AppSettings::load(const char* path){
  std::string text;
  if(gx_read_text_file(path,text,8192) && parse_settings(text,*this)) return true;
  std::string backup=std::string(path)+".bak";
  if(gx_read_text_file(backup.c_str(),text,8192) && parse_settings(text,*this)){
    GX_LOG_WARN("settings: restored previous valid settings from backup");return true;
  }
  return false;
}
bool AppSettings::save(const char* path) const{
  Json j=Json::object();j["version"]=Json(1.0);
  j["context"]=Json((double)ctx_size);j["temperature"]=Json((double)temperature);
  j["top_k"]=Json((double)topk);j["debug_level"]=Json((double)debug_level);
  j["backend"]=Json((double)backend_mode);j["weight_storage"]=Json((double)weight_mode);
  j["reply_tokens"]=Json((double)reply_tokens);
  j["server"]=Json::boolean(server_on);j["console_chat"]=Json::boolean(chat_on);
  AppSettings check;std::string next=j.dump()+"\n",previous;
  if(!parse_settings(next,check)) return false;
  // Only back up a valid configuration; a damaged main file must never
  // replace a valid recovery copy. Replace each file through a temporary file.
  if(gx_read_text_file(path,previous,8192) && parse_settings(previous,check)){
    std::string backup=std::string(path)+".bak";
    if(!gx_write_text_file(backup.c_str(),previous)) return false;
  }
  return gx_write_text_file(path,next);
}
