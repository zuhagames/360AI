#ifndef GEMMA_XEX_MCP_H
#define GEMMA_XEX_MCP_H
#include "json.h"
class Engine;
typedef std::string (*gx_completion_fn)(const std::string&,int,void*);
// Returns an HTTP status. 202 indicates a notification with no JSON response.
int gx_mcp_dispatch(const std::string& body, Engine* engine, u32 context,
                    gx_completion_fn complete, void* user, std::string& response);
#endif
