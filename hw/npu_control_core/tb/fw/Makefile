CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS = -nostdlib -march=rv32ima -mabi=ilp32
LDSCRIPT = link.ld

SRCS = main.c mobilenet.c conv2d.c
BINS = $(SRCS:.c=.bin)

all: $(BINS)

%.elf: %.c start.S
	$(CC) -T $(LDSCRIPT) start.S $< -o $@ $(CFLAGS)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin *.o

.PHONY: all clean
