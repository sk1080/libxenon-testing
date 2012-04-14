#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#define BREAKPOINT_INSTR			0x0FE00016 // Breakpoint instruction
#define MAX_BREAKPOINTS				100

typedef struct BREAKPOINT_
{
	unsigned int instruction;
	void* addr;
	int valid;
} BREAKPOINT;

BREAKPOINT* get_breakpoint(void * addr);
void set_breakpoint(unsigned int * addr);
void remove_breakpoint(unsigned int * addr);
void enable_breakpoint(unsigned int * addr);
void disable_breakpoint(unsigned int * addr);








#endif
