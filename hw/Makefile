# NPU Architecture Verification Master Makefile
# Runs all testbenches across all verification phases

.PHONY: all clean test_phase1 test_phase2 test_phase3 test_phase4 test_phase4_5

all: test_phase1 test_phase2 test_phase3 test_phase4 test_phase4_5
	@echo ""
	@echo "=================================================="
	@echo " 🎉 ALL ARCHITECTURE VERIFICATION TESTS PASSED! 🎉"
	@echo "=================================================="

test_phase1:
	@echo "=================================================="
	@echo "🚀 Running Phase 1: Compute Core & MAC Array Test "
	@echo "=================================================="
	$(MAKE) -C npu_compute_core/tb clean
	$(MAKE) -C npu_compute_core/tb
	./npu_compute_core/tb/obj_dir/Vnpu_mac_array

test_phase2:
	@echo "=================================================="
	@echo "🚀 Running Phase 2: Control Core Test             "
	@echo "=================================================="
	$(MAKE) -C npu_control_core/tb clean
	$(MAKE) -C npu_control_core/tb
	./npu_control_core/tb/obj_dir/Vnpu_control_core

test_phase3:
	@echo "=================================================="
	@echo "🚀 Running Phase 3: Memory Subsystem Test         "
	@echo "=================================================="
	$(MAKE) -C npu_memory_system/tb clean
	$(MAKE) -C npu_memory_system/tb
	./npu_memory_system/tb/obj_dir/Vnpu_memory_subsystem

test_phase4:
	@echo "=================================================="
	@echo "🚀 Running Phase 4: Single Cluster Test           "
	@echo "=================================================="
	$(MAKE) -C npu_cluster/tb clean
	$(MAKE) -C npu_cluster/tb
	./npu_cluster/tb/obj_dir/Vnpu_cluster_top

test_phase4_5:
	@echo "=================================================="
	@echo "🚀 Running Phase 4.5: Multi-Cluster Test          "
	@echo "=================================================="
	$(MAKE) -C npu_multi_cluster/tb clean
	$(MAKE) -C npu_multi_cluster/tb
	./npu_multi_cluster/tb/obj_dir/Vnpu_multi_cluster_top

clean:
	$(MAKE) -C npu_compute_core/tb clean
	$(MAKE) -C npu_control_core/tb clean
	$(MAKE) -C npu_memory_system/tb clean
	$(MAKE) -C npu_cluster/tb clean
	$(MAKE) -C npu_multi_cluster/tb clean
	@echo "All clean."
