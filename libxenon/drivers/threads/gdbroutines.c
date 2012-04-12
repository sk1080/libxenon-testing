#include <threads/threads.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "gdbroutines.h"

const char hexchars[]="0123456789abcdef";

int threads_suspended = 0;

int mode;

void set_mode(int number)
{
	mode = number;
}

int get_mode()
{
	return mode;
}

int hex(char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

int hexToInt(char **ptr, int *ival)
{
	int cnt;
	int val,nibble;

	val = 0;
	cnt = 0;
	while(**ptr) {
		nibble = hex(**ptr);
		if(nibble<0) break;

		val = (val<<4)|nibble;
		cnt++;

		(*ptr)++;
	}
	*ival = val;
	return cnt;
}

int ptr_seems_valid(void * p){	//Putting this here because this is subject to change
	return (unsigned int)p>=0x80000000 && (unsigned int)p<0xa0000000;
}

char* mem2hstr(char *buf,const char *mem,int count)
{
	int i;
	char ch;

	for(i=0;i<count;i++,mem++) {
		ch = *mem;
		*buf++ = hexchars[ch>>4];
		*buf++ = hexchars[ch&0x0f];
	}
	*buf = 0;
	return buf;
}

char getdbgchar(void)//TODO: UART *DOES* *NOT* *WORK*, needs a lot of hacking to get it to work at all, somebody please fix this(getting data on uart is hard)
{
	return getch();	 //TODO: THESE ARE JUST HERE TO TAKE THE PLACE OF MY HACKS FOR THE TIME BEING
}

void putdbgchar(unsigned char c) //TODO: UART has not been functional since I added net support, and it was hacky then
{
	putch(c);
}

void putdbgstr(unsigned char *s)
{
	uart_puts(s);
}

void uart_putpacket(const char *buffer)
{
	unsigned char recv;
	unsigned char chksum,ch;
	char *ptr;
	const char *inp;
	static char outbuf[BUFMAX];

	do {
		inp = buffer;
		ptr = outbuf;

		*ptr++ = '$';

		chksum = 0;
		while((ch=*inp++)!='\0') {
			*ptr++ = ch;
			chksum += ch;
		}

		*ptr++ = '#';
		*ptr++ = hexchars[chksum>>4];
		*ptr++ = hexchars[chksum&0x0f];
		*ptr = '\0';

		putdbgstr(outbuf);

		recv = getdbgchar();
	} while((recv&0x7f)!='+');
}

void uart_getpacket(char *buffer)
{
	char ch;
	unsigned char chksum,xmitsum;
	int i,cnt;

	do {
		while((ch=(getdbgchar()&0x7f))!='$');

		cnt = 0;
		chksum = 0;
		xmitsum = -1;

		while(cnt<BUFMAX) {
			ch = getdbgchar()&0x7f;
			if(ch=='#') break;

			chksum += ch;
			buffer[cnt] = ch;
			cnt++;
		}
		if(cnt>=BUFMAX) continue;

		buffer[cnt] = 0;
		if(ch=='#') {
			xmitsum = hex(getdbgchar()&0x7f)<<4;
			xmitsum |= hex(getdbgchar()&0x7f);

			if(chksum!=xmitsum) putdbgchar('-');
			else {
				putdbgchar('+');
				if(buffer[2]==':') {
					putdbgchar(buffer[0]);
					putdbgchar(buffer[1]);

					cnt = strlen((const char*)buffer);
					for(i=3;i<=cnt;i++) buffer[i-3] = buffer[i];
				}
			}
		}
	} while(chksum!=xmitsum);
}

void halt_threads()
{
	if(threads_suspended != 0)
		return;

	int thisthread = thread_get_current()->ThreadId;
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++)
	{
		PTHREAD pthr = thread_get_pool(i);
		if(pthr->Valid && pthr->ThreadId != thisthread)
		{
			if(mode == 1 && pthr->Name)
			{
				//printf("We have a named thread: %s\n", pthr->Name);

				if(!strcmp(pthr->Name, "tcpip_thread"))
				{
					continue;
				}
				if(!strcmp(pthr->Name, "poll_thread"))
				{
					continue;
				}
			}

			if(pthr->Priority != 0) //Idle threads
			thread_suspend(pthr);
		}
	}

	threads_suspended = 1;

}

void resume_threads()
{
	if(threads_suspended != 1)
		return;
	int thisthread = thread_get_current()->ThreadId;
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++)
	{
		PTHREAD pthr = thread_get_pool(i);
		if(pthr->Valid && pthr->ThreadId != thisthread)
		{
			if(mode == 1 && pthr->Name)
			{
				//printf("We have a named thread: %s\n", pthr->Name);

				if(!strcmp(pthr->Name, "tcpip_thread"))
					continue;
				if(!strcmp(pthr->Name, "poll_thread"))
					continue;
			}

			if(pthr->Priority != 0)
			thread_resume(pthr);
		}
	}
	threads_suspended = 0;

}

void halt_threads_nolock()
{
	if(threads_suspended != 0)
		return;
	//int thisthread = thread_get_current()->ThreadId;
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++)
	{
		PTHREAD pthr = thread_get_pool(i);
		if(pthr->Valid)
		{
			if(mode == 1 && pthr->Name)
			{
				//printf("We have a named thread: %s\n", pthr->Name);

				if(!strcmp(pthr->Name, "tcpip_thread"))
				{
					continue;
				}
				if(!strcmp(pthr->Name, "poll_thread"))
				{
					continue;
				}
				if(!strcmp(pthr->Name, "gdb"))
				{
					continue;
				}
			}

			if(pthr->Priority != 0) //Idle threads
			    if(pthr->SuspendCount < 80)
			    {
			        pthr->SuspendCount++;
			    }
		}
	}
	threads_suspended = 1;
}

void resume_threads_nolock()
{
	if(threads_suspended != 1)
		return;
	//int thisthread = thread_get_current()->ThreadId;
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++)
	{
		PTHREAD pthr = thread_get_pool(i);
		if(pthr->Valid)
		{
			if(mode == 1 && pthr->Name)
			{
				//printf("We have a named thread: %s\n", pthr->Name);

				if(!strcmp(pthr->Name, "tcpip_thread"))
					continue;
				if(!strcmp(pthr->Name, "poll_thread"))
					continue;
				if(!strcmp(pthr->Name, "gdb"))
					continue;
			}

			if(pthr->Priority != 0)
			if(pthr->SuspendCount)
			{
			    pthr->SuspendCount--;
			}
		}
	}
	threads_suspended = 0;

}

int hstr2nibble(const char *buf,int *nibble)
{
        int ch;

        ch = *buf;
        if(ch>='0' && ch<='9') {
                *nibble = ch - '0';
                return 1;
        }
        if(ch>='a' && ch<='f') {
                *nibble = ch - 'a' + 10;
                return 1;
        }
        if(ch>='A' && ch<='F') {
                *nibble = ch - 'A' + 10;
                return 1;
        }
        return 0;
}

int hstr2byte(const char *buf,int *bval)
{
        int hnib,lnib;

        if(!hstr2nibble(buf,&hnib) || !hstr2nibble(buf+1,&lnib)) return 0;

        *bval = (hnib<<4)|lnib;
        return 1;
}

const char* vhstr2int(const char *buf,int *ival)
{
        int i,val,nibble;
        int found0,lim;

        found0 = 0;
        for(i=0;i<8;i++,buf++) {
                if(*buf!='0') break;

                found0 = 1;
        }

        val = 0;
        lim = 8 - i;
        for(i=0;i<lim;i++,buf++) {
                if(!hstr2nibble(buf,&nibble)) {
                        if(i==0 && !found0) return NULL;

                        *ival = val;
                        return buf;
                }
                val = (val<<4)|nibble;
        }
        if(hstr2nibble(buf,&nibble)) return NULL;

        *ival = val;
        return buf;
}

const char* fhstr2int(const char *buf,int *ival)
{
        int i,val,nibble;

        val = 0;
        for(i=0;i<8;i++,buf++) {
                if(!hstr2nibble(buf,&nibble)) return NULL;

                val = (val<<4)|nibble;
        }

        *ival = val;
        return buf;
}

char* int2fhstr(char *buf,int val)
{
        int i,nibble,shift;

        for(i=0,shift=28;i<8;i++,shift-=4,buf++) {
                nibble = (val>>shift)&0x0f;
                *buf = hexchars[nibble];
        }
        return buf;
}

char* int2vhstr(char *buf,int val)
{
        int i,nibble,shift;

        for(i=0,shift=28;i<8;i++,shift-=4) {
                nibble = (val>>shift)&0x0f;
                if(nibble) break;
        }
        if(i==8) {
                *buf++ = '0';
                return buf;
        }

        *buf++ = hexchars[nibble];
        for(i++,shift-=4;i<8;i++,shift-=4,buf++) {
                nibble = (val>>shift)&0x0f;
                *buf = hexchars[nibble];
        }
        return buf;
}

char* thread2fhstr(char *buf,int thread)
{
        int i,nibble,shift;

        for(i=0;i<8;i++,buf++) *buf = '0';
        for(i=0,shift=28;i<8;i++,shift-=4,buf++) {
                nibble = (thread>>shift)&0x0f;
                *buf = hexchars[nibble];
        }
        return buf;
}

char* thread2vhstr(char *buf,int thread)
{
        int i,nibble,shift;

        for(i=0,shift=28;i<8;i++,shift-=4) {
                nibble = (thread>>shift)&0x0f;
                if(nibble) break;
        }
        if(i==8) {
                *buf++ = '0';
                return buf;
        }

        *buf++ = hexchars[nibble];
        for(i++,shift-=4;i<8;i++,shift-=4,buf++) {
                nibble = (thread>>shift)&0x0f;
                *buf = hexchars[nibble];
        }
        return buf;
}

const char* fhstr2thread(const char *buf,int *thread)
{
        int i,nibble,val;

        for(i=0;i<8;i++,buf++)
                if(*buf!='0') return NULL;

        val = 0;
        for(i=0;i<8;i++,buf++) {
                if(!hstr2nibble(buf,&nibble)) return NULL;

                val = (val<<4)|nibble;
        }

        *thread = val;
        return buf;
}

const char* vhstr2thread(const char *buf,int *thread)
{
        int i,val,nibble;
        int found0,lim;

        found0 = 0;
        for(i=0;i<16;i++,buf++) {
                if(*buf!='0') break;

                found0 = 1;
        }

        val = 0;
        lim = 16 - i;
        for(i=0;i<lim;i++,buf++) {
                if(!hstr2nibble(buf,&nibble)) {
                        if(i==0 && found0) return NULL;

                        *thread = val;
                        return buf;
                }

                val = (val<<4)|nibble;
        }
        if(hstr2nibble(buf,&nibble)) return NULL;

        *thread = val;
        return buf;
}



int parseqp(const char *in,int *mask,int *thread)
{
        const char *ptr;

        ptr = fhstr2int(in+2,mask);
        if(ptr==NULL) return 0;

        ptr = fhstr2thread(ptr,thread);
        if(ptr==NULL) return 0;

        return 1;
}

void packqq(char *out,int mask,int thread,PTHREAD pthr)
{
		int len;

        *out++ = 'q';
        *out++ = 'Q';
        out = int2fhstr(out,mask);
        out = thread2fhstr(out,thread);

        if(mask&0x01) {
                memcpy(out,"00000001",8);
                out += 8;
                *out++ = '1';
                *out++ = '0';
                out = thread2fhstr(out,thread);
        }
        if(mask&0x02) {
                memcpy(out,"00000002",8);
                out += 8;
                *out++ = '0';
                *out++ = '1';
                *out++ = '1';
        }
        if(mask&0x04) {
                memcpy(out,"00000004",8);//TODO
                out += 8;

                len = 1;

                *out++ = hexchars[(len>>4)&0x0f];
                *out++ = hexchars[len&0x0f];

                memcpy(out,"\0",1);
                out += 1;
        }
        if(mask&0x08) {
                memcpy(out,"00000008",8);
                out += 8;

                if(pthr->Name == NULL)
                	pthr->Name = "";

                len = strlen(pthr->Name);

                printf("Packing name %s\n", pthr->Name);

                *out++ = hexchars[(len>>4)&0x0f];
                *out++ = hexchars[len&0x0f];

                memcpy(out,&pthr->Name,len);
                out += len;
        }
        if(mask&0x10) {
                memcpy(out,"00000010",8);//TODO
                out += 8;

                len = 1;

                *out++ = hexchars[(len>>4)&0x0f];
                *out++ = hexchars[len&0x0f];

                memcpy(out,"\0",1);
                out += 1;
        }
        *out = 0;
}

