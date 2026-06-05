/* Copyright 2026 NPU IP */
/* Startup code for NPU Control Core Firmware */

.section .text.init
.globl _start
_start:
    /* Initialize stack pointer to top of TCDM (0x10000000 + 256KB) */
    li sp, 0x10040000

    /* Enable Machine External Interrupts in mie (bit 11) so wfi wakes up */
    li   t0, 0x800
    csrs mie, t0

    /* Zero .bss section in TCDM */
    la   t0, _bss_start
    la   t1, _bss_end
    beq  t0, t1, bss_done
bss_loop:
    sw   zero, 0(t0)
    addi t0, t0, 4
    blt  t0, t1, bss_loop
bss_done:

    /* Call main */
    call main

    /* Loop forever if main returns */
1:  j 1b
