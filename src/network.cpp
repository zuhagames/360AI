#include "network.h"
#include "platform.h"
#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#elif defined(_WIN32)
#include <winsock2.h>
#endif
static gx_mutex_t network_mutex=gx_mutex_create();
static unsigned network_users=0;
bool gx_network_acquire(int& error){
  error=0;if(!network_mutex) return false;gx_mutex_lock(network_mutex);
  if(network_users){++network_users;gx_mutex_unlock(network_mutex);return true;}
#ifdef _XBOX
  XNetStartupParams p;memset(&p,0,sizeof(p));p.cfgSizeOfStruct=sizeof(p);p.cfgFlags=XNET_STARTUP_BYPASS_SECURITY;
  error=XNetStartup(&p);if(error){gx_mutex_unlock(network_mutex);return false;}
#endif
#if defined(_XBOX) || defined(_WIN32)
  WSADATA w;error=WSAStartup(MAKEWORD(2,2),&w);
  if(error){
#ifdef _XBOX
    XNetCleanup();
#endif
    gx_mutex_unlock(network_mutex);return false;
  }
#endif
  network_users=1;gx_mutex_unlock(network_mutex);return true;
}
void gx_network_release(){
  if(!network_mutex) return;gx_mutex_lock(network_mutex);
  if(network_users && --network_users==0){
#if defined(_XBOX) || defined(_WIN32)
    WSACleanup();
#endif
#ifdef _XBOX
    XNetCleanup();
#endif
  }gx_mutex_unlock(network_mutex);
}
