#ifndef __drivers_time_time_h
#define __drivers_time_time_h

#ifdef __cplusplus
extern "C" {
#endif
    
extern unsigned long long _system_time; // Please dont touch this
extern unsigned long long _clock_time; // or this, most definitely this

unsigned long long get_system_time();
void set_system_time(unsigned long long time);
unsigned long long get_clock_time();
void stall_execution(int i); // Stalls processor for i microseconds
void udelay(int);
void mdelay(int);
void delay(int);

#ifdef __cplusplus
};
#endif

#endif
