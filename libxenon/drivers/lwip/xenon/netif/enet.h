/* 
 * File:   enet.h
 * Author: amorton
 *
 * Created on January 6, 2012, 8:28 AM
 */

#ifndef ENET_H
#define	ENET_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "lwip/opt.h"
#include <pci/io.h>
#include <threads/mutex.h>

#define TX_DESCRIPTOR_NUM 0x10
#define RX_DESCRIPTOR_NUM 0x10
#define MTU 1528
#define MEM(x) (0x80000000|(long)(x))

typedef struct _enet_context
{
	volatile uint32_t *rx_descriptor_base;
	void *rx_receive_base;
	int rx_descriptor_rptr;

	volatile uint32_t *tx_descriptor_base;
	int tx_descriptor_wptr;
	void *tx_buffer_base;

	struct eth_addr *ethaddr;
        
#if !NO_SYS
        int dhcpWait;
        MUTEX *dhcpMut;
#endif /* !NO_SYS */
} enet_context;

#ifdef	__cplusplus
}
#endif

#endif	/* ENET_H */

