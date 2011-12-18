#include <stdio.h>
#include <stdarg.h>
#include <debug.h>
#include <newlib/vfs.h>
#include <threads/threads.h>
#include <ppc/atomic.h>

// Debug output printf, outputs up to 0x200 characters
int printf(const char * string, ...)
{
    char buf[0x200];
    va_list ap;
    int r;
    
    va_start(ap, string);
    r = vsnprintf(buf, 0x200, string, ap);
    va_end(ap);
    
    static unsigned int console_lock = 0;
        
    char irql;
        
    if(thread_get_processor_block()->Irq < 2)
        irql = thread_raise_irql(2);
    else
        irql = thread_get_processor_block()->Irq;
    lock(&console_lock);
    
    vfs_console_write(NULL, buf, r);
    
    unlock(&console_lock);
    thread_lower_irql(irql);
    
    return r;
}