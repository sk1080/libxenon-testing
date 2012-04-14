#ifndef ROUTINES_H
#define	ROUTINES_H

#ifdef	__cplusplus
extern "C" {
#endif

#define BUFMAX			2048

void set_mode(int number);
int get_mode();

int hex(char ch);

int hexToInt(char **ptr, int *ival);

int ptr_seems_valid(void * p);

char* mem2hstr(char *buf,const char *mem,int count);

char getdbgchar(void);//TODO: UART *DOES* *NOT* *WORK*, needs a lot of hacking to get it to work at all, somebody please fix this(getting data on uart is hard)

void putdbgchar(unsigned char c); //TODO: UART has not been functional since I added net support, and it was hacky then

void putdbgstr(unsigned char *s);

void uart_putpacket(const char *buffer);

void uart_getpacket(char *buffer);

void halt_threads();

void resume_threads();

void halt_threads_nolock();

void resume_threads_nolock();

int parseqp(const char *in,int *mask,int *thread);
void packqq(char *out,int mask,int thread,PTHREAD pthr);

int parsezbreak(const char *in,int *type,char **addr,int *len);

#ifdef	__cplusplus
}
#endif

#endif
