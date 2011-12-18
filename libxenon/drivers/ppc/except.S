.section ".except"

#include <ppc/xenonsprs.h>

//#define DEBUG 1
#define PAGE_BITS 12

#define OFF_REG(r) r * 8
#define REG_LR 32
#define REG_CTR 33
#define REG_CR 34
#define REG_XER 35
#define REG_SRR0 36
#define REG_SRR1 37
#define REG_DAR 38
#define REG_PIR 39

.extern pagetable

.globl exc_base
exc_base:

.globl secondary_lock
secondary_lock:
	.long 0

.globl secondary_alive
secondary_alive:
	.long 0

debug_lock:
	.long 0

#define DO_EXCEPT(addr, fnc) \
. = exc_base + addr;  \
    mtsprg0 %r31; \
    mfsprg1 %r31; \
    std %r3, OFF_REG(3)(%r31); \
    mflr %r3; \
    std %r3, OFF_REG(REG_LR)(%r31); \
    bl except_savevars; \
    mtlr %r3; \
    li %r3, addr; \
    b fnc

//ex_reset:
. = exc_base + 0x100
	mfspr   %r3, hid1
	li      %r4, 3
	rldimi  %r3, %r4, 58,4
	rldimi  %r3, %r4, 38,25
	sync
	mtspr   hid1, %r3
	sync
	isync
	mflr %r7
	mtsrr0  %r7
	mtsrr1  %r8
	rfid

//DO_EXCEPT(0x00000100, ex_reset);
DO_EXCEPT(0x00000200, ex_machine_check);
DO_EXCEPT(0x00000300, ex_data_storage);
DO_EXCEPT(0x00000380, ex_data_segment);
DO_EXCEPT(0x00000400, ex_instruction_storage);
DO_EXCEPT(0x00000480, ex_instruction_segment);

//DO_EXCEPT(0x00000500, ex_external);
. = exc_base + 0x500
ex_external:
        // Save registers
        mtsprg0 %r13
        mfspr %r13, hsprg0 // Put the processor data block on r13
        std %r3, OFF_REG(3)(%r13)
        mflr %r3
        std %r3, OFF_REG(REG_LR)(%r13)
        mfctr %r3
        std %r3, OFF_REG(REG_CTR)(%r13)
        mfcr %r3
        std %r3, OFF_REG(REG_CR)(%r13)
        mfxer %r3
        std %r3, OFF_REG(REG_XER)(%r13)
        mfsprg0 %r3
        std %r3, OFF_REG(13)(%r13)

        // Save both return registers
        mfsrr0 %r3
        std %r3, OFF_REG(REG_SRR0)(%r13)
        mfsrr1 %r3
        std %r3, OFF_REG(REG_SRR1)(%r13)

        // Setup the MSR
        lis %r3, 0x1000
        sldi %r3, %r3, 32 // Hypervisor Mode
        ori %r3, %r3, 0x3030 // Data/Instruction relocate/Machine Check/FPU
        oris %r3, %r3, 0x200 // VPU
        mtsrr1 %r3

        // Setup r13
        addis %r13, %r13, 0x8000

        // Setup return address
        lis %r3, asm_external_interrupt_handler@h
        ori %r3, %r3, asm_external_interrupt_handler@l
        mtsrr0 %r3

        // Launch handler
        rfid

DO_EXCEPT(0x00000600, ex_alignment);
DO_EXCEPT(0x00000700, ex_program);
DO_EXCEPT(0x00000800, ex_fpu_unavailable);

//DO_EXCEPT(0x00000900, ex_decrementer);
. = exc_base + 0x900
ex_decrementer:
        rfid
        mtsprg0 %r4
	lis %r4, 0x7FFF
	ori %r4, %r4, 0xFFFF
	mtspr dec, %r4
        mfsprg0 %r4
        rfid

//DO_EXCEPT(0x00000980, ex_hypervisor_decrementer);
. = exc_base + 0x980
ex_hypervisor_decrementer:
        hrfid
        mtsprg0 %r4
	lis %r4, 0x7FFF
	ori %r4, %r4, 0xFFFF
	mtspr hdec, %r4
        mfsprg0 %r4
        hrfid

DO_EXCEPT(0x00000A00, ex_reserved);
DO_EXCEPT(0x00000C00, ex_system_call);
DO_EXCEPT(0x00000D00, ex_trace);
//DO_EXCEPT(0x00000E00, ex_fpu_assist);
. = exc_base + 0xE00
ex_fpu_assist:
        addi %r3, %r3, 0x80
        bdnz+ _fpu_assist_loop
_fpu_assist_exit:
        sync
        isync
	blr
_fpu_assist_loop:
        dcbst %r0, %r3
        icbi %r0, %r3
        addi %r3, %r3, 0x80
        bdnz+ _fpu_assist_loop
        b _fpu_assist_exit

DO_EXCEPT(0x00000F20, ex_vpu_unavailable);
DO_EXCEPT(0x00001600, ex_maintenance);
DO_EXCEPT(0x00001700, ex_vmx_assist);
DO_EXCEPT(0x00001800, ex_thermal_management);

//1a8: 0000000000007f85 00000000ea0000ac 000000007f8xxxxx (l=1) -> 00000000ea000000, N=1, WIMG=5, pp=0, lp=0

ex_machine_check:
	b dump

ex_data_storage:

#if DEBUG
	mflr %r2
	li %r3, 'D'
	bl putc
	li %r3, ' '
	bl putc
	mtlr %r2
	li %r3, 's'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, '0'
	bl putc
	li %r3, '='
	bl putc
	mfsrr0 %r3
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'D'
	bl putc
	li %r3, 'A'
	bl putc
	li %r3, 'R'
	bl putc
	li %r3, '='
	bl putc
	bl putc
	mfdar %r3 // get faulting address
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'D'
	bl putc
	li %r3, 'S'
	bl putc
	li %r3, 'I'
	bl putc
	li %r3, 'S'
	bl putc
	li %r3, 'R'
	bl putc
	li %r3, '='
	bl putc
	mfspr %r3, 18 // dsisr
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'H'
	bl putc
	li %r3, 'i'
	bl putc
	li %r3, 'n'
	bl putc
	li %r3, 't'
	bl putc
	li %r3, '='
	bl putc
#endif

#if DEBUG
	mfspr %r3, PpeTlbIndexHint
	bl puthex
	li %r3, ','
	bl putc
#endif

	mfdar %r3 // get faulting address
        rldicl %r4, %r3, 36, 60 // extract high nibble (right aligned)

	sldi %r4, %r4, 2 /* address pagetable */
	addis %r4, %r4, pagetable@h - 0x80000000 
	lwz %r4, pagetable@l(%r4) /* load physical addr & attribs */

	cmpldi %r4, 0
	beq page_fault

	cmpwi %r4, -1
	beq user_page_handler

	rldicl %r3, %r3, 0, 36  // &0x0FFFFFFF
	rldicr %r5, %r4, 10, 40 // shift left addr << 10
	rldicl %r6, %r4, 0, 50  // leave lower 14 bits of RPN
	or %r3, %r3, %r5        //r3 now contains the phys addr

2:
	mfspr %r4, PpeTlbIndexHint

	rldicl %r2, %r3, 64-PAGE_BITS, 64-PAGE_BITS // page nr

	rldicr %r2, %r2, 0, 47 // round down to 64K

	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26

	mr %r15, %r4  /* page number */
#if DEBUG
	mr %r16, %r3

	li %r3, 'r'
	bl putc
	li %r3, '='
	bl putc
	mr %r3, %r16
1:
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r15
	bl puthex
	li %r3, ' '
	bl putc
	mr %r3, %r16
#endif

// 00001120_00000898
	mtspr PpeTlbIndex, %r15

		/* ARPN */
	rldicr %r2, %r3, 0, 47 // round down to 64K

	or %r2, %r2, %r6 /* OR in page attributes */

	mfdar %r15
///	mr %r15, %r3

#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc
#endif

	mtspr PpeTlbRpn, %r2

			/* calculate VPN */

	rldicr %r2, %r15, 60 - PAGE_BITS, 56 // AVPN
	rldicl %r2, %r2, 0, 20 // whatever
	ori %r2, %r2, 5 // set V|L  (valid, large)
#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, '\n'
	bl putc
#endif

// 00000000_00004481

	mtspr PpeTlbVpn, %r2
	
	b except_return

ex_data_segment:
	b dump

ex_instruction_storage:

#if DEBUG
	mflr %r2
	li %r3, 'I'
	bl putc
	li %r3, ' '
	bl putc
	mtlr %r2
	li %r3, 's'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, '0'
	bl putc
	li %r3, '='
	bl putc
	mfsrr0 %r3
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'H'
	bl putc
	li %r3, 'i'
	bl putc
	li %r3, 'n'
	bl putc
	li %r3, 't'
	bl putc
	li %r3, '='
	bl putc
#endif

#if DEBUG
	mfspr %r3, PpeTlbIndexHint
	bl puthex
	li %r3, ','
	bl putc
#endif

	mfsrr0 %r3 // get faulting address

	rldicl %r4, %r3, 36, 60 // extract high nibble

	sldi %r4, %r4, 2
	addis %r4, %r4, pagetable@ha - 0x80000000
	lwz %r4, pagetable@l(%r4)

	cmpldi %r4, 0
	beq page_fault

	

	rldicl %r3, %r3, 0, 36 // extract rest of addr
	rldicr %r5, %r4, 10, 40
	rldicl %r6, %r4, 0, 50
	or %r3, %r3, %r5
		// r3 now contains the phys addr

	mfspr %r4, PpeTlbIndexHint

	rldicl %r2, %r3, 64-PAGE_BITS, 64-PAGE_BITS-1 // page nr
	rldicr %r2, %r2, 0, 47 // round down to 64K

	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26

	mr %r15, %r4
#if DEBUG
	mr %r16, %r3

	li %r3, 'r'
	bl putc
	li %r3, '='
	bl putc
	mr %r3, %r16
1:
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r15
	bl puthex
	li %r3, ' '
	bl putc
	mr %r3, %r16
#endif

// 00001120_00000898
	mtspr PpeTlbIndex, %r15

		/* ARPN */
	rldicr %r2, %r3, 0, 47 // round down to 64K

	or %r2, %r2, %r6

	mfsrr0 %r15
///	mr %r15, %r3

#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc
#endif

	mtspr PpeTlbRpn, %r2

	rldicr %r2, %r15, 60 - PAGE_BITS, 56 // AVPN
	rldicl %r2, %r2, 0, 20 // whatever
	ori %r2, %r2, 5 // set valid
#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, '\n'
	bl putc
#endif

// 00000000_00004481

	mtspr PpeTlbVpn, %r2
	b except_return

ex_instruction_segment:
	b dump

ex_alignment:
	b dump

ex_program:
	b dump

ex_fpu_unavailable:
	b dump

ex_reserved:
	b dump

ex_system_call:
	b dump

ex_trace:
	b dump

ex_vpu_unavailable:
	b dump

ex_maintenance:
	b dump

ex_vmx_assist:
	b dump

ex_thermal_management:
	b dump



user_page_handler:
	mr %r15, %r3

	rldicl %r4,%r15,48,50 // calc idx in userpagetable
	sldi %r4,%r4,2

	addis %r4, %r4, userpagetable@ha - 0x80000000
	lwz %r4, userpagetable@l(%r4)

	mfdsisr %r6
	rldicl %r6,%r6,39,63

		/* no mapping -> segfault */
	cmplwi %r4,0
	beq user_segfault_handler

		/* if write in read only page, segfault too */
	rldicl %r5,%r4,0,62
	cmplwi %r5,3
	bne 1f
	cmplwi %r6,1
	bne 1f
	b user_segfault_handler
1:

	rldicr %r5, %r4, 0, 50  
	rldicl %r6, %r4, 0, 50  // leave lower 14 bits of RPN

	mfspr %r4, PpeTlbIndexHint
	
		/* index + LVPN */
	srdi %r2,%r15,12
	andi. %r2,%r2,0x7ff
	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26
	mtspr PpeTlbIndex, %r4

		/* ARPN */
	rldicr %r2, %r5, 0, 47 // round down to 64K
	or %r2, %r2, %r6 /* OR in page attributes */
	mtspr PpeTlbRpn, %r2

		/* calculate AVPN */
	srdi %r2,%r15,12-7+11
	rldicl %r3,%r3,0,56
	ori %r2, %r2, 5 // set V|L  (valid, large)
	mtspr PpeTlbVpn, %r2

	b except_return

user_segfault_handler:
	lis %r9,vm_segfault_handler@ha - 0x80000000
	lwz %r9,vm_segfault_handler@l(%r9)

	li %r3,0
	cmplwi %r9,0
	beq dump_from_pagefault // no user handler -> call system handler

	/*
	calling user code can throw exceptions,
	to preserve original HV context base I increment context pointer before calling user
	and decrement it after, to create a sort of context stack
	*/

	mfsprg1 %r31
	mfsrr0 %r4
	std %r4, OFF_REG(REG_SRR0)(%r31)
	mfsrr1 %r4
	std %r4, OFF_REG(REG_SRR1)(%r31)
	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)


	mfsprg1 %r8
	addi %r8,%r8,0x200
	mtsprg1 %r8
	
	mfpir %r3
	mfsrr0 %r4
	mfdar %r5
	
	mfmsr %r8

	lis %r10, sfh32@h
	ori %r10, %r10, sfh32@l
	mtsrr0 %r10
	rfid

sfh32:
	mtsprg3 %r8

	mtctr %r9 
	bctrl

	mfsprg3 %r4
	mtsrr1 %r4
	
	lis %r4, sfh64@h - 0x80000000
	ori %r4, %r4, sfh64@l
	mtsrr0 %r4
	rfid

sfh64:
	mfsprg1 %r8
	subi %r8,%r8,0x200
	mtsprg1 %r8

	mfsprg1 %r31

	cmplwi %r3,0
	bne 1f
	ld %r3, OFF_REG(REG_SRR0)(%r31)
1:
	mtsrr0 %r3
	ld %r3, OFF_REG(REG_SRR1)(%r31)
	mtsrr1 %r3
	ld %r3, OFF_REG(31)(%r31)
	mtsprg0 %r3

except_savevars:
        std %r0, OFF_REG(0)(%r31)
        std %r1, OFF_REG(1)(%r31)
        std %r2, OFF_REG(2)(%r31)
        std %r4, OFF_REG(4)(%r31)
        std %r5, OFF_REG(5)(%r31)
        std %r6, OFF_REG(6)(%r31)
        std %r7, OFF_REG(7)(%r31)
        std %r8, OFF_REG(8)(%r31)
        std %r9, OFF_REG(9)(%r31)
        std %r10, OFF_REG(10)(%r31)
        std %r11, OFF_REG(11)(%r31)
        std %r12, OFF_REG(12)(%r31)
        std %r13, OFF_REG(13)(%r31)
        std %r14, OFF_REG(14)(%r31)
        std %r15, OFF_REG(15)(%r31)
        std %r16, OFF_REG(16)(%r31)
        std %r17, OFF_REG(17)(%r31)
        std %r18, OFF_REG(18)(%r31)
        std %r19, OFF_REG(19)(%r31)
        std %r20, OFF_REG(20)(%r31)
        std %r21, OFF_REG(21)(%r31)
        std %r22, OFF_REG(22)(%r31)
        std %r23, OFF_REG(23)(%r31)
        std %r24, OFF_REG(24)(%r31)
        std %r25, OFF_REG(25)(%r31)
        std %r26, OFF_REG(26)(%r31)
        std %r27, OFF_REG(27)(%r31)
        std %r28, OFF_REG(28)(%r31)
        std %r29, OFF_REG(29)(%r31)
        std %r30, OFF_REG(30)(%r31)
        mfsprg0 %r4
        std %r4, OFF_REG(31)(%r31)
        mfcr %r4
        std %r4, OFF_REG(REG_CR)(%r31)
        mfctr %r4
        std %r4, OFF_REG(REG_CTR)(%r31)
        mfxer %r4
        std %r4, OFF_REG(REG_XER)(%r31)
        blr
	
except_return:

	mfsprg1 %r31
	ld %r0, OFF_REG(0)(%r31)
	ld %r5, OFF_REG(5)(%r31)
	ld %r6, OFF_REG(6)(%r31)
	ld %r7, OFF_REG(7)(%r31)
	ld %r8, OFF_REG(8)(%r31)
	ld %r9, OFF_REG(9)(%r31)
	ld %r10, OFF_REG(10)(%r31)
	ld %r11, OFF_REG(11)(%r31)
	ld %r12, OFF_REG(12)(%r31)
	ld %r13, OFF_REG(13)(%r31)
	ld %r14, OFF_REG(14)(%r31)
	ld %r15, OFF_REG(15)(%r31)
	ld %r16, OFF_REG(16)(%r31)
	ld %r17, OFF_REG(17)(%r31)
	ld %r18, OFF_REG(18)(%r31)
	ld %r19, OFF_REG(19)(%r31)
	ld %r20, OFF_REG(20)(%r31)
	ld %r21, OFF_REG(21)(%r31)
	ld %r22, OFF_REG(22)(%r31)
	ld %r23, OFF_REG(23)(%r31)
	ld %r24, OFF_REG(24)(%r31)
	ld %r25, OFF_REG(25)(%r31)
	ld %r26, OFF_REG(26)(%r31)
	ld %r27, OFF_REG(27)(%r31)
	ld %r28, OFF_REG(28)(%r31)
	ld %r29, OFF_REG(29)(%r31)
	ld %r30, OFF_REG(30)(%r31)

	ld %r4, OFF_REG(REG_LR)(%r31)
	mtlr %r4

	ld %r4, OFF_REG(REG_CR)(%r31)
	mtcr %r4
	ld %r4, OFF_REG(REG_CTR)(%r31)
	mtctr %r4
	ld %r4, OFF_REG(REG_XER)(%r31)
	mtxer %r4

	ld %r4, OFF_REG(4)(%r31)
	ld %r3, OFF_REG(3)(%r31)
	ld %r2, OFF_REG(2)(%r31)
	ld %r1, OFF_REG(1)(%r31)
	mfsprg0 %r31

	rfid

dump:

//	li %r3, debug_lock@l
//	bl lock

.globl dump_from_pagefault
dump_from_pagefault:
	mfsprg1 %r31
	addi %r8,%r31,0x200 /* move to next context in stack in case there's nested segfaults */
	mtsprg1 %r8
	
	mfsrr0 %r4
	std %r4, OFF_REG(REG_SRR0)(%r31)
	mfsrr1 %r4
	std %r4, OFF_REG(REG_SRR1)(%r31)
	mfdar %r4
	std %r4, OFF_REG(REG_DAR)(%r31)
	mfpir %r4
	std %r4, OFF_REG(REG_PIR)(%r31)

	lis %r4,crashdump@h
	ori %r4,%r4,crashdump@l
	mtsrr0 %r4

        // Setup the MSR
        lis %r4, 0x1000
        sldi %r4, %r4, 32 // Hypervisor Mode
        ori %r4, %r4, 0x3030 // Data/Instruction relocate/Machine Check/FPU
        oris %r4, %r4, 0x200 // VPU
        mtsrr1 %r4

	mr %r30,%r3
	mr %r3,%r31
	li %r4,0x200
	bl memdcbf
	mr %r3,%r30

	oris %r4,%r31,0x8000

        // Setup the stack
        lis %r1, exception_stack@h
        ori %r1, %r1, exception_stack@l
        addi %r1, %r1, 4096-8 // Put at top of stack

	rfid

putc:
/*	mfpir %r4
	cmplwi %r4,0
	bnelr*/

	lis %r4, 0x8000
	ori %r4, %r4, 0x200
	rldicr  %r4, %r4, 32,31
	oris  %r4, %r4, 0xea00

	mtsprg3 %r3
	cmplwi %r3,'\n'
	bne notnl
	
	li %r3,'\r'
	slwi %r3, %r3, 24
	stw %r3, 0x1014(%r4)
	1:
		lwz %r3, 0x1018(%r4)
		rlwinm. %r3, %r3, 0, 6, 6
		beq 1b

notnl:
	mfsprg3 %r3

	slwi %r3, %r3, 24
	stw %r3, 0x1014(%r4)
	1:
		lwz %r3, 0x1018(%r4)
		rlwinm. %r3, %r3, 0, 6, 6
		beq 1b
	blr

page_fault:

	li %r3,0

	b dump_from_pagefault


puthex:
	mflr 0
	mr 30,3
	li 31,0
.L5:
	rldicl 9,30,4,60
	addi 31,31,1
	sldi 30,30,4
	addi 3,9,55+32
	cmpdi 7,9,9
	bgt 7,.L3
	addi 3,9,48
.L3:
	bl putc
	cmpdi 7,31,16
	bne 7,.L5
	mtlr 0
	blr
