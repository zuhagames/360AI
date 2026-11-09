#ifndef GX_HTTPS_H
#define GX_HTTPS_H
#include "common.h"
// Fixed HTTPS allowlist, certificate/hostname/time verification, bounded body,
// redirects, HTTP framing and deadline. The worker owns all networking state.
typedef bool (*GxHttpSink)(const void*,size_t,void*);
typedef bool (*GxHttpCancel)(void*);
bool gx_https_url(const std::string& url,std::string& host,std::string& path);
bool gx_https_get(const std::string& url,size_t limit,GxHttpSink sink,void* user,
                  GxHttpCancel cancel,std::string& error);
std::string gx_sha256(const void* data,size_t size);
bool gx_file_sha256(const std::string& path,std::string& hash,size_t limit);
#endif
