#ifndef __drivers_time_time_h
#define __drivers_time_time_h

#ifdef __cplusplus
extern "C" {
#endif

void stall_execution(int i); // Stalls for i microseconds
void udelay(int);
void mdelay(int);
void delay(int);

#ifdef __cplusplus
};
#endif

#endif
