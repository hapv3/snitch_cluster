VFLAGS = --assert --top-module npu_cluster_top -Wall -Wno-fatal --trace --cc -Wno-lint --build --exe
SRC = ../src/npu_cluster_top.sv ../src/npu_core_wrapper.sv \
      ../../npu_control_core/src/npu_control_core.sv \
      ../../npu_control_core/src/npu_ispm.sv \
      ../../npu_control_core/src/npu_mailbox.sv \
      ../../npu_memory_system/src/npu_memory_subsystem.sv \
      ../../npu_memory_system/src/npu_dma_engine.sv \
      ../../npu_memory_system/src/npu_tcdm_interconnect.sv \
      ../../npu_memory_system/src/npu_tcdm_bank.sv \
      ../../npu_compute_core/src/npu_compute_core.sv \
      ../../npu_compute_core/src/npu_mac_array.sv \
      ../../npu_compute_core/src/npu_activation_engine.sv

VINC = -I../../npu_compute_core/src

# Build firmware first
fw:
	$(MAKE) -C fw

# Phase 4 test
conv2d: fw
	verilator $(VFLAGS) $(VINC) $(SRC) conv2d_test.cpp -Mdir obj_dir -o Vnpu_cluster_top
	./obj_dir/Vnpu_cluster_top

# Phase 5 test (TensorLite operators)
tensorlite: fw
	verilator $(VFLAGS) $(VINC) $(SRC) tensorlite_test.cpp -Mdir obj_dir_tensorlite -o Vtensorlite
	./obj_dir_tensorlite/Vtensorlite

# Default: build everything
all: conv2d tensorlite

clean:
	rm -rf obj_dir obj_dir_tensorlite *.vcd
	$(MAKE) -C fw clean

.PHONY: all fw conv2d tensorlite clean
