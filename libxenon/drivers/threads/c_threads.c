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
#include <string.h>
#include <stdlib.h>

char processor_blocks[0x1000 * 6] = {0}; // Per-Processor Core Data Block
char processor_fpu_vpu_blocks[sizeof(PROCESSOR_FPU_VPU_SAVE) * 6] = {0};
char processor_interrupt_stack[0x1000 * 6] = {0}; // interrupt handling stack

// Enough stack for the other threads to init on
char thread_init_stack[0x1000 * 5];
static unsigned int threads_online = 0;

// Random Data
PROCESSOR_DATA_BLOCK *Processors[6] = {0}; // Array of all processor pointers
THREAD_LIST ThreadList; // List of all threads
THREAD ThreadPool[MAX_THREAD_COUNT] = {0}; // The thread pool

// Lock this if you ever have to lock more than one processor at once
// AND LOCK IT FIRST
static unsigned int ThreadListLock = 0; // Lock for the thread list


// 20ms = 50000
static unsigned int decrementer_ticks = 50000; // Default decrementer value

void save_floating_point(PROCESSOR_FPU_VPU_SAVE* ptr);
void restore_floating_point(PROCESSOR_FPU_VPU_SAVE* ptr);
void save_vector(PROCESSOR_FPU_VPU_SAVE* ptr);
void restore_vector(PROCESSOR_FPU_VPU_SAVE* ptr);

static void thread_interrupt_unexpected(unsigned int soc_interrupt)
{
    // Simple interrupt acknowledge
    std((volatile void*)(soc_interrupt + 0x60), 0);
}

unsigned long long _system_time = 0; // System Timer (Used for telling time)
unsigned long long _clock_time = 0; // Clock Timer (Used for timing stuff)
static void thread_interrupt_clock(unsigned int soc_interrupt)
{
    stw((volatile void*)0xEA00106C, 0x01000000);
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
    //save_floating_point();
    //printf("SMC\n");
    //restore_floating_point();
    //printf("SMC\n");
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
    
    fetch = lwz((volatile void*)(vdp_offset + 0xEDC));
    stw((volatile void*)(vdp_offset + 0x6540), 0);
    
    assert(!(fetch & 0x7FFFFFFE));
    if(fetch & 0x80000000)
    {
        stw((volatile void*)(vdp_offset + 0xED8), -1);
        fetch = lwz((volatile void*)(vdp_offset + 0xED4));
        if(fetch & 1)
        {
            fetch = lwz((volatile void*)(vdp_offset + 0xECC));
            
            char * name;
            if(fetch & 0x40000000)
                name = "Host";
            else
                name = "CP";
            
            save_floating_point(thread_get_processor_block()->FPUVPUSave);
            printf("RBBM read-error interrupt e:%d r:%d addr:%04X\n",
                    fetch >> 31, name, (fetch >> 2) & 0x7FFF);
            restore_floating_point(thread_get_processor_block()->FPUVPUSave);
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

// Dumps the context from the processor to the current thread object
void dump_thread_context()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    PTHREAD pthr = processor->CurrentThread;
    
    // Copy main regs
    memcpy(pthr->Context.Gpr, processor->RegisterSave, 32*8);
    pthr->Context.Msr = processor->MSRSave;
    pthr->Context.Iar = processor->IARSave;
    pthr->Context.Cr = processor->CRSave;
    pthr->Context.Ctr = processor->CTRSave;
    pthr->Context.Lr = processor->LRSave;
    pthr->Context.Xer = processor->XERSave;
    
    // Dump floating point
    save_floating_point(&pthr->Context.FpuVpu);
    
    // Dump vector
    //save_vector(&pthr->Context.FpuVpu); // UNUSED
}

// Restores the current thread context to the processor
void restore_thread_context()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    PTHREAD pthr = processor->CurrentThread;
    
    // Copy main regs
    memcpy(processor->RegisterSave, pthr->Context.Gpr, 32*8);
    processor->MSRSave = pthr->Context.Msr;
    processor->IARSave = pthr->Context.Iar;
    processor->CRSave = pthr->Context.Cr;
    processor->CTRSave = pthr->Context.Ctr;
    processor->LRSave = pthr->Context.Lr;
    processor->XERSave = pthr->Context.Xer;
    
    // Flush floating point
    restore_floating_point(&pthr->Context.FpuVpu);
    
    // Flush vector
    // restore_vector(&pthr->Context.FpuVpu); // UNUSED
}

PTHREAD thread_schedule_core()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Walk the thread list, find the first thread with the highest priority
    //PTHREAD pthr = processor->FirstThread;
    PTHREAD pthr = processor->ListPtr;
    PTHREAD winThread = pthr;
    do
    {
        // Check for suspend
        if(pthr->SuspendCount == 0)
        {
                if((pthr->Priority + pthr->PriorityBoost)
                        > (winThread->Priority + winThread->PriorityBoost))
                    winThread = pthr;
                else if(((pthr->Priority + pthr->PriorityBoost)
                        < (winThread->Priority + winThread->PriorityBoost))
                        && (pthr->PriorityBoost < pthr->MaxPriorityBoost))
                    pthr->PriorityBoost++; // Boost the priority if need be
        }
        
        pthr = pthr->NextThread;
    } while(pthr != processor->ListPtr);
    processor->ListPtr = pthr->NextThread;
    return pthr;
}

// Schedules for the currently running thread, in the context of the thread
void thread_schedule_running()
{
    // todo: this
}

// Schedules for the current processes
void thread_schedule()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Lock the processor
    lock(&processor->Lock);
    
    // Flush context to the thread object
    dump_thread_context();
    
    // Mark current thread as not running
    processor->CurrentThread->ThreadIsRunning = 0;
    
    PTHREAD pthr = thread_schedule_core();
    
    // This thread is the winner, check the priority boost
    if(pthr->PriorityBoost)
        pthr->PriorityBoost--;
    if(pthr->PriorityBoost > pthr->MaxPriorityBoost)
        pthr->PriorityBoost = pthr->MaxPriorityBoost;
    
    // Mark as current thread
    processor->CurrentThread = pthr;
    
    // Mark as running
    pthr->ThreadIsRunning = 1;
    
    // Flush the context
    restore_thread_context();
    
    // Unlock the processor
    unlock(&processor->Lock);
}

void decrementer_interrupt_handler()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // We will only reschedule if the irq is less than 2 (DPC)
    if(processor->Irq < 2)
        thread_schedule(); // Quantum has ended!
    
    // Reset the decrementer (used as the quantum timer)
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
    
    printf("Lowing to %i Irq\n", irql);
    
    // Lower the irql and leave
    thread_lower_irql(irql);
    
    printf("Now at %i Irq\n", thread_get_processor_block()->Irq);
    
    return rval;
}

PTHREAD thread_pool_alloc()
{
    int i;
    for(i = 0;i < MAX_THREAD_COUNT;i++)
    {
        if(!ThreadPool[i].Valid && !ThreadPool[i].HandleOpen)
        {
            memset(&ThreadPool[i], 0, sizeof(THREAD));
            ThreadPool[i].HandleOpen = 1;
            ThreadPool[i].Valid = 1;
            ThreadPool[i].ThreadId = i;
            
            printf("Thread %i Allocated\n", i);
            
            return &ThreadPool[i];
        }
    }
    
    return NULL; // Out of threads!
}

void thread_insert(PTHREAD pthr)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Link the list
    pthr->NextThread = processor->FirstThread;
    pthr->PreviousThread = processor->LastThread;
    processor->FirstThread->PreviousThread = pthr;
    processor->LastThread->NextThread = pthr;
    processor->FirstThread = pthr;
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
        delay(5);
        printf("%i %08X\n", proc, thread_get_processor_block());
        //printf("Thread %i Spin %08X\n",
//                thread_get_processor_block()->CurrentProcessor,
//                thread_get_processor_block());
    }
}

void thread_terminate()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // We dont ever want to leave this function, ever
    thread_raise_irql(2);
    
    lock(&processor->Lock);
    // Mark the thread as invalid, causing it to never be scheduled again
    processor->CurrentThread->Valid = 0;
    unlock(&processor->Lock);
    
    // Put us down to zero (at this point, we may be eaten by the scheduler)
    thread_lower_irql(0);
    
    // Call for another thread to take its place
    thread_schedule();
}

void thread_proc_startup(thread_proc entrypoint, void* argument)
{
   printf("Thread Startup\n");
    
   int retval = entrypoint(argument);
    
   printf("Thread returned %i\n", retval);
    
   thread_terminate();
}

unsigned int thread_create(void* entrypoint, int stack_size,
        void* argument, int create_suspended)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    unsigned int thread_id = 0xFFFFFFFF;
    
    // Lock the processor
    int irql = thread_raise_irql(2);
    lock(&processor->Lock);
    
    // Round up to nearest 4kb, min 16kb
    if(stack_size < 16*1024)
        stack_size = 16*1024;
    if(stack_size & 0xFFF)
        stack_size = (stack_size & 0xFFFFF000) + 0x1000;
    
    char * stack = (char*)malloc(stack_size);
    
    if(stack != NULL)
    {
        // Get a thread object
        PTHREAD pthr = thread_pool_alloc();
        if(pthr != NULL)
        {
            // Setup thread vars
            pthr->StackBase = stack;
            pthr->StackSize = stack_size;
            pthr->Priority = 7; // Base priority
            pthr->MaxPriorityBoost = 5; // Default boost
            pthr->Valid = 1;
            
            // Insert into list
            unlock(&processor->Lock);
            lock(&ThreadListLock);
            lock(&processor->Lock);
            
            pthr->NextThread = processor->FirstThread;
            pthr->PreviousThread = processor->LastThread;
            processor->FirstThread->PreviousThread = pthr;
            processor->LastThread->NextThread = pthr;
            processor->FirstThread = pthr;
            
            pthr->NextThreadFull = ThreadList.FirstThread;
            pthr->PreviousThreadFull = ThreadList.LastThread;
            ThreadList.FirstThread->PreviousThreadFull = pthr;
            ThreadList.LastThread->NextThreadFull = pthr;
            ThreadList.FirstThread = pthr;
            
            unlock(&ThreadListLock);
            
            if(create_suspended)
                pthr->SuspendCount = 1;
            
            // Setup the registers
            pthr->Context.Gpr[1] = pthr->StackBase + pthr->StackSize - 8; // Stack Ptr
            pthr->Context.Iar = thread_proc_startup; // Address
            pthr->Context.Gpr[3] = entrypoint; // Entry point
            pthr->Context.Gpr[4] = argument; // Argument
            pthr->Context.Msr = 0x100000000200B030; // Machine State
            
            thread_id = pthr->ThreadId;
        }
    }
    
    unlock(&processor->Lock);
    thread_lower_irql(irql);
    
    return thread_id;
}

// This function sets up the thread state
// meant to be called in the context of the thread
void thread_startup()
{
    int i;
    static int thread_startup_lock = 0;
    
    // Init processor state
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    processor->Irq = 2;
    processor->FPUVPUSave =
            (PROCESSOR_FPU_VPU_SAVE*)(processor_fpu_vpu_blocks
            + processor->CurrentProcessor * sizeof(PROCESSOR_FPU_VPU_SAVE));
    
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
    
    // Set quantum length
    processor->QuantumEnd = decrementer_ticks;
    
    // Setup the idle thread
    lock(&processor->Lock);
    lock(&thread_startup_lock);
    PTHREAD pthr = thread_pool_alloc();
    unlock(&thread_startup_lock);
    pthr->ThisProcessor = processor;
    pthr->Valid = 1;
    
    // Set current thread
    processor->CurrentThread = pthr;
    
    // Insert into list
    processor->FirstThread = processor->LastThread
            = pthr->NextThread = pthr->PreviousThread = pthr;
    processor->ListPtr = processor->FirstThread;
    unlock(&processor->Lock);
    
    if(processor->CurrentProcessor == 0)
    {
        // Init main list
        ThreadList.FirstThread = pthr;
        ThreadList.LastThread = pthr;
        pthr->NextThreadFull = pthr->PreviousThreadFull = pthr;
        pthr->Priority = 7; // Default priority for main thread
    }
    
    // Insert into main list
    lock(&ThreadListLock);
    lock(&processor->Lock);
    ThreadList.FirstThread->PreviousThreadFull = 
            ThreadList.LastThread->NextThreadFull = pthr;
    pthr->NextThreadFull = ThreadList.FirstThread;
    pthr->PreviousThreadFull = ThreadList.LastThread;
    unlock(&processor->Lock);
    unlock(&ThreadListLock);
    
    
    // Signal ready
    atomic_inc(&threads_online);
    
    // Set the quantum length
    mtspr(dec, decrementer_ticks);
    
    // Enable interrupts
    mtmsr(mfmsr() | 0x8000);
    
    // Allow thread scheduling on this core
    processor->Irq = 0;
    
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