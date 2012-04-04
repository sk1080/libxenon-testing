#include <stdio.h>

#include <ppc/timebase.h>

#include "lwipopts.h"
#include "lwip/debug.h"

#include "lwip/timers.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/init.h"
#include "lwip/dhcp.h"
#include "lwip/stats.h"

#include "lwip/ip.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/tcp_impl.h"
#include "lwip/xenon/netif/enet.h"

#if !NO_SYS
#include "lwip/tcpip.h"
#include "lwip/netifapi.h"
#include <threads/mutex.h>
#else
static uint64_t now, last_tcp, last_dhcp_coarse, last_dhcp_fine, now2, dhcp_wait;
#endif

struct netif netif;

ip_addr_t ipaddr, netmask, gateway;

#define NTOA(ip) (int)((ip.addr>>24)&0xff), (int)((ip.addr>>16)&0xff), (int)((ip.addr>>8)&0xff), (int)(ip.addr&0xff)

extern void enet_poll(struct netif *netif);
extern err_t enet_init(struct netif *netif);

void network_poll();
void network_poll_thr(void *nused);
void netif_status_change(struct netif *netif);
void network_print_config();

#if NO_SYS
void network_init_no_sys()
{

#ifdef STATS
	stats_init();
#endif /* STATS */
	printf(" * initializing lwip 1.4.0(no_sys)...\n");

	last_tcp=mftb();
	last_dhcp_fine=mftb();
	last_dhcp_coarse=mftb();

	//printf(" * configuring device for DHCP...\r\n");
	/* Start Network with DHCP */
	IP4_ADDR(&netmask, 255,255,255,255);
	IP4_ADDR(&gateway, 0,0,0,0);
	IP4_ADDR(&ipaddr, 0,0,0,0);

	lwip_init();  //lwip 1.4.0 RC2
	//printf("ok now the NIC\n");

	if (!netif_add(&netif, &ipaddr, &netmask, &gateway, NULL, enet_init, ip_input)){
		printf(" ! netif_add failed!\n");
		return;
	}
	netif_set_default(&netif);

	printf(" * requesting dhcp...");
	//dhcp_set_struct(&netif, &netif_dhcp);
	dhcp_start(&netif);

	dhcp_wait=mftb();
	int i = 0;
	while (netif.ip_addr.addr==0 && i < 60) {
		network_poll();
		now2=mftb();
		if (tb_diff_msec(now2, dhcp_wait) >= 250){
			dhcp_wait=mftb();
			i++;
			if (i % 2)
				printf(".");
		}
	}

	if (netif.ip_addr.addr) {
		printf("success\n");
	} else {
		printf("failed\n");
		printf(" * now assigning a static ip\n");

		IP4_ADDR(&ipaddr, 192, 168, 1, 99);
		IP4_ADDR(&gateway, 192, 168, 1, 1);
		IP4_ADDR(&netmask, 255, 255, 255, 0);
		netif_set_addr(&netif, &ipaddr, &netmask, &gateway);
		netif_set_up(&netif);
	}
}
#endif /* NO_SYS */

void network_init_sys()
{

#ifdef STATS
	stats_init();
#endif /* STATS */
	printf(" * initializing lwip 1.4.0(sys)...\n");

	/* Start Network with DHCP */
	IP4_ADDR(&netmask, 255,255,255,255);
	IP4_ADDR(&gateway, 0,0,0,0);
	IP4_ADDR(&ipaddr, 0,0,0,0);

	tcpip_init(NULL, NULL);  //lwip 1.4.0 RC2
        
	if (netifapi_netif_add(&netif, &ipaddr, &netmask, &gateway, NULL, enet_init, tcpip_input) != ERR_OK){
		printf(" ! netif_add failed!\n");
		return;
	}
	netifapi_netif_set_default(&netif);
        netif_set_status_callback(&netif, netif_status_change);
        
        //start the network_poll thread
        PTHREAD npthr = thread_create(network_poll_thr, 0, 0, 0);
        thread_set_processor(npthr, 5);
        thread_set_priority(npthr, 15);
        thread_close(npthr);

	printf(" * requesting dhcp...");
        enet_context *state = (enet_context*)(&netif)->state;
        state->dhcpWait = 1;
        state->dhcpMut = mutex_create(1);
        mutex_acquire(state->dhcpMut, INFINITE);
	netifapi_dhcp_start(&netif);

        //wait 10 seconds for dhcp to work
        int ret = mutex_acquire(state->dhcpMut, 10000);
        
	if (!ret && !netif_is_up(&netif)) {
            printf(" * DHCP timed out\n");
            printf(" * now assigning a static ip\n");
            netifapi_dhcp_stop(&netif);
            IP4_ADDR(&ipaddr, 192, 168, 1, 99);
            IP4_ADDR(&gateway, 192, 168, 1, 1);
            IP4_ADDR(&netmask, 255, 255, 255, 0);
            netifapi_netif_set_addr(&netif, &ipaddr, &netmask, &gateway);
            netifapi_netif_set_up(&netif);
	} else if (ret && !netif_is_up(&netif)) {
            printf(" * DHCP failed!\n");
            printf(" * aborting network init\n");
        }
}

void netif_status_change(struct netif *netif)
{
    enet_context *state = (enet_context*)netif->state;
    if (state->dhcpWait) {
        //unflag dhcpWait
        state->dhcpWait = 0;
        //release the dhcp mutex
        mutex_release(state->dhcpMut);
    }
    if (netif_is_up(netif)) {
        //network is up, print config
        printf(" * network interface has come up\n");
        network_print_config();
    } else {
        printf(" * network interface has gone down\n");
    }
}

void network_poll()
{

	// sys_check_timeouts();

	enet_poll(&netif);
#if NO_SYS
        now=mftb();
	if (tb_diff_msec(now, last_tcp) >= TCP_TMR_INTERVAL)
	{
		last_tcp=mftb();
		tcp_tmr();
	}

	if (tb_diff_msec(now, last_dhcp_fine) >= DHCP_FINE_TIMER_MSECS)
	{
		last_dhcp_fine=mftb();
		dhcp_fine_tmr();
	}

	if (tb_diff_sec(now, last_dhcp_coarse) >= DHCP_COARSE_TIMER_SECS)
	{
		last_dhcp_coarse=mftb();
		dhcp_coarse_tmr();
	}
#endif /* NO_SYS */
}

void network_poll_thr(void *nused)
{
    while(1) {
        network_poll();
    }
}

void network_print_config()
{
	printf(" * network config: %d.%d.%d.%d / %d.%d.%d.%d\n",
		NTOA(netif.ip_addr), NTOA(netif.netmask));
	printf("\t\tMAC: %02X%02X%02X%02X%02X%02X\n\n",
			netif.hwaddr[0], netif.hwaddr[1], netif.hwaddr[2],
			netif.hwaddr[3], netif.hwaddr[4], netif.hwaddr[5]);
}


