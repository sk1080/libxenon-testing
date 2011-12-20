#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <console/console.h>
#include <xenon_uart/xenon_uart.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/cache.h>
#include <newlib/vfs.h>
#include <threads/threads.h>
#include <debug/debug.h>
#include <ppc/atomic.h>
#include <ppc/register.h>

static char exception_text[4096]="\0";
debug_function_proc debugRoutine = debug_routine_stub;

static void debug_flush_console()
{
    char * p=exception_text;
    while(*p){
        putch(*p);
        console_putch(*p++);
    }
    exception_text[0]='\0';
}
/*
#define EXCEPT_CODE_UNKNOWN                     0
#define EXCEPT_CODE_DEBUG_PRINT                 1
#define EXCEPT_CODE_TRAP                        2
#define EXCEPT_CODE_INVALID_INSTRUCTION         3
#define EXCEPT_CODE_INVALID_FLOAT_OPERATION     4
#define EXCEPT_CODE_PRIVILEGED_INSTRUCTION      5
#define EXCEPT_CODE_SEGMENTATION_FAULT          6 // Not used yet
#define EXCEPT_CODE_BREAKPOINT                  7 // Not used yet
 */
static char *exception_strings[] =
{
    "Unknown Exception!",
    "Debug Print!", // Why are we here? this is handled...
    "Trap!",
    "Invalid Instruction!",
    "Invalid Float Operation!",
    "Privileged Instruction!",
    "Segmentation Fault!",
    "Breakpoint!",
};

void dump_thread_context_to_screen(PROCESSOR_DATA_BLOCK *processor, unsigned int exceptionCode)
{
    console_set_colors(0x000080ff, 0xffffffff);
    console_init();
    console_clrscr();
    
    strcpy(exception_text, exception_strings[exceptionCode]);
    
    debug_flush_console();

    sprintf(exception_text,"\nIar=%016llx Msr=%016llx Lr=%016llx\n\n",
                    processor->IARSave, processor->MSRSave, processor->LRSave);

    debug_flush_console();

    int i;
    for(i=0;i<8;++i)
            sprintf(exception_text,"%s%02d=%016llx %02d=%016llx %02d=%016llx %02d=%016llx\n",
                            exception_text,
                    i,processor->RegisterSave[i],
                    i+8,processor->RegisterSave[i+8],
                    i+16,processor->RegisterSave[i+16],
                    i+24,processor->RegisterSave[i+24]);

    debug_flush_console();

    strcat(exception_text,"\n\nOn uart: 'h'=Halt, 'r'=Reboot\n\n");

    debug_flush_console();

    for(;;){
            switch(getch()){
                    case 'h':
                            xenon_smc_power_shutdown();
                            for(;;);
                            break;
                    case 'r':
                            xenon_smc_power_reboot();
                            for(;;);
                            break;
            }
    }
}

extern void (*stdout_hook)(const char *text, int len);
unsigned int debug_routine_stub(unsigned int code, PROCESSOR_DATA_BLOCK *processor)
{
    if(code != EXCEPT_CODE_DEBUG_PRINT)
        return 0;
    
    size_t i;
    size_t len = processor->RegisterSave[4];
    char * src = (char*)processor->RegisterSave[3];
    
    if(strncmp(src, "Thread create returned", 22) == 0)
        if(stdout_hook)
                stdout_hook("wtf\n", 4);
    
    if (stdout_hook)
        stdout_hook(src, len);
    
    for (i = 0; i < len; ++i)
        putch(((const char*)src)[i]);
    
    // Skip over the trap
    if(processor->CurrentThread)
        processor->CurrentThread->Context.Iar += 4;
    else
        processor->IARSave += 4;
    
    return 1;
}

// Returns exception code
unsigned int decode_exception(PROCESSOR_DATA_BLOCK *processor, unsigned int exceptReason)
{
    if(exceptReason == EXCEPT_REASON_INVALID_INSTRUCTION)
        return EXCEPT_CODE_INVALID_INSTRUCTION;
    else if(exceptReason == EXCEPT_REASON_INVALID_FLOAT_OPERATION)
        return EXCEPT_CODE_INVALID_FLOAT_OPERATION;
    else if(exceptReason == EXCEPT_REASON_PRIVILEGED_INSTRUCTION)
        return EXCEPT_CODE_PRIVILEGED_INSTRUCTION;
    else if(exceptReason == EXCEPT_REASON_TRAP)
    {
        // If you have any special reasons for trap codes, they go here
        // The format of the instruction is:
        // twi 31, r0, CODE
        
        unsigned int op = *(unsigned int*)processor->IARSave;
        // First, check if we handle this type of trap, if not just return trap
        if((op & 0xFFFFFF00) != 0x0FE00000) // twi 31, r0, 0
            return EXCEPT_CODE_TRAP;
        
        // Extract the trap type
        unsigned int trap_type = op & 0xFF;
        
        if(trap_type == DEBUG_TRAP_PRINT)
            return EXCEPT_CODE_DEBUG_PRINT;
        
        // Not defined, just trap
        return EXCEPT_CODE_TRAP;
    }
    
    // No other except reasons exist, in this case just return unknown exception
    return EXCEPT_CODE_UNKNOWN;
}

void program_interrupt_handler()
{
    // This is called when the program interrupts
    // Via an invalid instruction, a twi instruction, or what-have-you
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Make sure only one exception gets in at a time
    static unsigned int program_interrupt_lock = 0;
    lock(&program_interrupt_lock);
    
    // Save the context
    if(processor->CurrentThread)
        dump_thread_context();
    
    // No debug function = red screen
    if(debugRoutine == NULL)
        dump_thread_context_to_screen(processor, decode_exception(processor, (processor->MSRSave >> 17) & 0xF));
    
    // Decode
    unsigned int code = decode_exception(processor, (processor->MSRSave >> 17) & 0xF);
    
    // Dispatch
    if(debugRoutine(code, processor) == 0)
        dump_thread_context_to_screen(processor, decode_exception(processor, (processor->MSRSave >> 17) & 0xF));
    
    // Restore context
    if(processor->CurrentThread)
        restore_thread_context();
    
    unlock(&program_interrupt_lock);
}

// Debug output printf, outputs up to 0x200 characters
int printf(const char * string, ...)
{
    char buf[0x200];
    va_list ap;
    int r;
    
    static unsigned int _lock = 0;
    unsigned int irql = thread_spinlock(&_lock);
    
    va_start(ap, string);
    r = vsnprintf(buf, 0x200, string, ap);
    va_end(ap);
    
    debug_print(buf, r);
    
    thread_unlock(&_lock, irql);
    //vfs_console_write(NULL, buf, r);
    
    return r;
}