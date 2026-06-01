# Verilator Makefile for NPU Memory Subsystem

VERILATOR = verilator
VFLAGS = --assert --top-module npu_memory_subsystem -Wall -Wno-fatal --trace --cc -Wno-lint --build --exe

RTL = ../src/npu_tcdm_bank.sv \
      ../src/npu_tcdm_interconnect.sv \
      ../src/npu_dma_engine.sv \
      ../src/npu_memory_subsystem.sv

TB = npu_memory_tb.cpp

all: run

build:
	$(VERILATOR) $(VFLAGS) $(RTL) $(TB)

run: build
	./obj_dir/Vnpu_memory_subsystem

clean:
	rm -rf obj_dir
