VFLAGS = --assert --top-module npu_cluster_top -Wall -Wno-fatal --trace --cc -Wno-lint --build --exe
SRC = ../src/npu_cluster_top.sv ../src/npu_core_wrapper.sv ../../npu_control_core/src/npu_control_core.sv ../../npu_control_core/src/npu_ispm.sv ../../npu_control_core/src/npu_mailbox.sv ../../npu_memory_system/src/npu_memory_subsystem.sv ../../npu_memory_system/src/npu_dma_engine.sv ../../npu_memory_system/src/npu_tcdm_interconnect.sv ../../npu_memory_system/src/npu_tcdm_bank.sv ../../npu_compute_core/src/npu_compute_core.sv ../../npu_compute_core/src/npu_mac_array.sv ../../npu_compute_core/src/npu_activation_engine.sv

all:
	verilator $(VFLAGS) -I../../npu_compute_core/src $(SRC) npu_cluster_tb.cpp
	./obj_dir/Vnpu_cluster_top

clean:
	rm -rf obj_dir
