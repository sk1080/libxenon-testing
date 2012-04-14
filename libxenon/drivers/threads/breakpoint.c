#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <ppc/cache.h>
#include <threads/threads.h>

#include <threads/breakpoint.h>

BREAKPOINT breakpoints[MAX_BREAKPOINTS];


BREAKPOINT* get_breakpoint(void * addr)
{
	int i;
	for(i = 0; i < MAX_BREAKPOINTS; i++)
	{
		if(breakpoints[i].valid && breakpoints[i].addr == addr)
			return &breakpoints[i];
	}



	return NULL;
}

void set_breakpoint(unsigned int * addr)
{
	//printf("Setting breakpoint at 0x%X\n", addr);

	if(get_breakpoint(addr) != NULL)
	{
		printf("Warning: tried to add a breakpoint where one already exists\n");
		return;
	}
	BREAKPOINT * bp = NULL;
	int i;
	for(i = 0; i < MAX_BREAKPOINTS; i++)
	{
		if(!breakpoints[i].valid)
		{
			bp = &breakpoints[i];
			break;
		}
	}

	if(bp == NULL)
	{
		printf("Error: no free breakpoints\n");
		return;
	}

	bp->valid = 1;
	bp->addr = addr;
	bp->instruction = *(addr);
	*(addr) = BREAKPOINT_INSTR;

	memicbi(addr, 4);

}

void remove_breakpoint(unsigned int * addr)
{

	//printf("Removing breakpoint at 0x%X\n", addr);

	BREAKPOINT * bp;
	bp = get_breakpoint(addr);
	if(bp == NULL)
	{
		printf("Warning: tried to remove a breakpoint where none exists\n");
		return;
	}

	*(addr) = bp->instruction;
	bp->valid = 0;

	memicbi(addr, 4);

}

void enable_breakpoint(unsigned int * addr)
{
	//printf("Enabling breakpoint at 0x%X\n", addr);

	BREAKPOINT * bp;
	bp = get_breakpoint(addr);
	if(bp == NULL)
	{
		printf("Warning: tried to enable a breakpoint where none exists\n");
		return;
	}
	*(addr) = BREAKPOINT_INSTR;
}

void disable_breakpoint(unsigned int * addr)
{
	//printf("Disabling breakpoint at 0x%X\n", addr);

	BREAKPOINT * bp;
	bp = get_breakpoint(addr);
	if(bp == NULL)
	{
		printf("Warning: tried to disable a breakpoint where none exists\n");
		return;
	}
	*(addr) = bp->instruction;
}
