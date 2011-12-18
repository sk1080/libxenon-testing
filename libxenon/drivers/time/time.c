#include <ppc/timebase.h>
#include <stdint.h>

void stall_execution(int i)
{
	i = i * 0x32;
	uint64_t t = mftb() + i;
	while(mftb() < t)
		asm volatile("db16cyc");
}

static void tdelay(uint64_t i)
{
	uint64_t t = mftb();
	t += i;
	while (mftb() < t)
            asm volatile("db16cyc");
	asm volatile("db16cyc");
}

void udelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000000);
}

void mdelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000);
}

void delay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u);
}
