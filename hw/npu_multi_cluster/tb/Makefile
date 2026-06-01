all:
	verilator --assert --top-module npu_multi_cluster_top -Wall -Wno-fatal --trace --cc -Wno-lint --build --exe -I../../npu_compute_core/src -I../../npu_cluster/src -I../../npu_control_core/src -I../../npu_memory_system/src ../src/npu_multi_cluster_top.sv ../../npu_cluster/src/npu_cluster_top.sv ../../npu_cluster/src/npu_core_wrapper.sv ../../npu_control_core/src/npu_control_core.sv ../../npu_control_core/src/npu_ispm.sv ../../npu_control_core/src/npu_mailbox.sv ../../npu_memory_system/src/npu_memory_subsystem.sv ../../npu_memory_system/src/npu_dma_engine.sv ../../npu_memory_system/src/npu_tcdm_interconnect.sv ../../npu_memory_system/src/npu_tcdm_bank.sv ../../npu_compute_core/src/npu_compute_core.sv ../../npu_compute_core/src/npu_mac_array.sv ../../npu_compute_core/src/npu_activation_engine.sv mobilenet_test.cpp

clean:
	rm -rf obj_dir
