#ifndef THREADS_H
#define	THREADS_H

#ifdef	__cplusplus
extern "C" {
#endif
    
#define MAX_THREAD_COUNT 256
    
typedef void (*thread_interrupt_proc)(unsigned int);
typedef unsigned int (*thread_ipi_proc)(unsigned int);
typedef int (*thread_proc)(void*);

#pragma pack(push, 1)

typedef struct _PROCESSOR_FPU_VPU_SAVE
{
    double Fpr[32]; // Floating save
    double Fpscr; // Floating status save
    float VrSave[128][4]; // Vector save (UNUSED)
    float VscrSave[4]; // Vector status save (UNUSED)
} PROCESSOR_FPU_VPU_SAVE;

// Thread context
typedef struct _CONTEXT
{
    unsigned long long Msr;   // Machine State
    unsigned long long Iar;   // Instruction Address
    unsigned long long Lr;    // Link
    unsigned long long Ctr;   // Counter
    
    // General purpose
    unsigned long long Gpr[32];
    
    unsigned long long Cr;    // Condition
    unsigned long long Xer;   // Fixed Point Exception
    
    PROCESSOR_FPU_VPU_SAVE FpuVpu; // Floating/Vector save
} CONTEXT, *PCONTEXT;

// The structure that lives on register 13
typedef struct _PROCESSOR_DATA_BLOCK
{
    // Register space for external interrupts
    unsigned long long RegisterSave[32];
    unsigned long long LRSave;
    unsigned long long CTRSave;
    unsigned long long CRSave;
    unsigned long long XERSave;
    unsigned long long IARSave;      // Also SRR0 (0x120)
    unsigned long long MSRSave;      // Also SRR1 (0x128)
    PROCESSOR_FPU_VPU_SAVE *FPUVPUSave; // Saves the other regs (0x130)
    unsigned int Reserved3;
    unsigned long long Reserved0; // Reserved
    
    unsigned char CurrentProcessor;  // What processor are we? (offset 0x140)
    unsigned char Irq;               // Interrupt request level (offset 0x141)
    unsigned char Reserved1[2];         // Reserved
    
    // Thread List
    struct _THREAD *FirstThread;
    struct _THREAD *LastThread;
    struct _THREAD *ListPtr;
    
    thread_interrupt_proc InterruptTable[0x20]; // Interrupt function pointers
    
    // IPI data
    unsigned int IpiLock; // Lock
    thread_ipi_proc IpiProc;
    unsigned int IpiContext;
    unsigned volatile int *IpiIncrement; // This ptr is incremented after Ipi completion
    
    // Scheduling stuff
    long long QuantumEnd; // When this quantum ends (clock)
    struct _THREAD *CurrentThread; // Currently running thread
    
    // Locks
    unsigned int Lock; // To synchronize access
    
} PROCESSOR_DATA_BLOCK;

// The thread structure
typedef struct _THREAD
{
    // Thread Context
    CONTEXT Context;
    
    // Assigned Processor
    PROCESSOR_DATA_BLOCK *ThisProcessor;
    
    // Thread List
    struct _THREAD *NextThread;
    struct _THREAD *PreviousThread;
    
    // List of all threads
    struct _THREAD *NextThreadFull;
    struct _THREAD *PreviousThreadFull;
    
    // If the object is valid
    unsigned char Valid;
    // Priority
    unsigned char Priority;
    // Priority boost
    unsigned char PriorityBoost;
    // Maximum Priority Boost
    unsigned char MaxPriorityBoost;
    // If we are currently running this thread
    unsigned char ThreadIsRunning;
    // Our suspend count
    unsigned char SuspendCount; // A count of zero means we can't be resumed anymore (running)
    // If the handle is still open
    unsigned char HandleOpen; // You have to close this before we dealloc the thread object!!
    // The thread ID (its unique, i promise)
    unsigned char ThreadId;
    
    void *DebugData; // Just a pointer so you can stick whatever you want on the object
    
    // To wake a thread up from sleep,
                // just set SleepTime to zero, scheduler will handle the rest
    long long SleepTime; // How long until we wake up (milliseconds * 2500)
    
    // TODO: Have a list of objects that if signaled, will wake us up
    
    char * StackBase; // The bottom of our stack
    unsigned int StackSize; // The size of our stack
    
} THREAD, *PTHREAD;

// A list of threads
typedef struct _THREAD_LIST
{
    PTHREAD FirstThread;
    PTHREAD LastThread;
} THREAD_LIST, *PTHREAD_LIST;
#pragma pack(pop)

#define thread_get_processor_block() ({PROCESSOR_DATA_BLOCK *rval; \
      asm volatile("mr %0, 13" : "=r" (rval)); rval;})

// Init
void threading_init();
// Shutdown
void threading_shutdown();

// Thread management

#define THREAD_FLAG_CREATE_SUSPENDED 1 // Creates the thread with 1 suspend

// Returns thread pointer
PTHREAD thread_create(void* entrypoint, unsigned int stack_size,
        void* argument, unsigned int flags);

// Call this to free up the handle for use by other threads, after calling
// DO NOT TOUCH THE HANDLE AGAIN
void thread_close(PTHREAD pthr);

// Swap the thread's processor
void thread_set_processor(PTHREAD pthr, unsigned int processor);

// Suspend/Resume thread
// Both return the value of the suspend count BEFORE the function goes through
// If it can't suspend/resume more, it returns -1
int thread_suspend(PTHREAD pthr);
int thread_resume(PTHREAD pthr);

// Set thread priority (0-15), 7 is default, 0 is idle thread
void thread_set_priority(PTHREAD pthr, unsigned int priority);

// Set the quantum for the scheduling engine, default quantum is 20ms
void process_set_quantum_length(unsigned int milliseconds);

// End this thread
void thread_terminate(unsigned int returnCode);

// Sleep
void thread_sleep(int milliseconds);

// Raise/Lower irql
int thread_raise_irql(unsigned int irql);
int thread_lower_irql(unsigned int irql);

// Raises IRQL to 2, then locks
int thread_spinlock(unsigned int *addr);
// Unlocks, then lowers irql to irql
void thread_unlock(unsigned int *addr, unsigned int irql);

// Runs "entrypoint" on all processors at the same time
// Return value is value of processor this is called on
unsigned int thread_send_ipi(thread_ipi_proc entrypoint, unsigned int context);

// These disable and enable interrupts, which also disables scheduling
// USE SPARINGLY!!
// (These only affect the core you are on)
unsigned int thread_disable_interrupts();
void thread_enable_interrupts(unsigned int msr);

// Flush context
void save_floating_point(PROCESSOR_FPU_VPU_SAVE* ptr);
void restore_floating_point(PROCESSOR_FPU_VPU_SAVE* ptr);
void save_vector(PROCESSOR_FPU_VPU_SAVE* ptr);
void restore_vector(PROCESSOR_FPU_VPU_SAVE* ptr);

#ifdef	__cplusplus
}
#endif

#endif	/* THREADS_H */

