/*
 * Xenon pci interrupt setup,
 *
 * Adapted from the xenon linux driver by: Felix Domke <tmbinc@elitedvb.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License v2
 * as published by the Free Software Foundation.
 */

#include "interrupt.h"

/* bridge (PCI) IRQ -> CPU IRQ */
static int xenon_pci_irq_map[] = {
		PRIO_CLOCK, PRIO_SATA_CDROM, PRIO_SATA_HDD, PRIO_SMM,
		PRIO_OHCI_0, PRIO_EHCI_0, PRIO_OHCI_1, PRIO_EHCI_1,
		-1, -1, PRIO_ENET, PRIO_XMA,
		PRIO_AUDIO, PRIO_SFCX, -1, -1};

static long bridge_base = 0xea000000;
static long iic_base = 0x20050000;

uint32_t eread32(long addr)
{
    return read32(addr);
}
uint32_t eread32n(long addr)
{
    return read32n(addr);
}

void ewrite32(long addr, uint32_t val)
{
    write32(addr, val);
    asm volatile("eieio");
}
void ewrite32n(long addr, uint32_t val)
{
    write32n(addr, val);
    asm volatile("eieio");
}
void ewrite64n(long addr, uint64_t val)
{
    *(volatile uint64_t*)addr = val;
    asm volatile("eieio");
}

void pci_irq_init()
{
    ewrite32(0xEA000000, 0x00000000); // pci
    ewrite32(0xEA000004, 0x40000000); // pci
    ewrite32(0xE1040074, 0x40000000); // northbridge interrupt
    ewrite32(0xE1040078, 0xEA000050); // northbridge interrupt
    ewrite32(0xEA00000C, 0x00000000); // pci
    ewrite32(0xEA000000, 0x00000003); // pci
    int i;
    for (i=0; i<0x10; ++i) //Disable all interrupts until needed
	ewrite32(0xEA000000 + 0x10 + i * 4,0); 
}

static void disconnect_pci_irq(int prio)
{
	int i;

	for (i=0; i<0x10; ++i)
		if (xenon_pci_irq_map[i] == prio)
			ewrite32(bridge_base + 0x10 + i * 4, 0);
}

	/* connects an PCI IRQ to CPU #0 */
static void connect_pci_irq(int prio)
{
	int i;

	for (i=0; i<0x10; ++i)
		if (xenon_pci_irq_map[i] == prio)
			ewrite32(bridge_base + 0x10 + i * 4,0x0800180 | (xenon_pci_irq_map[i]/4));
}

void interrupt_mask(unsigned int irq)
{
	disconnect_pci_irq(irq);
}

void interrupt_unmask(unsigned int irq)
{
	int i;
	connect_pci_irq(irq);
	for (i=0; i<6; ++i)
		ewrite64n(iic_base + i * 0x1000 + 0x68, 0);
}
