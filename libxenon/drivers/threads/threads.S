// Threading utility

#include <ppc\xenonsprs.h>

#define OFF_REG(r) r * 8
#define REG_LR 32
#define REG_CTR 33
#define REG_CR 34
#define REG_XER 35
#define REG_SRR0 36
#define REG_SRR1 37

// Threading stuff

.globl asm_external_interrupt_handler
asm_external_interrupt_handler:
    // Save registers 0-2, 4-12, 14-31
    std %r0, OFF_REG(0)(%r13)
    std %r1, OFF_REG(1)(%r13)
    std %r2, OFF_REG(2)(%r13)
    std %r4, OFF_REG(4)(%r13)
    std %r5, OFF_REG(5)(%r13)
    std %r6, OFF_REG(6)(%r13)
    std %r7, OFF_REG(7)(%r13)
    std %r8, OFF_REG(8)(%r13)
    std %r9, OFF_REG(9)(%r13)
    std %r10, OFF_REG(10)(%r13)
    std %r11, OFF_REG(11)(%r13)
    std %r12, OFF_REG(12)(%r13)
    std %r14, OFF_REG(14)(%r13)
    std %r15, OFF_REG(15)(%r13)
    std %r16, OFF_REG(16)(%r13)
    std %r17, OFF_REG(17)(%r13)
    std %r18, OFF_REG(18)(%r13)
    std %r19, OFF_REG(19)(%r13)
    std %r20, OFF_REG(20)(%r13)
    std %r21, OFF_REG(21)(%r13)
    std %r22, OFF_REG(22)(%r13)
    std %r23, OFF_REG(23)(%r13)
    std %r24, OFF_REG(24)(%r13)
    std %r25, OFF_REG(25)(%r13)
    std %r26, OFF_REG(26)(%r13)
    std %r27, OFF_REG(27)(%r13)
    std %r28, OFF_REG(28)(%r13)
    std %r29, OFF_REG(29)(%r13)
    std %r30, OFF_REG(30)(%r13)
    std %r31, OFF_REG(31)(%r13)

    // Setup the stack
    lis %r1, processor_interrupt_stack@h
    ori %r1, %r1, processor_interrupt_stack@l
    lbz %r4, 0x140(%r13) // Processor ID
    slwi %r4, %r4, 12
    add %r1, %r4, %r1
    addi %r1, %r1, 4096-8 // Put at top of stack

    // Launch the C handler
    bl external_interrupt_handler

    // Restore registers
    ld %r3, OFF_REG(REG_LR)(%r13)
    mtlr %r3
    ld %r3, OFF_REG(REG_CTR)(%r13)
    mtctr %r3
    ld %r3, OFF_REG(REG_CR)(%r13)
    mtcr %r3
    ld %r3, OFF_REG(REG_XER)(%r13)
    mtxer %r3
    ld %r3, OFF_REG(REG_SRR0)(%r13)
    mtsrr0 %r3
    ld %r3, OFF_REG(REG_SRR1)(%r13)
    mtsrr1 %r3
    ld %r0, OFF_REG(0)(%r13)
    ld %r1, OFF_REG(1)(%r13)
    ld %r2, OFF_REG(2)(%r13)
    ld %r3, OFF_REG(3)(%r13)
    ld %r4, OFF_REG(4)(%r13)
    ld %r5, OFF_REG(5)(%r13)
    ld %r6, OFF_REG(6)(%r13)
    ld %r7, OFF_REG(7)(%r13)
    ld %r8, OFF_REG(8)(%r13)
    ld %r9, OFF_REG(9)(%r13)
    ld %r10, OFF_REG(10)(%r13)
    ld %r11, OFF_REG(11)(%r13)
    ld %r12, OFF_REG(12)(%r13)
    ld %r14, OFF_REG(14)(%r13)
    ld %r15, OFF_REG(15)(%r13)
    ld %r16, OFF_REG(16)(%r13)
    ld %r17, OFF_REG(17)(%r13)
    ld %r18, OFF_REG(18)(%r13)
    ld %r19, OFF_REG(19)(%r13)
    ld %r20, OFF_REG(20)(%r13)
    ld %r21, OFF_REG(21)(%r13)
    ld %r22, OFF_REG(22)(%r13)
    ld %r23, OFF_REG(23)(%r13)
    ld %r24, OFF_REG(24)(%r13)
    ld %r25, OFF_REG(25)(%r13)
    ld %r26, OFF_REG(26)(%r13)
    ld %r27, OFF_REG(27)(%r13)
    ld %r28, OFF_REG(28)(%r13)
    ld %r29, OFF_REG(29)(%r13)
    ld %r30, OFF_REG(30)(%r13)
    ld %r31, OFF_REG(31)(%r13)
    ld %r13, OFF_REG(13)(%r13)

    // Leave the interrupt
    rfid