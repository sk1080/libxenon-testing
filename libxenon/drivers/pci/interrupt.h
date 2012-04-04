#include "io.h"

#define PRIO_IPI_4       0x08
#define PRIO_IPI_3       0x10
#define PRIO_SMM         0x14
#define PRIO_SFCX        0x18
#define PRIO_SATA_HDD    0x20
#define PRIO_SATA_CDROM  0x24
#define PRIO_OHCI_0      0x2c
#define PRIO_EHCI_0      0x30
#define PRIO_OHCI_1      0x34
#define PRIO_EHCI_1      0x38
#define PRIO_XMA         0x40
#define PRIO_AUDIO       0x44
#define PRIO_ENET        0x4C
#define PRIO_XPS         0x54
#define PRIO_GRAPHICS    0x58
#define PRIO_PROFILER    0x60
#define PRIO_BIU         0x64
#define PRIO_IOC         0x68
#define PRIO_FSB         0x6c
#define PRIO_IPI_2       0x70
#define PRIO_CLOCK       0x74
#define PRIO_IPI_1       0x78

void pci_irq_init();
void interrupt_unmask(unsigned int irq);
void interrupt_mask(unsigned int irq);
