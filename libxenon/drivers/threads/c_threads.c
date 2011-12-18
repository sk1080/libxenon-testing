#include <threads/threads.h>
#include <ppc/register.h>
#include <time/time.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_secotp.h>
#include <stdio.h>

#include "ppc/atomic.h"

char processor_blocks[0x1000 * 6] = {0}; // Per-Processor Core Data Block
char processor_interrupt_stack[0x1000 * 6] = {0}; // interrupt handling stack

// Enough stack for the other threads to init on
char thread_init_stack[0x1000 * 5];
static unsigned int threads_online = 0;

// Random Data
PROCESSOR_DATA_BLOCK *Processors[6] = {0}; // Array of all processor pointers
THREAD_LIST ThreadList; // List of all threads
static unsigned int ThreadListLock = 0; // Lock for the thread list

static void thread_interrupt_unexpected(unsigned int soc_interrupt, unsigned int irq)
{
    // Simple interrupt acknowledge
    std((volatile void*)(soc_interrupt + 0x60), 0);
}

static void thread_interrupt_clock(unsigned int soc_interrupt, unsigned int irq)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Set IRQ
    std((volatile void*)(soc_interrupt + 8), 0x74);
    processor->Irq = 0x74;
    ld((volatile void*)(soc_interrupt + 8));
    
    stw((volatile void*)0xEA00106C, 0x100);
    std((volatile void*)(soc_interrupt + 0x10), 0x3E0070);
    
    // Here is where we would poll the debugger for a break-in
    
    // Here is where we would update timers, etc
    
    std((volatile void*)(soc_interrupt + 0x68), irq);
    processor->Irq = irq;
    ld((volatile void*)(soc_interrupt + 8));
}

static void thread_interrupt_ipi_clock(unsigned int soc_interrupt, unsigned int irq)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Set IRQ
    std((volatile void*)(soc_interrupt + 8), 0x74);
    processor->Irq = 0x74;
    ld((volatile void*)(soc_interrupt + 8));
    
    std((volatile void*)(soc_interrupt + 0x68), irq);
    processor->Irq = irq;
    ld((volatile void*)(soc_interrupt + 8));
}

static void thread_interrupt_ipi(unsigned int soc_interrupt, unsigned int irq)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Set IRQ
    std((volatile void*)(soc_interrupt + 8), 0x78);
    processor->Irq = 0x78;
    ld((volatile void*)(soc_interrupt + 8));
    
    // Process IPIs here
    
    std((volatile void*)(soc_interrupt + 0x68), irq);
    processor->Irq = irq;
    ld((volatile void*)(soc_interrupt + 8));
}

static void thread_interrupt_spurious(unsigned int soc_interrupt, unsigned int irq)
{
    // Don't do anything here
}

void external_interrupt_handler()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    unsigned int soc_interrupt =
        0x20050000 + (processor->CurrentProcessor * 0x1000);
    unsigned long long interruptSource
        = ld((volatile void*)(soc_interrupt + 0x50));
    
    //printf("External Interrupt %02X on Processor %i\n",
      //      interruptSource, processor->CurrentProcessor);
    
    processor->InterruptTable[interruptSource / 4](soc_interrupt, processor->Irq);
    
    //processor->MSRSave &= ~0x8000; // Disable external interrupts
}

void thread_idle_loop()
{
    for(;;)
    {
        // We dont do much here
        asm volatile("db16cyc");
        
        // TODO: check threads that can be swapped to, etc
        delay(5);
        printf("Thread %i Spin\n", thread_get_processor_block()->CurrentProcessor);
    }
}

// This function sets up the thread state
// meant to be called in the context of the thread
void thread_startup()
{
    int i;
    
    // Init processor state
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    printf("Processor %i Begin Init\n",
            processor->CurrentProcessor);
    
    // Setup interrupts
    for(i = 0;i < 0x20;i++)
        processor->InterruptTable[i] = thread_interrupt_unexpected;
    processor->InterruptTable[0x1E] = thread_interrupt_ipi;
    processor->InterruptTable[0x1F] = thread_interrupt_spurious;
    if(processor->CurrentProcessor == 0)
        processor->InterruptTable[0x1D] = thread_interrupt_clock;
    else
        processor->InterruptTable[0x1C] = thread_interrupt_ipi_clock;
    
    printf("Processor %i Interrupts Assigned\n",
            processor->CurrentProcessor);
    
    // Signal ready
    atomic_inc(&threads_online);
    
    printf("Processor %i Ready\n",
            processor->CurrentProcessor);
    
    printf("Processor %i Enabling Interrupts\n",
            processor->CurrentProcessor);
    
    mtmsr(mfmsr() | 0x8000);
    
    // Main thread can leave now
    if(processor->CurrentProcessor == 0)
        return;
    
    printf("Processor %i Begin Idle Loop\n",
            processor->CurrentProcessor);
    
    thread_idle_loop();
}

// Starts up threading alltogether
void threading_init()
{
    int i;
    
    static int threading_init_check = 0;
    if(threading_init_check)
    {
        printf("Threading already init'd!\n");
        return;
    }
    threading_init_check = 1;
    
    // Init the base threading stuff
    printf("Thread subinit\n");
    xenon_thread_startup();
    
    printf("Init process objects\n");
    Processors[0] = thread_get_processor_block();
    for(i = 0;i < 5;i++)
        Processors[i+1] = (PROCESSOR_DATA_BLOCK*)((char*)Processors[i] + 0x1000);
    
    printf("Init thread list\n");
    ThreadList.FirstThread = ThreadList.LastThread = NULL;
    
    // Set the cores to full speed
    printf("Pulling CPU to full speed\n");
    xenon_make_it_faster(XENON_SPEED_FULL);
    
    printf("Telling other threads to run init\n");
    
    // Tell the other threads to run "thread_startup"
    for(i = 1;i < 6;i++)
    {
        int res = xenon_run_thread_task(i,
                thread_init_stack + ((i - 1) * 0x1000) + (0x1000-0x8),
                thread_startup);
        
        if(res)
                printf("Thread %i failed to run init\n", i);
    }
    
    printf("Running init on main thread\n");
    
    // Run thread_startup ourself
    thread_startup();
    
    printf("Waiting for other threads to pull online\n");
    
    // Wait for the threads to complete startup
    while(threads_online !=  6)
        stall_execution(0xA);
    
    printf("All threads online!\n");
    
    printf("Thread init complete!\n");
}