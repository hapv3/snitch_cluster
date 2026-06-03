# Makefile for NPU RISC-V Runtime Firmware

CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS = -march=rv32ima -mabi=ilp32 -O2 -nostdlib -fno-builtin

all: npu_runtime.bin

npu_runtime.elf: npu_runtime.c ../../hw/npu_cluster/tb/fw/start.S ../../hw/npu_cluster/tb/fw/link.ld
	$(CC) $(CFLAGS) -T ../../hw/npu_cluster/tb/fw/link.ld ../../hw/npu_cluster/tb/fw/start.S npu_runtime.c -o npu_runtime.elf

npu_runtime.bin: npu_runtime.elf
	$(OBJCOPY) -O binary npu_runtime.elf npu_runtime.bin

clean:
	rm -f *.elf *.bin
