#ifndef __include_network_h
#define __include_network_h

#ifdef __cplusplus
extern "C" {
#endif

#include <lwipopts.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>

#if NO_SYS
#define network_init()  network_init_no_sys()
#else
#define network_init()  network_init_sys()
#endif

void network_init_no_sys();
void network_init_sys();
void network_poll();
void network_print_config();

extern struct netif netif;

#ifdef __cplusplus
}
#endif

#endif