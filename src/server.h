#ifndef GEMMA_XEX_SERVER_H
#define GEMMA_XEX_SERVER_H

#include "common.h"
#include "engine.h"
#include "platform.h"

struct AppSettings;

bool server_start(Engine* eng, KVCache* kv, AppSettings* settings, gx_mutex_t mutex);
void server_stop();
bool server_running();
std::string server_status_text();

#endif
