#include <stdio.h>
#include <stdarg.h>
#include <debug.h>
#include <newlib/vfs.h>

// Debug output printf, outputs up to 0x200 characters
int printf(const char * string, ...)
{
    char buf[0x200];
    va_list ap;
    int r;
    
    va_start(ap, string);
    r = vsnprintf(buf, 0x200, string, ap);
    va_end(ap);
    
    vfs_console_write(NULL, buf, r);
    
    return r;
}