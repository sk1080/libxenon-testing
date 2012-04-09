#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xenon_uart/xenon_uart.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/register.h>
#include <threads/threads.h>
#include <threads/debug.h>
#include <threads/gdb.h>
#include <xenon_uart/xenon_uart.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <network/network.h>


static int stub_active = 0;
static int attached = 0;
static int active = 0;
static int mode = 0;//0 = uart, 1 = network

static int threads_suspended = 0;

static int signal = 3;

PTHREAD ctrlthread;
PTHREAD otherthread;

int gdb_active()
{
	return stub_active;
}

static int hex(char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

static int hexToInt(char **ptr, int *ival)
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

const char hexchars[]="0123456789abcdef";
#define BUFMAX			2048

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

static void uart_putpacket(const char *buffer)
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

static void uart_getpacket(char *buffer)
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
					printf("lets not stop this one\n");
					continue;
				}
				if(!strcmp(pthr->Name, "poll_thread"))
				{
					printf("lets not stop this one\n");
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

static 	int listen_fd, sock_fd;

static int net_putchar(char c)
{
	return send(sock_fd, &c, 1, 0);
}

static void net_putpacket(const char * buffer)
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

		printf("Putting packet %s\n", outbuf);
		send(sock_fd, outbuf, strlen(outbuf), 0);
		//printf("waiting for ack...\n");
		recv(sock_fd, outbuf, 1, 0);
		//printf("got ack...\n");
		recv = outbuf[0];

	} while((recv&0x7f)!='+');

}

static int net_getpacket(char * buffer)
{
	unsigned char chksum,xmitsum;
	char ch;
	int i;
	int len;

	//fd_set fds;
	//struct timeval tv = {0, 0};

	do {
		chksum = 0;
		xmitsum = 0;

	//FD_ZERO(&fds);
	//FD_SET(sock_fd, &fds);
	//r = select(sock_fd + 1, &fds, NULL, NULL, &tv);
	//if(r == 1)
	long arg = lwip_fcntl(sock_fd, F_GETFL, 0);
	lwip_fcntl(sock_fd, F_SETFL, arg | O_NONBLOCK);
	len = recv(sock_fd, buffer, BUFMAX, 0);
	lwip_fcntl(sock_fd, F_SETFL, arg);


	//else if (r == -1)
	//	return -1;
	//else
	//	continue;
	if(errno)
	{
		if(errno == EAGAIN)//?
		{
			continue;
		}
		else
		{
			perror("error: ");
			printf("number was %i\n", errno);
			return -1;
		}
	}
	if(len == -1)
	{
		perror("-1 len:");
		continue;
	}
	buffer[len] = '\0';

	printf("Got packet of len %i: %s\n",len, buffer);

	if(len > 3)
	{

	}
	else
	{
		printf("?\n");
	}

	if(buffer[0] != '$')
	{
		printf("Break!\n");


		if(active == 0)
		{
			active = 1;
			halt_threads();
		}

		chksum = 1; xmitsum = 0; //avoid lols
		continue;
	}

	int cnt = 1;

	while(cnt < len)
	{
		ch = buffer[cnt];
		//printf("chksumming char %c\n", ch);
		if(ch=='#') break;

		chksum += ch;

		cnt++;

	}
	if(cnt > len)
	{
		printf("overrun\n");
		continue;
	}
	if(ch == '#')
	{
		cnt++;
		//printf("read chk char char %c\n", buffer[cnt]);
		xmitsum = hex(buffer[cnt]&0x7f)<<4;
		cnt++;
		//printf("read chk char char %c\n", buffer[cnt]);
		xmitsum |= hex(buffer[cnt]&0x7f);
		buffer[cnt - 2] = '\0';

		for(i=1; i < len; i++) buffer[i - 1] = buffer[i];


		if(chksum!=xmitsum) net_putchar('-');
		else {
			net_putchar('+');
			if(buffer[2]==':') {
				net_putchar(buffer[0]);
				net_putchar(buffer[1]);

				cnt = strlen((const char*)buffer);
				for(i=3;i<=cnt;i++) buffer[i-3] = buffer[i];
			}
		}


	}

	} while(chksum != xmitsum);

	//printf("parsed as: %s\n", buffer);


	return 0;
}

static void putpacket(const char *buffer)
{
	if(mode == 0)
		uart_putpacket(buffer);
	else
		net_putpacket(buffer);
}

static int getpacket(char * buffer)
{
	if(mode == 0)
		uart_getpacket(buffer);
	else
		return net_getpacket(buffer);

	return 0;
}

int parse_cmd(char * buffer)
{

    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();

	switch(buffer[0])
	{
		case 'q':
		{
			switch(buffer[1])
			{
				case 'S':
				{
					if(buffer[2] == 'u')
					{
					//printf("Got qSupported\n");
					putpacket("PacketSize=2048");
					}
					else
					{
						putpacket("OK");
					}
					break;
				}
				case 'C':
				{
					if(otherthread == NULL)
					{
						sprintf(buffer, "QC%i", -1);
					}
					else
					{

						sprintf(buffer, "QC%i", otherthread->ThreadId + 1);
					}
					putpacket(buffer);
					break;
				}
				case 'A':
				{
					putpacket("1");
					break;
				}
				case 'O':
				{
					//qOffsets
					putpacket("Text=0;Data=0;Bss=0");
					break;
				}
				case 'f':
				{

						int thisthread = thread_get_current()->ThreadId;
						int i;
						sprintf(buffer, "m");
						for(i = 0; i < MAX_THREAD_COUNT; i++)
						{
							PTHREAD pthr = thread_get_pool(i);
							if(pthr->Valid && pthr->ThreadId != thisthread)
							{
								sprintf(buffer, "%s%x,", buffer, pthr->ThreadId + 1); //FIXME: a lot of threads will make this fial
							}

						}

						putpacket(buffer);

					break;
				}
				case 's':
				{
					putpacket("l");
					break;
				}
				case 'T':
				{
					putpacket("");
					break;
				}
				case 'P':
				{
					putpacket(""); //bitch
					break;
				}
				default:
				break;
			}
			break;
		}
		case 'H':
		{
			char * ptr = &buffer[2];
			int thread;
			hexToInt(&ptr, &thread);
			thread--; //Ghetto: gdb doesn't like thread id 0, also this fails...
			if(thread < 0)
				thread++;
			PTHREAD pthr = thread_get_pool(thread);

					if(buffer[1] == 'c')
					{
						if(thread == -1)
						{
							ctrlthread = NULL;
							putpacket("OK");
							break;
						}
						else
						{
							if(pthr)
								if(pthr->Valid)
								{
									ctrlthread = pthr;
									putpacket("OK");
									break;
								}
						}
					}
					if(buffer[1] == 'g')
					{
						if(thread == -1)
						{
							otherthread = NULL;
							printf("WTF???");
							putpacket("OK");
							break;
						}
						else
						{
							if(pthr)
								if(pthr->Valid)
								{
									otherthread = pthr;
									putpacket("OK");
									break;
								}
						}
					}
					putpacket("E01");



			break;
		}
		case '?':
		{
			char buffer[10]; //enough?
			sprintf(buffer, "S%i", signal);
			putpacket(buffer);
			break;
		}
		case 'g':
		{
			char *ptr = buffer;
		    //unsigned int irql = thread_spinlock(&ThreadListLock);

			if(otherthread == NULL)
			{
				putpacket("E01");
				break;
			}

		    //ptr = mem2hstr(ptr, (char*)&otherthread->Context.Gpr, 32 * 4);
		    if(processor->CurrentProcessor == 0)
		    {
		    	//dump_thread_context(&context);
		    }
			    int reg;
			    for(reg = 0; reg < 32; reg++)
			    {
			    	ptr = mem2hstr(ptr, (char*)&otherthread->Context.Gpr[reg] + 4, 4);
			    }
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.FpuVpu.Fpr, 32 * 8);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Iar + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Msr + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Cr + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Lr + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Ctr + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.Xer + 4, 4);
		    ptr = mem2hstr(ptr, (char*)&otherthread->Context.FpuVpu.Fpscr + 4, 4);


		    if(processor->CurrentProcessor == 0)
		    {
		    	//restore_thread_context(&context);
		    }

		    //thread_unlock(&ThreadListLock, irql);
		    putpacket(buffer);
			break;
		}
		case 'D':
		{
			attached = 0;
			active = 0;
			resume_threads();
			putpacket("OK");
			//thread_enable_interrupts(msr);
			return 1;
		}
		case 'm':
		{
			char * ptr = &buffer[1];
			unsigned int addr;
			unsigned int len;
			if(!hexToInt(&ptr, &addr))
			{
				putpacket("E00");
				break;
			}
			if(!(*ptr++==','))
			{
				putpacket("E01");
				break;
			}
			if(!hexToInt(&ptr,&len))
			{
				putpacket("E02");
				break;
			}
			if(!ptr_seems_valid((void*)addr))
			{
				putpacket("E03");
				break;
			}
			mem2hstr(buffer,(void*)addr,len);
			putpacket(buffer);

			break;
		}
		case 'c':
		{
			//TODO
			attached = 0;
			active = 0;
			resume_threads();
			putpacket("OK");
			//thread_enable_interrupts(msr);
			return 1;
		}
		case 'T':
		{
			char * ptr = &buffer[1];
			int thread;
			hexToInt(&ptr, &thread);
			thread--; //Ghetto: gdb doesn't like thread id 0, also this fails...

			PTHREAD pthr = thread_get_pool(thread);

			if(pthr == NULL)
			{
				//putpacket("E01"); Dodge some lol temporarily ^
				//break;
			}
			if(!pthr->Valid)
			{
				//putpacket("E02");
				//break;
			}

			putpacket("OK");
			break;
		}
		default:
		{
			printf("Got packet %c\n", buffer[0]);
			putpacket("");
			break;
		}

	}
	return 0;

}

void net_server()
{

	mode = 1;

	printf("Gdb server starting up on port 2159\n");

	//Thank you bochs

	struct sockaddr_in sockaddr;
	socklen_t sockaddr_len;
	int r;
	int opt;

	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1)
	{
		printf("Failed to create socket\n");
		return;
	}

	opt = 1;
	r = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset (&sockaddr, '\000', sizeof sockaddr);
	sockaddr.sin_len = sizeof sockaddr;
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(2159);
	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	r = bind(listen_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
	if(r == -1)
	{
		printf("Failed to bind to port\n");
		return;
	}

	r = listen(listen_fd, 0);
	if(r == -1)
	{
		printf("Failed to listen on socket\n");
		return;
	}

	//printf("Will now accept connections\n");

	sockaddr_len = sizeof sockaddr;

	while(1)
	{

	sock_fd = accept(listen_fd, (struct sockaddr *)&sockaddr, &sockaddr_len);

	if(sock_fd == -1)
	{
		printf("Failed to accept\n");
		return;
	}
	//close(listen_fd);

	opt = 1;
	r = setsockopt(sock_fd, SOCK_STREAM,TCP_NODELAY, &opt, sizeof(opt));

	if(sock_fd == -1)
	{
		printf("Setsockopt failed\n");
		return;
	}

	/*
	long arg;
	arg = lwip_fcntl(sock_fd, F_GETFL, 0);
	lwip_fcntl(sock_fd, F_SETFL, arg | O_NONBLOCK);
	*/

	unsigned int ip = sockaddr.sin_addr.s_addr;
	printf("Connected to %d.%d.%d.%d\n", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);

	char buffer[BUFMAX];

	while(1)
	{
		printf("Waiting for a packet\n");
		int ret = net_getpacket(buffer);
		if(ret == -1)
			break;

		ret = parse_cmd(buffer);

		if(ret == 1)
		{
			close(sock_fd);
			break;
		}


	}


	}

}

void gdbserver()
{
	while(1)
	{
    //unsigned int msr = thread_disable_interrupts();
	//if(!charpresent())
	//{
		//thread_enable_interrupts(msr);
	//	return; //non-blocking
	//}
	//printf("Stub activating");
	char buffer[BUFMAX];

	if(attached == 1)
	{
		putpacket("S05");
	}
	attached = 1;
	while(1)
	{
		//printf("Waiting for a packet\n");
		getpacket(buffer);

		if(!active)
		{
			halt_threads();
			active = 1;
		}

		//if(getpacket(buffer) == -1) non-blocking
		//	return;

		parse_cmd(buffer);

		}

	}

}

// Does exception stuff
int gdb_debug_routine(unsigned int code, CONTEXT *context)
{

	//int msr = thread_disable_interrupts();

	if(debug_routine_stub(code, context) == 1)
		return 1;

	switch(code)
	{
		case EXCEPT_CODE_INVALID_INSTRUCTION:
			signal = 4;
		break;

		case EXCEPT_CODE_INVALID_FLOAT_OPERATION:
			signal = 8;
		break;

		case EXCEPT_CODE_PRIVILEGED_INSTRUCTION:
			signal = 4;//hmm
		break;

		case EXCEPT_CODE_SEGMENTATION_FAULT_READ:
			signal = 11;
		break;

		case EXCEPT_CODE_SEGMENTATION_FAULT_WRITE:
			signal = 11;
		break;

		case EXCEPT_CODE_BREAKPOINT:
			signal = 5;//tbd
		break;

		default:
		break;
	}

	PROCESSOR_DATA_BLOCK *block;
	block = (unsigned int *)(context->Gpr[13]);//more lame 64bit->32bit ghettocode

	//printf("We have a fail on cpu %i\n", block->CurrentProcessor);

	PTHREAD thread = block->CurrentThread;
	//thread->SuspendCount++;
	otherthread = thread;

	halt_threads_nolock();

	//Time to do things the ghetto way, as we are already in lock
	//printf("Suspending threads...\n");

	//uart_puts("in to the loop\n");
	/*
	char buffer[100];
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++)
	{

		//sprintf(buffer,"ct%i",i);
		//uart_puts(buffer);
		PTHREAD pthr = thread_get_pool(i);
		//sprintf(buffer,"gt%i",i);
		//uart_puts(buffer);
		if(pthr == NULL || pthr == thread)
			continue;
		if(pthr->Valid )
		{
			if(pthr->Name)
				if(!strcmp(pthr->Name, "gdb"))
				{
					//TODO
					continue;
				}

			//uart_puts("c1");

			if(mode == 1 && pthr->Name)
			{
				//printf("We have a named thread: %s\n", pthr->Name);

				if(!strcmp(pthr->Name, "tcpip_thread"))
				{
					//printf("lets not stop this one\n");
					continue;
				}
				if(!strcmp(pthr->Name, "poll_thread"))
				{
					//printf("lets not stop this one\n");
					continue;
				}
			}

			//uart_puts("c2");

			if(pthr->Priority != 0) //Idle threads
			if(pthr->SuspendCount < 80)
			{
			    pthr->SuspendCount++;
			}

			//uart_puts("c3\n");
		}
	}
	*/


	//printf("Suspended threads...\n");

    //context->Iar += 4;

	//thread_enable_interrupts(msr);

    return 1;
}

static PTHREAD gdbthread;

// Setup the gdb stub
void gdb_init()
{
	ctrlthread = thread_get_pool(0);
	otherthread = thread_get_pool(0);

    //debugRoutine = gdb_debug_routine;
	//debugPoll = gdbserver;
    //stub_active = 1;
    //putpacket("Otrollolol");
    printf("Creating gdb thread\n");
    //gdbthread = thread_create(gdbserver, 0, 0, 0);

    gdbthread = thread_create(net_server, 0, 0, 0);
    thread_set_name(gdbthread, "gdb");
    debugRoutine = gdb_debug_routine;
}

void gdb_stop()
{
	//end the thread here...?
	active = 0;
	stub_active = 0;
}

