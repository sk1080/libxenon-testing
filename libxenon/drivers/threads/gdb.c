//The lamest gdbstub in existance
//TODO: Implement more functionality
//TODO: Refactor code further
//TODO: Cleanup


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

#include <threads/breakpoint.h>

#include "gdbroutines.h"


static int stub_active = 0;
static int attached = 0;
static int active = 0;
static int running = 0;

static int signal = 3;

PTHREAD ctrlthread;
PTHREAD otherthread;

int gdb_active()//was really only usefull for uart
{
	return stub_active;
}

static 	int listen_fd, sock_fd;

static int net_putchar(char c)
{
	int ret;
	do{ret = send(sock_fd, &c, 1, 0);}while(ret < 0);
	return ret;
}

extern const char hexchars[];

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

		//printf("Putting packet %s\n", outbuf);
		int ret;
		do{ret = send(sock_fd, outbuf, strlen(outbuf), 0);}while(ret < 0);
		//printf("waiting for ack...\n");
		while(recv(sock_fd, outbuf, 1, 0) <= 0);
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

	do {
		chksum = 0;
		xmitsum = 0;

	long arg = lwip_fcntl(sock_fd, F_GETFL, 0);
	lwip_fcntl(sock_fd, F_SETFL, arg | O_NONBLOCK);
	len = recv(sock_fd, buffer, BUFMAX, 0);
	lwip_fcntl(sock_fd, F_SETFL, arg);

	if(errno)
	{
		if(errno == EAGAIN)//?
		{
			//Check if we have a crash
			if(active == 0 && attached == 1 && running == 0)
			{
				printf("crash/trap detected\n");
				buffer[0] = '?'; //Simulate a ? packet if we are resuming from a continue
				buffer[1] = '\0';
				active = 1;
				return 0;
			}

			chksum = 1; xmitsum = 0; //avoid lols
			continue;
		}
		else
		{
			perror("error: ");
			printf("number was %i\n", errno);
			return -1;
		}
	}

	buffer[len] = '\00';

	//printf("Got packet of len %i: %s\n",len, buffer);
	if(len < 0)
	{
		chksum = 1; xmitsum = 0; //avoid lols, idk what is going on with no error...
		continue;
	}

	if(buffer[0] != '$')
	{
		printf("Break!\n");


		if(active == 0 && attached == 1 && running == 1)
		{
			//Break from a continue
			halt_threads();
			running = 0;
			buffer[0] = '?'; //Simulate a ? packet if we are resuming from a continue
			buffer[1] = '\0';
			active = 1;
			return 0;
		}

		if(active == 0)
		{
			active = 1;
			halt_threads();
			running = 0;
		}

		attached = 1;

		chksum = 1; xmitsum = 0; //avoid lols
		continue;
	}

	int cnt = 1;

	while(cnt < len)
	{
		ch = buffer[cnt];
		if(ch=='#') break;

		chksum += ch;

		cnt++;

	}
	if(cnt > len)
	{
		printf("packet overrun\n");
		continue;
	}
	if(ch == '#')
	{
		cnt++;
		xmitsum = hex(buffer[cnt]&0x7f)<<4;
		cnt++;
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

	return 0;
}

static void putpacket(const char *buffer)
{
	if(get_mode() == 0)
		uart_putpacket(buffer);
	else
		net_putpacket(buffer);
}

static int getpacket(char * buffer)
{
	if(get_mode() == 0)
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
				case 'S'://qSupported
				{
					if(buffer[2] == 'u')
					{
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

						sprintf(buffer, "QC%x", otherthread->ThreadId + 1);
					}
					putpacket(buffer);
					break;
				}
				case 'A'://qAttached
				{
					putpacket("1");
					break;
				}
				case 'O'://qOffsets
				{
					putpacket("Text=0;Data=0;Bss=0");//We do not use any relocation
					break;
				}
				case 'f':
				{
						int i;
						sprintf(buffer, "m");
						for(i = 0; i < MAX_THREAD_COUNT; i++)
						{
							PTHREAD pthr = thread_get_pool(i);
							if(pthr->Valid)
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
					if(buffer[2] == 'h') //qThreadExtraInfo: extra thread info
					{

					char tempbuffer[BUFMAX / 4];
					int thread;
					char * ptr = &buffer[17];
					hexToInt(&ptr, &thread);
					ptr = buffer;
					PTHREAD pthr = thread_get_pool(thread);
					sprintf(tempbuffer, "No Name");
					if(pthr)
					{
						if(pthr->Valid)
							if(pthr->Name)
							{
								sprintf(tempbuffer, "Name: %s", pthr->Name);
							}
					}


					int len = strlen(tempbuffer);

					int i;
					for(i = 0; i < len; i++)
					{
						sprintf(ptr, "%x", tempbuffer[i]);
						ptr +=2;
					}

					putpacket(buffer);
					break;

					}
					else //qTStatus
					{
						putpacket("");
						break;
					}
				}
				case 'P':
				{
					printf("Got qP\n");
					//bitch
					int ret,rthread,mask;

					ret = parseqp(buffer,&mask,&rthread);
					if(!ret || (mask&~0x1f)) {
						putpacket("E01");
						break;
					}
					printf("qP thread parsed as %i\n", rthread);
					PTHREAD pthr = thread_get_pool(rthread - 1);

					packqq(buffer,mask,rthread,pthr);
					putpacket(buffer);
					break;
				}
				default:
				break;
			}
			break;
		}
		case 'H'://set the current thread
		{
			PTHREAD pthr = NULL;

			char * ptr = &buffer[2];
			int thread;
			hexToInt(&ptr, &thread);
			int threadid = thread - 1; //Ghetto: gdb doesn't like thread id 0
			if(threadid >= 0)
			{
				pthr = thread_get_pool(threadid);
			}
					if(buffer[1] == 'c')
					{
						if(thread == 0)
						{
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
						if(thread == 0)
						{
							putpacket("OK");
							break;
						}

						if(pthr)
							if(pthr->Valid)
							{
								otherthread = pthr;
								putpacket("OK");
								break;
							}

					}
					putpacket("E01");



			break;
		}
		case '?': //Halt reason
		{
			char buffer[4];
			sprintf(buffer, "S%02.2X", signal);
			putpacket(buffer);
			break;
		}
		case 'g': //Read registers
		{
			char *ptr = buffer;

			if(otherthread == NULL)
			{
				putpacket("E01");
				break;
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

		    putpacket(buffer);
			break;
		}
		case 'D'://Detach
		{
			attached = 0;
			active = 0;
			running = 1;
			resume_threads();
			putpacket("OK");
			return 1;
			break;
		}
		case 'm'://Read memory
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
		case 'c'://Continue
		{
			//TODO
			active = 0;
			running = 1;
			signal = 3; //Reset signal
			resume_threads();
			break;
		}
		case 'C'://Continue with signal
		{
			int val;
			char * ptr = &buffer[1];
			hexToInt(&ptr, &val);
			signal = val;
			active = 0;
			running = 1;
			resume_threads();
			break;
		}
		case 'S'://Step with signal
		{
			int val;
			char * ptr = &buffer[1];
			hexToInt(&ptr, &val);
			signal = val;
			PTHREAD pthr = ctrlthread;
			if(pthr == NULL)
			{
				ctrlthread = thread_get_pool(0);
			}
			//printf("Enabling single step at 0x%llX\n", pthr->Context.Iar);
			pthr->Context.Msr |= 0x400;//Enable single step

			active = 0;
			running = 1;
			resume_threads();
			break;
		}
		case 's'://step
		{
			PTHREAD pthr = ctrlthread;
			if(pthr == NULL)
			{
				ctrlthread = thread_get_pool(0);
			}
			//printf("Enabling single step at 0x%llX\n", pthr->Context.Iar);
			pthr->Context.Msr |= 0x400;//Enable single step

			active = 0;
			running = 1;
			resume_threads();
			break;
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
		case 'z':
		{
			int ret,type,len;
			char *addr;

			ret = parsezbreak(buffer,&type,&addr,&len);
			if(!ret) {
				putpacket("E01");
				break;
			}
			if(type!=0) break;

			if(len<4) {
				putpacket("E02");
				break;
			}

			remove_breakpoint((unsigned int *)addr);

			putpacket("OK");
			break;
		}
		case 'Z':
		{
			int ret,type,len;
			char *addr;

			ret = parsezbreak(buffer,&type,&addr,&len);
			if(!ret) {
				putpacket("E01");
				break;
			}
			if(type!=0) break;

			if(len<4) {
				putpacket("E02");
				break;
			}

			set_breakpoint((unsigned int *)addr);

			putpacket("OK");
			break;
		}
		default:
		{
			printf("Got packet %c\n", buffer[0]);//Unknown packet
			putpacket("");//Yea we don't support that
			break;
		}

	}
	return 0;

}

void net_server()
{
	int port = 2159;

	set_mode(1);//network

	printf("Gdb server starting up on port %i\n", port);

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

	unsigned int ip = sockaddr.sin_addr.s_addr;
	printf("Connected to %d.%d.%d.%d\n", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);

	char buffer[BUFMAX];

	while(1)
	{
		//printf("Waiting for a packet\n");
		int ret = net_getpacket(buffer);
		if(ret == -1)
		{
			//close(sock_fd); The socket is already closed by now(hopefully)
			break;
		}

		ret = parse_cmd(buffer);

		if(ret == 1)
		{
			//close(sock_fd); NO
			break;
		}


	}


	}

}

void gdbserver()//UART Version, not usable at this time
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

		case EXCEPT_CODE_TRACE:
			signal = 5;
			//printf("Got a trace at %llX\n", context->Iar);
		break;

		default:
			printf("Unknown except code: %i\n", code);
			signal = 0;
		break;
	}

	PROCESSOR_DATA_BLOCK *block;
	block = (unsigned int *)(context->Gpr[13]);//more lame 64bit->32bit ghettocode

	PTHREAD thread = block->CurrentThread;


	if(context->Msr && 0x400)//If we are single stepping then disable single step and trap
	{
		context->Msr &= ~0x400;
	}

	otherthread = thread;

	halt_threads_nolock();

	running = 0;

	if(code == 5)
	{
		thread->WaitingInHandler = 1;
		while(thread->WaitingInHandler);
	}

    return 1;
}

static PTHREAD gdbthread;

// Setup the gdb stub
void gdb_init()
{
	ctrlthread = thread_get_pool(0);
	otherthread = thread_get_pool(0);
    printf("Creating gdb thread\n");
    gdbthread = thread_create(net_server, 0, 0, 0);
    thread_set_name(gdbthread, "gdb");
    debugRoutine = gdb_debug_routine;
}

void gdb_stop()
{
	//end the thread here...?
	//TODO
}

