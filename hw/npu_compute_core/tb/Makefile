TOP = npu_mac_array
SRC_DIR = ../src
TB_DIR = verilator

VSRCS = $(SRC_DIR)/npu_compute_core_pkg.sv $(SRC_DIR)/$(TOP).sv

VERILATOR = verilator
VFLAGS = --cc --exe --build --trace -Wall -Wno-fatal -Wno-UNUSED -Wno-UNDRIVEN -I$(SRC_DIR)

all: sim

sim: $(VSRCS) $(TB_DIR)/npu_mac_array_tb.cpp
	$(VERILATOR) $(VFLAGS) $(VSRCS) $(TB_DIR)/npu_mac_array_tb.cpp --top-module $(TOP) -Mdir obj_dir -o V$(TOP)

clean:
	rm -rf obj_dir work_verilator *.vcd *.log

.PHONY: all sim clean

.PHONY: clean
clean:
	rm -rf obj_dir* *.log *.vcd test_*
