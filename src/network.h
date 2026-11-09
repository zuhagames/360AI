#ifndef GX_NETWORK_H
#define GX_NETWORK_H
// Shared process-wide XNet/Winsock lifetime for the API server and updater.
bool gx_network_acquire(int& error);
void gx_network_release();
#endif
