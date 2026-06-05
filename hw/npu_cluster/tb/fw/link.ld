/* Copyright 2026 NPU IP */
/* Linker script for NPU Control Core Firmware */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
  ispm (rwx) : ORIGIN = 0x1000,       LENGTH = 32K
  tcdm (rw)  : ORIGIN = 0x10030000,   LENGTH = 64K
}

SECTIONS
{
  .text : {
    *(.text.init)
    *(.text*)
    *(.rodata*)
  } > ispm

  /* .bss runs in TCDM, zero-initialized by start.S */
  .bss : {
    _bss_start = .;
    *(.bss*)
    *(.sbss*)
    _bss_end = .;
  } > tcdm

  _end = .;
}
