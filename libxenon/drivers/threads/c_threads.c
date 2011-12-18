#include <threads/threads.h>
#include <ppc/register.h>
#include <time/time.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_smc/xenon_smc.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <time/time.h>
#include <ppc/atomic.h>

char processor_blocks[0x1000 * 6] = {0}; // Per-Processor Core Data Block
char processor_interrupt_stack[0x1000 * 6] = {0}; // interrupt handling stack

// Enough stack for the other threads to init on
char thread_init_stack[0x1000 * 5];
static unsigned int threads_online = 0;

// Random Data
PROCESSOR_DATA_BLOCK *Processors[6] = {0}; // Array of all processor pointers
THREAD_LIST ThreadList; // List of all threads
static unsigned int ThreadListLock = 0; // Lock for the thread list

// 20ms = 50000
static unsigned int decrementer_ticks = 50000; // Default decrementer value

static void thread_interrupt_unexpected(unsigned int soc_interrupt)
{
    // Simple interrupt acknowledge
    std((volatile void*)(soc_interrupt + 0x60), 0);
}

static void thread_interrupt_clock(unsigned int soc_interrupt)
{
    stw((volatile void*)0xEA00106C, 0x100);
    std((volatile void*)(soc_interrupt + 0x10), 0x3E0070);
    
    // Poll debugger
    
    // Update timers
    _clock_time += 10000;
    _system_time += 10000;
}

static void thread_interrupt_ipi_clock(unsigned int soc_interrupt)
{
}

static void thread_interrupt_ipi(unsigned int soc_interrupt)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Process the Ipi
    lock(&processor->IpiLock);
    
    // Run the function
    if(processor->IpiProc)
    {
        processor->IpiProc(processor->IpiContext);
    
        // Clear the Ipi
        processor->IpiProc = NULL;
    }
    
    // Increment the counter
    if(processor->IpiIncrement)
        atomic_inc((unsigned int*)processor->IpiIncrement);
    
    unlock(&processor->IpiLock);
}

static void thread_interrupt_hal_smm(unsigned int soc_interrupt)
{
    // TODO: THIS
    // We need DPCs so we can queue a dpc to handle the smc stuff
    /*
    unsigned int hal_offset = 0xEA000000;
    
    // Fetch
    unsigned int message = __builtin_bswap32(lwz(hal_offset + 0x1050));
    
    // Output
    printf("Hal Message: %08X\n", message);
    
    // Acknowledge
    stw(hal_offset + 0x1058, __builtin_bswap32(message));
     */
}

static void thread_interrupt_spurious(unsigned int soc_interrupt)
{
    // Don't do anything here
}

static void thread_interrupt_vdp_isr(unsigned int soc_interrupt)
{
    unsigned int vdp_offset = 0xEC800000;
    unsigned int fetch;
    
    fetch = lwz(vdp_offset + 0xEDC);
    stw(vdp_offset + 0x6540, 0);
    
    assert(!(fetch & 0x7FFFFFFE));
    if(fetch & 0x80000000)
    {
        stw(vdp_offset + 0xED8, -1);
        fetch = lwz(vdp_offset + 0xED4);
        if(fetch & 1)
        {
            fetch = lwz(vdp_offset + 0xECC);
            
            char * name;
            if(fetch & 0x40000000)
                name = "Host";
            else
                name = "CP";
            
            printf("RBBM read-error interrupt e:%d r:%d addr:%04X\n",
                    fetch >> 31, name, (fetch >> 2) & 0x7FFF);
        }
    }
}

void external_interrupt_handler()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    unsigned int soc_interrupt =
        0x20050000 + (processor->CurrentProcessor * 0x1000);
    unsigned long long interruptSource
        = ld((volatile void*)(soc_interrupt + 0x50));
    
    char irq = processor->Irq;
    if((unsigned int)processor->InterruptTable[interruptSource / 4]
            != (unsigned int)thread_interrupt_unexpected)
    {
        // Set our Irq
        irq = processor->Irq;
        std((volatile void*)(soc_interrupt + 8), interruptSource);
        processor->Irq = interruptSource;
            ld((volatile void*)(soc_interrupt + 8));
    }
        else irq = 255;
    
    processor->InterruptTable[interruptSource / 4](soc_interrupt);
    
    if(irq != 255)
    {
        // Restore our Irq
        std((volatile void*)(soc_interrupt + 0x68), irq);
        processor->Irq = irq;
        ld((volatile void*)(soc_interrupt + 8));
    }
}

void decrementer_interrupt_handler()
{
    // TODO: Manage quantums here
    
    // Reset the decrementer
    mtspr(dec, decrementer_ticks);
}

void ipi_send_packet(thread_ipi_proc entrypoint,
        unsigned int context,
        char processors, unsigned volatile int *count)
{
    int i;
    
    // First set the ipi data
    for(i = 0;i < 6;i++)
    {
        if(((processors) & (1 << i)) == 0)
            continue;
        
        // Lock and set data
        lock(&Processors[i]->IpiLock);
        Processors[i]->IpiProc = entrypoint;
        Processors[i]->IpiContext = context;
        Processors[i]->IpiIncrement = count;
        unlock(&Processors[i]->IpiLock);
    }
    
    // Request the IPI interrupt
    unsigned int soc_interrupt =
        0x20050000 + (thread_get_processor_block()->CurrentProcessor * 0x1000);
    
    // Request interrupt 0x78 on the processor mask
    std((volatile void*)(soc_interrupt + 0x10), ((int)processors << 16) | 0x78);
}

unsigned int thread_send_ipi(thread_ipi_proc entrypoint, unsigned int context)
{
    // The IPI Lock
    static unsigned int ipi_lock = 0;
    
    // How many have finished the IPI
    unsigned volatile int ipi_count = 0;
    
    // Return value
    unsigned int rval = 0;
    
    // Make sure we are running at dispatch level
    char irql;
    if(thread_get_processor_block()->Irq < 2)
        irql = thread_raise_irql(2);
    else
        irql = thread_get_processor_block()->Irq;
    
    // Make sure IPIs are one at a time
    lock(&ipi_lock);
    
    // Send the IPI to other processors
    ipi_send_packet(entrypoint, context,
            0x3F & ~(1 << thread_get_processor_block()->CurrentProcessor),
            &ipi_count);
    
    // Run the IPI ourselves
    thread_raise_irql(0x78);
    rval = entrypoint(context);
    
    // Wait for the other processors to finish
    while(ipi_count != 5)
        asm volatile("db16cyc");
    
    // Release the lock
    unlock(&ipi_lock);
    
    // Lower the irql and leave
    thread_lower_irql(irql);
    
    return rval;
}

int thread_raise_irql(unsigned int irql)
{
    //assert(thread_get_processor_block()->Irq <= irql);
    if(thread_get_processor_block()->Irq > irql)
    {
        printf("Thread %i tried to increase irq (%i) to a lesser value (%i)!\n",
                thread_get_processor_block()->CurrentProcessor,
                thread_get_processor_block()->Irq, irql);
    }
    
    char irq = thread_get_processor_block()->Irq;
    thread_get_processor_block()->Irq = irql;
    
    return irq;
}

int thread_lower_irql(unsigned int irql)
{
    //assert(thread_get_processor_block()->Irq >= irql);
    if(thread_get_processor_block()->Irq < irql)
    {
        printf("Thread %i tried to lower irq (%i) to a greater value (%i)!\n",
                thread_get_processor_block()->CurrentProcessor,
                thread_get_processor_block()->Irq, irql);
    }
    
    char irq = thread_get_processor_block()->Irq;
    thread_get_processor_block()->Irq = irql;
    
    return irq;
}

void thread_idle_loop()
{
    char proc = thread_get_processor_block()->CurrentProcessor;
    for(;;)
    {
        // We dont do much here
        //asm volatile("db16cyc");
        
        // TODO: check threads that can be swapped to, etc
        printf("%i %08X\n", proc, thread_get_processor_block());
        //printf("Thread %i Spin %08X\n",
//                thread_get_processor_block()->CurrentProcessor,
//                thread_get_processor_block());
        delay(5);
    }
}

// This function sets up the thread state
// meant to be called in the context of the thread
void thread_startup()
{
    int i;
    
    // Init processor state
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    processor->Irq = 2;
    
    // Setup interrupts
    for(i = 0;i < 0x20;i++)
        processor->InterruptTable[i] = thread_interrupt_unexpected;
    processor->InterruptTable[0x5] = thread_interrupt_hal_smm;
    processor->InterruptTable[0x16] = thread_interrupt_vdp_isr;
    if(processor->CurrentProcessor != 0)
        processor->InterruptTable[0x1C] = thread_interrupt_ipi_clock;
    else
        processor->InterruptTable[0x1D] = thread_interrupt_clock;
    processor->InterruptTable[0x1E] = thread_interrupt_ipi;
    processor->InterruptTable[0x1F] = thread_interrupt_spurious;
    
    // Signal ready
    atomic_inc(&threads_online);
    
    // Set the quantum length
    mtspr(dec, decrementer_ticks);
    
    // Enable interrupts
    mtmsr(mfmsr() | 0x8000);
    
    // Main thread can leave now
    if(processor->CurrentProcessor == 0)
        return;
    
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
    xenon_thread_startup();
    
    Processors[0] = thread_get_processor_block();
    for(i = 0;i < 5;i++)
        Processors[i+1] = (PROCESSOR_DATA_BLOCK*)((char*)Processors[i] + 0x1000);
    
    ThreadList.FirstThread = ThreadList.LastThread = NULL;
    
    // Set the cores to full speed
    xenon_make_it_faster(XENON_SPEED_FULL);
    
    // Main processor startup
    thread_startup();
    
    // Tell the other threads to run "thread_startup"
    for(i = 1;i < 6;i++)
    {
        int res = xenon_run_thread_task(i,
                thread_init_stack + ((i - 1) * 0x1000) + (0x1000-0x8),
                thread_startup);
        
        if(res)
                printf("Thread %i failed to run init\n", i);
    }
    
    // Wait for the threads to complete startup
    while(threads_online !=  6)
        stall_execution(0xA);
    
    printf("All threads online!\n");
    
    printf("Thread init complete!\n");
}