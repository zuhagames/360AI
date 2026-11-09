#include "https.h"
#include "platform.h"
#include "network.h"
#include "bearssl.h"
#include <time.h>
#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif
#include "trust_anchors.h"

static std::string hex_hash(const u8* hash){const char* digits="0123456789abcdef";std::string out;for(int i=0;i<32;++i){out+=digits[hash[i]>>4];out+=digits[hash[i]&15];}return out;}
std::string gx_sha256(const void* data,size_t n){br_sha256_context c;br_sha256_init(&c);br_sha256_update(&c,data,n);u8 hash[32];br_sha256_out(&c,hash);return hex_hash(hash);}
bool gx_file_sha256(const std::string& path,std::string& hash,size_t limit){
  hash.clear();FILE* f=fopen(path.c_str(),"rb");if(!f) return false;br_sha256_context c;br_sha256_init(&c);u8 block[16384];size_t n,total=0;bool ok=true;
  while((n=fread(block,1,sizeof(block),f))!=0){if(n>limit-total){ok=false;break;}total+=n;br_sha256_update(&c,block,n);}
  if(ferror(f)) ok=false;fclose(f);if(!ok) return false;u8 h[32];br_sha256_out(&c,h);hash=hex_hash(h);return true;
}
bool gx_https_url(const std::string& url,std::string& host,std::string& path){
  host.clear();path.clear();if(url.size()>8192 || url.compare(0,8,"https://")!=0) return false;
  for(size_t i=0;i<url.size();++i) if((u8)url[i]<=32 || (u8)url[i]>=127 || url[i]=='\\' || url[i]=='#') return false;
  size_t slash=url.find('/',8);if(slash==std::string::npos) return false;host=url.substr(8,slash-8);path=url.substr(slash);
  return host=="api.github.com" || host=="github.com" || host=="release-assets.githubusercontent.com" || host=="objects.githubusercontent.com";
}
namespace {
struct Network{
  bool ready;
  Network():ready(false){
    int error=0;ready=gx_network_acquire(error);
  }
  ~Network(){if(!ready) return;
    gx_network_release();
  }
};
struct Connection{
  SOCKET socket;double deadline;GxHttpCancel cancel;void* user;
  Connection(GxHttpCancel c,void* u):socket(INVALID_SOCKET),deadline(gx_time_seconds()+180),cancel(c),user(u){}
  ~Connection(){if(socket!=INVALID_SOCKET) closesocket(socket);}
  bool stopped(){return gx_time_seconds()>deadline || (cancel && cancel(user));}
  bool wait(bool write){
    while(!stopped()){fd_set f;FD_ZERO(&f);FD_SET(socket,&f);timeval t;t.tv_sec=0;t.tv_usec=200000;
      int n=select((int)socket+1,write ? 0 : &f,write ? &f : 0,0,&t);if(n>0) return true;if(n<0) return false;}return false;
  }
  static int read(void* ctx,unsigned char* data,size_t n){Connection* c=(Connection*)ctx;if(!c->wait(false)) return -1;int r=recv(c->socket,(char*)data,(int)n,0);return r>0 ? r : -1;}
  static int write(void* ctx,const unsigned char* data,size_t n){Connection* c=(Connection*)ctx;if(!c->wait(true)) return -1;int r=send(c->socket,(const char*)data,(int)n,0);return r>0 ? r : -1;}
  bool connect_host(const std::string& host){
    sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(443);
#ifdef _XBOX
    XNDNS* dns=0;if(XNetDnsLookup(host.c_str(),0,&dns)!=0 || !dns) return false;
    double until=gx_time_seconds()+12;while(((volatile XNDNS*)dns)->iStatus==WSAEINPROGRESS && gx_time_seconds()<until && !stopped()) gx_sleep_ms(50);
    bool ok=dns->iStatus==0 && dns->cina>0;if(ok) addr.sin_addr=dns->aina[0];XNetDnsRelease(dns);if(!ok) return false;
#else
    hostent* dns=gethostbyname(host.c_str());if(!dns || dns->h_addrtype!=AF_INET || dns->h_length!=4) return false;memcpy(&addr.sin_addr,dns->h_addr_list[0],4);
#endif
    socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(socket==INVALID_SOCKET) return false;
#ifdef _XBOX
    BOOL allow=TRUE;if(setsockopt(socket,SOL_SOCKET,0x5801,(const char*)&allow,sizeof(allow))==SOCKET_ERROR) return false;
#endif
#if defined(_XBOX) || defined(_WIN32)
    u_long one=1;if(ioctlsocket(socket,FIONBIO,&one)!=0) return false;
#else
    if(fcntl(socket,F_SETFL,O_NONBLOCK)<0) return false;
#endif
    int result=::connect(socket,(sockaddr*)&addr,sizeof(addr));
    if(result==SOCKET_ERROR){
#if defined(_XBOX) || defined(_WIN32)
      int code=WSAGetLastError();if(code!=WSAEWOULDBLOCK && code!=WSAEINPROGRESS) return false;
#else
      if(errno!=EINPROGRESS) return false;
#endif
      double old=deadline;deadline=GX_MIN(deadline,gx_time_seconds()+12);bool ready=wait(true);deadline=old;if(!ready) return false;
      sockaddr_in connected;
#if defined(_XBOX) || defined(_WIN32)
      int bytes=sizeof(connected);
#else
      socklen_t bytes=sizeof(connected);
#endif
      // XNet has no SO_ERROR socket option. A connected peer confirms that
      // nonblocking connect completed successfully on both host and console.
      if(getpeername(socket,(sockaddr*)&connected,&bytes)!=0) return false;
    }return true;
  }
};
bool entropy(u8* data,size_t size){
#ifdef _XBOX
  return XNetRandom(data,(UINT)size)==0;
#elif defined(_WIN32)
  HCRYPTPROV provider=0;if(!CryptAcquireContext(&provider,0,0,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT|CRYPT_SILENT)) return false;
  bool ok=CryptGenRandom(provider,(DWORD)size,data)!=0;CryptReleaseContext(provider,0);return ok;
#else
  FILE* f=fopen("/dev/urandom","rb");if(!f) return false;bool ok=fread(data,1,size,f)==size;fclose(f);return ok;
#endif
}
bool line(br_sslio_context* io,std::string& out,size_t max){out.clear();char c=0;while(out.size()<max){if(br_sslio_read_all(io,&c,1)<0) return false;if(c=='\n'){if(out.empty() || out[out.size()-1]!='\r') return false;out.resize(out.size()-1);return true;}out+=c;}return false;}
std::string lower(std::string s){for(size_t i=0;i<s.size();++i) if(s[i]>='A' && s[i]<='Z') s[i]+=32;return s;}
bool decimal(const std::string& s,size_t limit,size_t& n){n=0;if(s.empty()) return false;for(size_t i=0;i<s.size();++i){if(s[i]<'0'||s[i]>'9' || n>(limit-(s[i]-'0'))/10) return false;n=n*10+(s[i]-'0');}return n<=limit;}
bool body_bytes(br_sslio_context* io,size_t n,GxHttpSink sink,void* user,Connection& conn){u8 data[16384];while(n){if(conn.stopped()) return false;size_t bytes=GX_MIN(n,sizeof(data));if(br_sslio_read_all(io,data,bytes)<0 || !sink(data,bytes,user)) return false;n-=bytes;}return true;}
}
bool gx_https_get(const std::string& initial,size_t limit,GxHttpSink sink,void* user,GxHttpCancel cancel,std::string& error){
  error.clear();Network network;if(!network.ready){error="Network startup failed. Check the console network settings.";return false;}
  std::string url=initial;gx_init_trust();
  for(int redirect=0;redirect<5;++redirect){
    std::string host,path;if(!gx_https_url(url,host,path)){error="Update URL is not an approved HTTPS host.";return false;}
    Connection conn(cancel,user);if(!conn.connect_host(host)){error="Could not connect to GitHub. Check Internet access and DashLaunch liveblock/livestrong settings.";return false;}
    // Heap allocation keeps the small Xbox worker stack free of TLS buffers.
    br_ssl_client_context* cc=new br_ssl_client_context;br_x509_minimal_context* xc=new br_x509_minimal_context;u8* buffer=new u8[BR_SSL_BUFSIZE_BIDI];
    br_ssl_client_init_full(cc,xc,gx_trust,sizeof(gx_trust)/sizeof(gx_trust[0]));br_ssl_engine_set_versions(&cc->eng,BR_TLS12,BR_TLS12);
    const uint16_t suites[]={BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256};
    br_ssl_engine_set_suites(&cc->eng,suites,sizeof(suites)/sizeof(suites[0]));
    time_t now=time(0);bool ok=now>=1577836800; // Fail closed on an unset console clock.
    if(ok) br_x509_minimal_set_time(xc,(uint32_t)((u64)now/86400+719528),(uint32_t)((u64)now%86400));
    u8 seed[32];if(!entropy(seed,sizeof(seed))) ok=false;else br_ssl_engine_inject_entropy(&cc->eng,seed,sizeof(seed));memset(seed,0,sizeof(seed));
    br_ssl_engine_set_buffer(&cc->eng,buffer,BR_SSL_BUFSIZE_BIDI,1);
    if(ok) ok=br_ssl_client_reset(cc,host.c_str(),0)!=0;
    br_sslio_context io;br_sslio_init(&io,&cc->eng,Connection::read,&conn,Connection::write,&conn);
    std::string request="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: 360AI-Xbox-Updater\r\nAccept: application/vnd.github+json, application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    if(ok) ok=br_sslio_write_all(&io,request.data(),request.size())==0 && br_sslio_flush(&io)==0;
    int status=0;std::string text,location;size_t length=0,header_bytes=0;bool has_length=false,chunked=false,framed=true;
    if(ok){ok=line(&io,text,4096) && (text.compare(0,9,"HTTP/1.1 ")==0 || text.compare(0,9,"HTTP/1.0 ")==0) && text.size()>=12;if(ok) status=atoi(text.c_str()+9);}
    while(ok){ok=line(&io,text,8192);header_bytes+=text.size()+2;if(!ok || header_bytes>32768){ok=false;break;}if(text.empty()) break;
      size_t colon=text.find(':');if(colon==std::string::npos){ok=false;break;}std::string key=lower(text.substr(0,colon)),value=text.substr(colon+1);size_t first=value.find_first_not_of(" \t");value=first==std::string::npos ? "" : value.substr(first);while(!value.empty() && (value[value.size()-1]==' ' || value[value.size()-1]=='\t')) value.resize(value.size()-1);
      if(key=="location") location=value;
      else if(key=="content-length"){if(has_length || !decimal(value,32u*1024u*1024u,length)) framed=false;has_length=true;}
      else if(key=="transfer-encoding"){if(chunked || lower(value)!="chunked") framed=false;chunked=true;}
      else if(key=="content-encoding" && lower(value)!="identity") framed=false;
    }
    if(ok && status==200){ok=framed && !(has_length && chunked) && (has_length || chunked);
      if(ok && has_length) ok=length<=limit && body_bytes(&io,length,sink,user,conn);
      if(ok && chunked){size_t total=0;for(;;){if(!line(&io,text,128)){ok=false;break;}size_t end=text.find(';'),bytes=0;std::string number=text.substr(0,end);if(number.empty() || number.size()>8){ok=false;break;}
          for(size_t i=0;i<number.size();++i){char c=number[i];int digit=c>='0' && c<='9' ? c-'0' : (c>='a' && c<='f' ? c-'a'+10 : (c>='A' && c<='F' ? c-'A'+10 : -1));if(digit<0){ok=false;break;}bytes=bytes*16+digit;}
          if(!ok || bytes>limit-total){ok=false;break;}if(!bytes){size_t trailers=0;do{ok=line(&io,text,4096);trailers+=text.size()+2;}while(ok && !text.empty() && trailers<8192);ok=ok && text.empty();break;}
          if(!body_bytes(&io,bytes,sink,user,conn) || !line(&io,text,2) || !text.empty()){ok=false;break;}total+=bytes;
        }}
    }else if(ok && (status==301 || status==302 || status==303 || status==307 || status==308)){ok=!location.empty();}
    else {ok=false;if(status==403 || status==429) error="GitHub rate limit reached. Try again later.";else if(status) {char b[80];snprintf(b,sizeof(b),"GitHub returned HTTP %d.",status);error=b;}}
    int tls_error=br_ssl_engine_last_error(&cc->eng);delete[] buffer;delete xc;delete cc;
    if(!ok){if(error.empty()){char b[192];snprintf(b,sizeof(b),"HTTPS update failed (TLS %d). Check Internet access, free space and the console date/time.",tls_error);error=b;}return false;}
    if(status==200) return true;url=location;
  }error="Too many update redirects.";return false;
}
