/* Copyright 2026 NPU IP */
/* Startup code for NPU Control Core Firmware */

.section .text.init
.globl _start
_start:
    /* Initialize stack pointer to end of TCDM (256KB mapped at 0x10000000) */
    li sp, 0x10040000
    
    /* Call main */
    call main

    /* Loop forever if main returns */
1:  j 1b
