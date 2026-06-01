/* Copyright 2026 NPU IP */
/* Linker script for NPU Control Core Firmware */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
  ispm (rwx) : ORIGIN = 0x1000, LENGTH = 32K
}

SECTIONS
{
  .text : {
    *(.text.init)
    *(.text*)
  } > ispm

  .data : {
    *(.data*)
    *(.rodata*)
    *(.sdata*)
  } > ispm

  .bss : {
    *(.bss*)
    *(.sbss*)
  } > ispm

  _end = .;
}
