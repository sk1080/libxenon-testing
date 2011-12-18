#ifndef THREADS_H
#define	THREADS_H

#ifdef	__cplusplus
extern "C" {
#endif
    
typedef void (*thread_interrupt_proc)(unsigned int, unsigned int);
typedef void (*thread_ipi_proc)(unsigned int);

#pragma pack(push, 1)

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
    
    double Fpscr;             // Floating Point Status/Control
    double Fpr[32];
    
    unsigned long long UserModeControl;
    unsigned long long Fill;
    
    float Vscr[4];            // Vector Status/Control
    float Vr[128][4];         // Vector
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
    unsigned long long Reserved0[2]; // Reserved
    
    unsigned char CurrentProcessor;  // What processor are we? (offset 0x140)
    unsigned char Irq;               // Interrupt request level
    unsigned char Reserved1[2];         // Reserved
    
    // Thread List
    unsigned int ThreadListLock;     // Lock this when doing thread things
    struct _THREAD *FirstThread;
    struct _THREAD *LastThread;
    
    thread_interrupt_proc InterruptTable[0x20]; // Interrupt function pointers
    
    // IPI data
    unsigned int IpiLock; // Lock
    thread_ipi_proc IpiProc;
    unsigned int IpiContext;
    unsigned volatile int *IpiIncrement; // This ptr is incremented after Ipi completion
    
} PROCESSOR_DATA_BLOCK;

// The thread structure
typedef struct _THREAD
{
    // Thread Context
    PCONTEXT Context;
    
    // Assigned Processor
    PROCESSOR_DATA_BLOCK *ThisProcessor;
    
    // Thread List
    struct _THREAD *NextThread;
    struct _THREAD *PreviousThread;
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

// Thread management

// Returns thread id (can be used as handle)
unsigned int thread_create(void* entrypoint, int stack_size,
        void* argument, int create_suspended);

// Swap the thread's processor
void thread_setprocessor(unsigned int id, unsigned int processor);

// Suspend/Resume thread
void thread_suspend(unsigned int id);
void thread_resume(unsigned int id);
unsigned int thread_get_suspend_count(unsigned int id);

// Raise/Lower irql
int thread_raise_irql(unsigned int irql);
int thread_lower_irql(unsigned int irql);

int thread_spinlock(unsigned int *lock);
void thread_unlock(unsigned int *lock, unsigned int irql);

// Runs "entrypoint" on all processors at the same time
void thread_send_ipi(thread_ipi_proc entrypoint, unsigned int context);

#ifdef	__cplusplus
}
#endif

#endif	/* THREADS_H */

