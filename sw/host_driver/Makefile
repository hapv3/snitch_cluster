CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS = -march=rv32ima -mabi=ilp32 -O2 -nostdlib -fno-builtin

SRCS = host_main.c ../npu_driver/npu_driver.c

all: host_fw.bin

host_fw.elf: $(SRCS) ../../hw/npu_cluster/tb/fw/start.S ../../hw/npu_cluster/tb/fw/link.ld
	$(CC) $(CFLAGS) -T ../../hw/npu_cluster/tb/fw/link.ld ../../hw/npu_cluster/tb/fw/start.S $(SRCS) -o host_fw.elf

host_fw.bin: host_fw.elf
	$(OBJCOPY) -O binary host_fw.elf host_fw.bin

clean:
	rm -f *.elf *.bin
