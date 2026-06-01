/* Copyright 2026 NPU IP */
/* Startup code for NPU Control Core Firmware */

.section .text.init
.globl _start
_start:
    /* Initialize stack pointer to end of I-SPM (32KB limit mapped at 0x1000) */
    li sp, 0x1000 + 32768
    
    /* Call main */
    call main

    /* Loop forever if main returns */
1:  j 1b
