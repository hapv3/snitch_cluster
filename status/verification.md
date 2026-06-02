# NPU System Verification & Testing Guide

This document outlines the test cases, firmware execution model, and instructions for running the Verilator simulations for the NPU Cluster and Multi-Cluster architectures.

## 1. Verification Strategy

The NPU is verified using **Verilator**, a cycle-accurate C++ simulation tool. Because simulating 4 RISC-V cores along with 40 Compute Cores at RTL level is extremely slow, we employ a hybrid approach:
- **NPU Logic**: Simulated fully in RTL (SystemVerilog).
- **RISC-V Control Cores**: Simulated via a fast C++ Instruction Set Simulator (ISS) built into the Verilator testbench. The ISS executes the RISC-V firmware and "injects" memory transactions directly into the RTL bus via Verilator DPI/Direct Signals.

## 2. Test Cases Overview

The testbenches are organized into progressive phases to verify different parts of the system.

### 2.1 Single Cluster: `conv2d_test` (Phase 4)
- **Location**: `hw/npu_cluster/tb/conv2d_test.cpp`
- **Purpose**: Verifies the raw throughput and functional correctness of the MAC Array and the basic sliding window convolution logic.
- **Data**: Hardcoded 4x4 input matrix and 3x3 kernel.
- **Execution**: Runs on a single NPU Compute Core inside 1 Cluster.

### 2.2 Single Cluster: `tensorlite_test` (Phase 5 & 5.5)
- **Location**: `hw/npu_cluster/tb/tensorlite_test.cpp`
- **Firmware**: `hw/npu_cluster/tb/fw/tensorlite_ops.c`
- **Purpose**: Verifies the integration of the RISC-V firmware with the NPU hardware across **20 different Neural Network Operations**.
- **Supported Operators**: 
  - *Hardware accelerated*: Conv2D, Fully Connected, ReLU, ReLU6, Leaky ReLU, SiLU, Sigmoid, Mish, Depthwise Conv2D, Avg Pool.
  - *Firmware executed*: Max Pool, Add, Sub, Mul, Softmax, Reshape, Pad, Resize Nearest Neighbor, Strided Slice, Transpose.

### 2.3 Multi-Cluster: `mobilenet_test` & `yolo_test` (Phase 5.5)
- **Location**: `hw/npu_multi_cluster/tb/yolo_test.cpp`
- **Purpose**: Verifies that the Multi-Cluster arbitration, interconnect, and Host AXI proxies function correctly when all 4 Clusters run workloads simultaneously.
- **Execution**: Spawns 4 RISC-V ISS instances. The host testbench triggers all 4 clusters via Mailbox writes (`0x40000004`). Each cluster executes the `tensorlite_ops` firmware, writes a success code (`0xA5A5`), and flags completion (`2`).

## 3. Running Instructions

All tests are driven via `Makefiles` in their respective `tb` directories.

### Prerequisites
- Operating System: Linux
- Dependencies: `verilator`, `make`, `riscv32-unknown-elf-gcc` (for firmware compilation).

### Running Single Cluster Tests
Navigate to the single cluster testbench directory:
```bash
cd hw/npu_cluster/tb
```

**Run Conv2D Test:**
```bash
make conv2d
```
*Expected Output:* `[SUCCESS] Conv2D Test Passed!`

**Run TensorLite 20-Op Test:**
```bash
make tensorlite
```
*Expected Output:*
```text
[INFO] Firmware execution completed after 72419 instructions.
[SUCCESS] Phase 5 TensorLite Verification COMPLETE
```

### Running Multi-Cluster Tests
Navigate to the multi-cluster testbench directory:
```bash
cd hw/npu_multi_cluster/tb
```

**Run YOLO / Multi-Cluster Test:**
```bash
make yolo
```
*Expected Output:*
```text
[INFO] Started RISC-V Firmware Execution for 4 Clusters (YOLO)
[SUCCESS] YOLO Multi-Cluster Task Complete in 1891 cycles!
```

## 4. Debugging

- **Waveform Tracing**: The `Makefile` targets include the `--trace` flag. Running the tests will generate `.vcd` files in the directory which can be viewed with GTKWave.
- **RTL Trace**: The testbenches include C++ `cout` statements that log every memory read/write the RISC-V ISS injects into the RTL (e.g., `[RTL Trace] Core 0 Read Addr...`). This is invaluable for debugging firmware lockups.
