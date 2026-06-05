VFLAGS = --assert --top-module npu_cluster_top +define+ASSERTS_OFF -Wall -Wno-fatal --trace --cc -Wno-lint --build --exe
PULP_INC = -y ../../snitch/src -I../../snitch/include \
           -y ../../common_cells/src -I../../common_cells/include \
           -y ../../fpnew/src -I../../fpnew/include \
           -y ../../reqrsp_interface/src -I../../reqrsp_interface/include \
           -y ../../register_interface/src -I../../register_interface/include \
           -y ../../riscv-dbg/src
VINC = -I../../npu_compute_core/src -I../../npu_cluster/src -I../../npu_control_core/src -I../../npu_memory_system/src

SRC = ../../riscv-dbg/src/dm_pkg.sv \
      ../../common_cells/src/cf_math_pkg.sv \
      ../../fpnew/src/fpnew_pkg.sv \
      ../../snitch/src/snitch_pkg.sv \
      ../../snitch/src/snitch_pma_pkg.sv \
      ../../snitch/src/riscv_instr.sv \
      ../../snitch/src/snitch_regfile_ff.sv \
      ../../snitch/src/snitch_lsu.sv \
      ../../snitch/src/snitch_l0_tlb.sv \
      ../src/npu_cluster_top.sv ../src/npu_core_wrapper.sv \
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

TEST_FILE ?= tests/test_matmul
CPP_SRC = npu_testbench.cpp $(TEST_FILE).cpp

all: npu_cluster

npu_cluster:
	verilator $(VFLAGS) $(PULP_INC) $(VINC) $(SRC) $(CPP_SRC) -Mdir obj_dir_cluster -o $(basename $(notdir $(TEST_FILE)))
