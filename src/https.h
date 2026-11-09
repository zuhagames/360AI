#ifndef GX_HTTPS_H
#define GX_HTTPS_H
#include "common.h"
// Fixed HTTPS allowlist, certificate/hostname/time verification, bounded body,
// redirects, HTTP framing and deadline. The worker owns all networking state.
typedef bool (*GxHttpSink)(const void*,size_t,void*);
typedef bool (*GxHttpCancel)(void*);
enum GxHttpPolicy {GX_HTTP_GITHUB,GX_HTTP_HUGGINGFACE};
bool gx_https_url(const std::string& url,std::string& host,std::string& path,GxHttpPolicy policy=GX_HTTP_GITHUB);
bool gx_https_get(const std::string& url,u64 limit,GxHttpSink sink,void* user,
                  GxHttpCancel cancel,std::string& error,GxHttpPolicy policy=GX_HTTP_GITHUB,std::string* next=0);
std::string gx_sha256(const void* data,size_t size);
bool gx_file_sha256(const std::string& path,std::string& hash,u64 limit,GxHttpCancel cancel=0,void* user=0);
#endif
