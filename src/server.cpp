#include "server.h"
#include "app.h"
#include "json.h"
#include "mcp.h"
#include "gpu.h"
#include "version.h"
#include <errno.h>

#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

static volatile bool running=false, stopping=false;
static gx_thread_t thread=0;
static gx_mutex_t engine_mutex=0;
static SOCKET listener=INVALID_SOCKET, client=INVALID_SOCKET;
static Engine* engine=0;
static KVCache* cache=0;
static AppSettings* settings=0;
static char ip[64];
static u32 requests=0;
static int port=80;
static bool net_started=false;
static char diagnostic[192]="server off";
static std::string cors_headers;
static const size_t MAX_HEADERS=16384, MAX_BODY=1024*1024;

static int socket_error(){
#if defined(_XBOX) || defined(_WIN32)
  return WSAGetLastError();
#else
  return errno;
#endif
}
static void net_error(const char* operation,int error){
  snprintf(diagnostic,sizeof(diagnostic),"%s (error %d)",operation,error);
  GX_LOG_ERROR("HTTP: %s",diagnostic);
}
static bool lan_socket(SOCKET s){
#ifdef _XBOX
  // XNET_STARTUP_BYPASS_SECURITY is ignored by the retail XNet library. Set
  // the insecure-LAN option on the listener AND every accepted connection.
  BOOL allow=TRUE;
  if(setsockopt(s,SOL_SOCKET,0x5801,(const char*)&allow,sizeof(allow))==SOCKET_ERROR){
    int error=socket_error();
    GX_LOG_WARN("HTTP: insecure socket option 0x5801 failed: %d",error);
    if(error==WSAEACCES){ net_error("LAN socket permission denied",error); return false; }
    // Devkit libraries may not expose this option; their startup flag suffices.
  }
  if(setsockopt(s,SOL_SOCKET,0x5802,(const char*)&allow,sizeof(allow))==SOCKET_ERROR)
    GX_LOG_WARN("HTTP: socket option 0x5802 unavailable: %d",socket_error());
#endif
  return true;
}

static bool net_init(){
#ifdef _XBOX
  XNetStartupParams p; memset(&p,0,sizeof(p)); p.cfgSizeOfStruct=sizeof(p);
  p.cfgFlags=XNET_STARTUP_BYPASS_SECURITY;
  int error=XNetStartup(&p);
  if(error!=0){ net_error("XNetStartup",error); return false; }
#endif
#if defined(_XBOX) || defined(_WIN32)
  WSADATA wsa; int wsa_error=WSAStartup(MAKEWORD(2,2),&wsa);
  if(wsa_error!=0){
    net_error("WSAStartup",wsa_error);
#ifdef _XBOX
    XNetCleanup();
#endif
    return false;
  }
#endif
  return true;
}
static void net_shutdown(){
  if(!net_started) return;
#if defined(_XBOX) || defined(_WIN32)
  WSACleanup();
#endif
#ifdef _XBOX
  XNetCleanup();
#endif
  net_started=false;
}
static void fetch_ip(){
#ifdef _XBOX
  strcpy(ip,"(network pending)");
  XNADDR addr; memset(&addr,0,sizeof(addr));
  if(XNetGetTitleXnAddr(&addr)!=0 && addr.ina.s_addr) XNetInAddrToString(addr.ina,ip,sizeof(ip));
#else
  strcpy(ip,"127.0.0.1");
#endif
}
static bool send_all(SOCKET s,const std::string& data){
  size_t pos=0;
  while(pos<data.size()){
    int n=send(s,data.data()+pos,(int)GX_MIN((size_t)16384,data.size()-pos),0);
    if(n<=0) return false; pos+=n;
  }
  return true;
}
static void reply(SOCKET s,int code,const std::string& body,const char* type="application/json",const char* extra=""){
  const char* reason=code==200 ? "OK" : code==202 ? "Accepted" : code==400 ? "Bad Request" :
    code==403 ? "Forbidden" : code==404 ? "Not Found" : code==405 ? "Method Not Allowed" :
    code==413 ? "Payload Too Large" : code==503 ? "Service Unavailable" : code==204 ? "No Content" : "Unsupported Media Type";
  char h[512]; snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n%s",
    code,reason,type,(unsigned)body.size(),extra);
  if(send_all(s,std::string(h)+cors_headers+"\r\n")) send_all(s,body);
}
static std::string lower(const std::string& s){
  std::string t=s; for(size_t i=0;i<t.size();++i) if(t[i]>='A'&&t[i]<='Z') t[i]+=32; return t;
}
static std::string trim(const std::string& s){
  size_t a=s.find_first_not_of(" \t"), b=s.find_last_not_of(" \t");
  return a==std::string::npos ? "" : s.substr(a,b-a+1);
}
static bool local_origin(const std::string& origin){
  std::string value=lower(origin); size_t offset=0;
  if(value.compare(0,7,"http://")==0) offset=7;
  else if(value.compare(0,8,"https://")==0) offset=8;
  else return false;
  std::string host=value.substr(offset);
  if(host.empty()) return false;
  if(host.find_first_of("/@?#\\ \t\r\n")!=std::string::npos) return false;
  size_t colon=host[0]=='[' ? host.find(':',host.find(']')) : host.find(':');
  if(colon!=std::string::npos){
    std::string p=host.substr(colon+1); if(p.empty() || p.size()>5) return false;
    for(size_t i=0;i<p.size();++i) if(p[i]<'0'||p[i]>'9') return false;
    if(atoi(p.c_str())>65535) return false;
    host.resize(colon);
  }
  if(host=="localhost" || host=="[::1]") return true;
  unsigned parts[4]; size_t start=0;
  for(int i=0;i<4;++i){
    size_t end=host.find('.',start);
    if((i<3 && end==std::string::npos) || (i==3 && end!=std::string::npos)) return false;
    std::string octet=host.substr(start,end==std::string::npos ? end : end-start);
    if(octet.empty() || octet.size()>3) return false;
    for(size_t j=0;j<octet.size();++j) if(octet[j]<'0'||octet[j]>'9') return false;
    parts[i]=(unsigned)atoi(octet.c_str()); if(parts[i]>255) return false;
    start=end+1;
  }
  return parts[0]==127 || parts[0]==10 || (parts[0]==192 && parts[1]==168) ||
    (parts[0]==172 && parts[1]>=16 && parts[1]<=31);
}
static std::string model_name(){ return engine->cfg().name; }
struct Generation{
  SOCKET socket; bool streaming, chat; bool disconnected; std::string text;
};
static void on_token(int,const char* text,void* user){
  Generation* g=(Generation*)user; g->text+=text;
  if(!g->streaming || g->disconnected) return;
  Json chunk=Json::object(), choice=Json::object();
  chunk["id"]=Json("xex-completion"); chunk["created"]=Json(0.0);
  chunk["model"]=Json(model_name()); chunk["object"]=Json(g->chat ? "chat.completion.chunk" : "text_completion");
  choice["index"]=Json(0.0); choice["finish_reason"]=Json();
  if(g->chat) choice["delta"]["content"]=Json(text); else choice["text"]=Json(text);
  chunk["choices"]=Json::array(); chunk["choices"].items.push_back(choice);
  if(!send_all(g->socket,"data: "+chunk.dump()+"\n\n")) g->disconnected=true;
}
static std::string complete(const std::string& prompt,int max_tokens,void* user){
  Generation local; local.socket=INVALID_SOCKET; local.streaming=false; local.chat=false; local.disconnected=false;
  Generation* g=user ? (Generation*)user : &local;
  std::vector<int> ids,out; engine->tok().encode_prompt(prompt,ids);
  if(engine_mutex) gx_mutex_lock(engine_mutex);
  EngineCfg ec; ec.temperature=settings->temperature; ec.top_k=(u32)settings->topk;
  ec.greedy=ec.temperature<=0.001f; ec.stop=&stopping;
  engine->generate(ids,out,GX_CLAMP(max_tokens,1,512),*cache,ec,on_token,g);
  if(engine_mutex) gx_mutex_unlock(engine_mutex);
  return g->text;
}
static int token_limit(const Json& j){
  const Json& n=j.get("max_tokens");
  if(n.type==Json::NIL) return 256;
  if(n.type!=Json::NUMBER || n.number<1 || n.number>512 || floor(n.number)!=n.number) return -1;
  return (int)n.number;
}
static void handle_client(SOCKET s){
  cors_headers.clear();
#if defined(_XBOX) || defined(_WIN32)
  int timeout=15000;
#else
  struct timeval timeout={15,0};
#endif
  setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
  setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
  std::string request; char buffer[4096]; size_t end=std::string::npos;
  while(end==std::string::npos){
    int n=recv(s,buffer,sizeof(buffer),0); if(n<=0) return;
    request.append(buffer,n); end=request.find("\r\n\r\n");
    if((end==std::string::npos && request.size()>MAX_HEADERS) || (end!=std::string::npos && end>MAX_HEADERS)){
      reply(s,413,"{\"error\":\"headers too large\"}"); return;
    }
  }
  size_t first=request.find("\r\n"), sp1=request.find(' '), sp2=request.find(' ',sp1+1);
  if(first==std::string::npos || sp1>=first || sp2>=first){ reply(s,400,"{}"); return; }
  std::string method=request.substr(0,sp1), path=request.substr(sp1+1,sp2-sp1-1);
  std::map<std::string,std::string> headers;
  for(size_t p=first+2;p<end;){
    size_t next=request.find("\r\n",p), colon=request.find(':',p);
    if(next==std::string::npos || colon>=next){ reply(s,400,"{}"); return; }
    std::string key=lower(request.substr(p,colon-p));
    if(headers.find(key)!=headers.end()){ reply(s,400,"{}"); return; }
    headers[key]=trim(request.substr(colon+1,next-colon-1)); p=next+2;
  }
  // Permit local OpenAI web clients, while rejecting public/opaque origins.
  // MCP stays restricted to native clients and does not enable browser CORS.
  if(headers.find("origin")!=headers.end()){
    if(path=="/mcp" || !local_origin(headers["origin"])){
      reply(s,403,"{\"error\":\"use a native client or a localhost/private-LAN web client\"}"); return;
    }
    cors_headers="Access-Control-Allow-Origin: "+headers["origin"]+"\r\nVary: Origin\r\n";
  }
  if(method=="OPTIONS" && path!="/mcp"){
    reply(s,204,"","application/json","Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, API-Key, X-API-Key, OpenAI-Organization, OpenAI-Project\r\n");
    return;
  }
  if(headers.find("transfer-encoding")!=headers.end()){ reply(s,400,"{\"error\":\"Content-Length required\"}"); return; }
  size_t length=0;
  if(headers.find("content-length")!=headers.end()){
    const std::string& v=headers["content-length"];
    if(v.empty()){ reply(s,400,"{}"); return; }
    for(size_t i=0;i<v.size();++i){
      if(v[i]<'0'||v[i]>'9'){ reply(s,400,"{}"); return; }
      length=length*10+v[i]-'0'; if(length>MAX_BODY){ reply(s,413,"{}"); return; }
    }
  }
  size_t start=end+4;
  while(request.size()-start<length){
    int n=recv(s,buffer,(int)GX_MIN(sizeof(buffer),length-(request.size()-start)),0); if(n<=0) return;
    request.append(buffer,n);
  }
  std::string body=request.substr(start,length);
  GX_LOG_ALL("HTTP: %s %s (%u bytes)",method.c_str(),path.c_str(),(unsigned)body.size());
  if(path=="/health" || path=="/"){
    if(method!="GET"){ reply(s,405,"{\"error\":\"use GET\"}"); return; }
    Json health=Json::object(); health["application"]=Json("360AI");
    health["version"]=Json(GX_VERSION);
    health["status"]=Json(engine && engine->ready() ? (engine->storage_ok() ? "ok" : "storage_error") : "no model");
    health["backend"]=Json(gx_gpu_status()); health["gpu_diagnostic"]=Json(gx_gpu_diagnostic());
    health["backend_selection"]=Json(engine ? engine->backend_note() : "no model");
    health["weight_storage"]=Json(engine && engine->disk_streaming() ? "HDD/USB stream (8 MB cache)" : "RAM");
    health["storage_ok"]=Json::boolean(engine && engine->storage_ok());
    health["api_base"]=Json("/v1"); health["completions"]=Json("POST /v1/completions with a JSON prompt");
    reply(s,200,health.dump()); return;
  }
  if(path=="/mcp"){
    if(method!="POST"){ reply(s,405,"{}","application/json","Allow: POST\r\n"); return; }
    if(lower(headers["content-type"]).find("application/json")!=0){ reply(s,415,"{}"); return; }
    if(headers.find("mcp-protocol-version")!=headers.end() && headers["mcp-protocol-version"]!="2025-06-18"){
      reply(s,400,"{\"error\":\"supported MCP version: 2025-06-18\"}"); return;
    }
    std::string result;
    int status=gx_mcp_dispatch(body,engine,cache->seq_len,complete,0,result);
    reply(s,status,result); return;
  }
  if(path=="/v1/models"){
    if(method!="GET"){ reply(s,405,"{}"); return; }
    Json r=Json::object(), model=Json::object();
    r["object"]=Json("list"); r["data"]=Json::array();
    model["id"]=Json(model_name()); model["object"]=Json("model"); model["owned_by"]=Json("360AI");
    r["data"].items.push_back(model); reply(s,200,r.dump()); return;
  }
  bool chat=path=="/v1/chat/completions";
  if(!chat && path!="/v1/completions"){ reply(s,404,"{}"); return; }
  if(method!="POST"){
    reply(s,405,"{\"error\":{\"message\":\"Use POST with Content-Type: application/json and a prompt (or messages for chat). Use GET /v1/models to test connectivity.\",\"type\":\"invalid_request_error\"}}","application/json","Allow: POST\r\n");
    return;
  }
  Json input;
  if(!Json::parse(body,input) || input.type!=Json::OBJECT){ reply(s,400,"{\"error\":\"invalid JSON\"}"); return; }
  std::string prompt;
  if(chat){
    const Json& msgs=input.get("messages");
    if(msgs.type!=Json::ARRAY || msgs.items.empty()){ reply(s,400,"{\"error\":\"messages required\"}"); return; }
    std::vector<std::pair<std::string,std::string> > turns;
    for(size_t i=0;i<msgs.items.size();++i){
      const Json& msg=msgs.items[i]; const Json& role=msg.get("role"); const Json& content=msg.get("content");
      if(role.type!=Json::STRING || content.type!=Json::STRING ||
         (role.text!="system" && role.text!="user" && role.text!="assistant" && role.text!="tool")){
        reply(s,400,"{\"error\":\"expected text messages\"}"); return;
      }
      turns.push_back(std::make_pair(role.text,content.text));
    }
    prompt=engine->tok().chat_prompt(turns);
  } else {
    if(input.get("prompt").type!=Json::STRING){ reply(s,400,"{\"error\":\"prompt required\"}"); return; }
    prompt=input.get("prompt").text;
  }
  int limit=token_limit(input);
  if(limit<0 || prompt.empty()){ reply(s,400,"{\"error\":\"invalid prompt or max_tokens (1-512)\"}"); return; }
  if(!engine->storage_ok()){reply(s,503,"{\"error\":{\"message\":\"Disk read failed. Check HDD/USB and reload the model.\",\"type\":\"storage_error\"}}");return;}
  Generation gen; gen.socket=s; gen.chat=chat; gen.streaming=input.get("stream").type==Json::BOOL && input.get("stream").flag;
  gen.disconnected=false;
  if(gen.streaming) send_all(s,"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: close\r\n"+cors_headers+"\r\n");
  std::string output=complete(prompt,limit,&gen);
  if(!engine->storage_ok()){
    const std::string error="{\"error\":{\"message\":\"Disk read failed. Check HDD/USB and reload the model.\",\"type\":\"storage_error\"}}";
    if(gen.streaming) send_all(s,"data: "+error+"\n\ndata: [DONE]\n\n"); else reply(s,503,error);
    return;
  }
  Json r=Json::object(), choice=Json::object();
  r["id"]=Json("xex-completion"); r["created"]=Json(0.0); r["model"]=Json(model_name());
  r["object"]=Json(chat ? (gen.streaming ? "chat.completion.chunk" : "chat.completion") : "text_completion");
  choice["index"]=Json(0.0); choice["finish_reason"]=Json("stop");
  if(chat){
    if(gen.streaming) choice["delta"]=Json::object();
    else { choice["message"]["role"]=Json("assistant"); choice["message"]["content"]=Json(output); }
  } else choice["text"]=Json(gen.streaming ? "" : output);
  r["choices"]=Json::array(); r["choices"].items.push_back(choice);
  if(gen.streaming) send_all(s,"data: "+r.dump()+"\n\ndata: [DONE]\n\n"); else reply(s,200,r.dump());
}
static void server_thread(void*){
  while(running){
    sockaddr_in addr;
#if defined(_XBOX) || defined(_WIN32)
    int length=sizeof(addr);
#else
    socklen_t length=sizeof(addr);
#endif
    SOCKET s=accept(listener,(sockaddr*)&addr,&length);
    if(s==INVALID_SOCKET){ if(!running) break; gx_sleep_ms(50); continue; }
    client=s;
    if(running && lan_socket(s)) handle_client(s);
    client=INVALID_SOCKET; closesocket(s); ++requests;
  }
}
bool server_start(Engine* eng,KVCache* kv,AppSettings* set,gx_mutex_t mutex){
  if(running) return true;
  engine=eng; cache=kv; settings=set; engine_mutex=mutex; stopping=false;
  if(!net_init()) return false; net_started=true; fetch_ip();
  listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(listener==INVALID_SOCKET){ net_error("socket",socket_error()); net_shutdown(); return false; }
  if(!lan_socket(listener)){ server_stop(); return false; }
  int opt=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
  sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_ANY);
  port=80; addr.sin_port=htons((u16)port);
  if(bind(listener,(sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){
    port=8080; addr.sin_port=htons((u16)port);
    if(bind(listener,(sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){ net_error("bind ports 80/8080",socket_error()); server_stop(); return false; }
  }
  if(listen(listener,4)==SOCKET_ERROR){ net_error("listen",socket_error()); server_stop(); return false; }
  running=true; requests=0; thread=gx_thread_begin(server_thread,0);
  if(!thread){ net_error("server thread",-1); server_stop(); return false; }
  strcpy(diagnostic,"listening");
  GX_LOG_ALL("HTTP + MCP listening on %s:%d",ip,port); return true;
}
void server_stop(){
  if(!running && listener==INVALID_SOCKET && !net_started) return;
  stopping=true; running=false;
  // Interrupt a slow request, then let the worker own its socket close.
  if(client!=INVALID_SOCKET) shutdown(client,2);
  if(listener!=INVALID_SOCKET){ shutdown(listener,2); closesocket(listener); listener=INVALID_SOCKET; }
  if(thread){ gx_thread_join(thread); thread=0; }
  net_shutdown();
}
bool server_running(){ return running; }
std::string server_status_text(){
  if(!running) return std::string("HTTP: ")+diagnostic;
  fetch_ip();
  char text[128]; snprintf(text,sizeof(text),"HTTP/MCP %s:%d (%u req)",ip,port,requests); return text;
}
