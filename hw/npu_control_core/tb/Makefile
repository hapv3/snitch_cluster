TOP = npu_control_core
SRC_DIR = ../src
TB_DIR = .

VSRCS = $(SRC_DIR)/npu_ispm.sv $(SRC_DIR)/npu_mailbox.sv $(SRC_DIR)/$(TOP).sv

VERILATOR = verilator
VFLAGS = --cc --exe --build --trace -Wall -Wno-fatal -I$(SRC_DIR)

all: fw sim

fw:
	$(MAKE) -C fw

sim: fw $(VSRCS) $(TB_DIR)/npu_control_core_tb.cpp
	$(VERILATOR) $(VFLAGS) $(VSRCS) $(TB_DIR)/npu_control_core_tb.cpp --top-module $(TOP) -Mdir obj_dir -o V$(TOP)

clean:
	rm -rf obj_dir *.vcd
	$(MAKE) -C fw clean

.PHONY: all sim fw clean
