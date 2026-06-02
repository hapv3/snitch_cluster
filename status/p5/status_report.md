# NPU Project Status Report vs. Initial Targets

This report evaluates our current progress against the initial architectural goals defined for the NPU IP project.

## Initial Target Overview
- **Goal**: Build an NPU IP for an ARM-based SoC, optimized for Vision-based CNN inference (TFLite).
- **Performance Target**: 10 TOPS @ 1 GHz.
- **Architecture**: Based on the **Snitch Cluster** principles (TCDM, explicit DMA, Control/Compute separation, SSR, FREP).
- **Configuration**: 4 Clusters. Each cluster contains 1 RISC-V Control Core and 10 Compute Cores (each doing 128 MACs/cycle).

---

## 🟢 1. Compute Core Datapath (Phase 1) - **[COMPLETED]**
**Target**: A compute unit capable of 128 INT8 MACs per cycle with hardware loops (FREP), Stream Semantic Registers (SSR), and Activation Engine.
**Status**: 
- We successfully developed the `npu_mac_array` (128 MACs, 32-bit accumulators).
- We implemented the `npu_activation_engine` (Requantization, ReLU, Sigmoid LUT).
- We built the `npu_ssr_interface` and `npu_frep_controller` for zero-overhead loop unrolling.
- **Verification**: Passed automated randomized C++ Verilator testing checking for extreme bounds and continuous accumulation.

## 🟢 2. RISC-V Control Core & Firmware (Phase 2) - **[COMPLETED]**
**Target**: An embedded RISC-V Snitch core to handle task dispatch, DMA programming, and mailbox MMIO with the ARM host.
**Status**: 
- We developed the RTL modules `npu_mailbox` (MMIO sync) and `npu_ispm` (Instruction memory).
- We wrote the bare-metal C firmware (`main.c`) utilizing GCC RISC-V cross-compilation.
- **Verification**: We built a custom C++ mini-ISS inside the Verilator testbench. The firmware successfully wakes up, parses ARM tasks, triggers simulated DMA/TCDM interfaces, orchestrates the NPU compute, and signals completion.

---

## 🟡 3. Memory Hierarchy & DMA (Phase 3) - **[PENDING]**
**Target**: Tightly Coupled Data Memory (TCDM) for low-latency shared SRAM, and an explicit DMA engine (`Xdma`) for double-buffered tensor loading from main memory.
**Current State**: 
- The Control Core firmware *simulates* DMA programming via memory-mapped addresses (`0x50000000`).
- **Next Step**: We need to implement the actual `Xdma` engine in RTL and the logarithmic interconnect for the multi-banked TCDM SRAM.

## 🟡 4. Cluster Integration & Scale-Out (Phase 4) - **[PENDING]**
**Target**: Achieve 10 TOPS @ 1 GHz (5,000 MACs/cycle).
**Current State**: 
- We have the individual components (MAC array, Control Core).
- **Next Step**: Instantiate **10 Compute Cores** and **1 Control Core** into a single Snitch Cluster (1,280 MACs/cycle). Then, instantiate 4 of these clusters at the top level to reach the 5,120 MACs/cycle (10.24 TOPS) goal.

## 🟡 5. Software Stack (Phase 5) - **[PENDING]**
**Target**: 100% TensorFlow Lite Support via a custom TFLite Delegate or TVM/MLIR backend compiler.
**Current State**: 
- Bare-metal hardware validation uses manual task descriptors.
- **Next Step**: Develop the software compiler passes to tile and schedule actual TFLite layers (Conv2D, DepthwiseConv) onto the hardware Mailbox descriptor format.

---

## Conclusion & Next Steps
We are perfectly on track with the hardware architecture. The foundational compute and control layers are solid. To continue marching towards the **10 TOPS** goal, our immediate next step is **Phase 3**: developing the **TCDM SRAM banking** and the **DMA Engine**, which will provide the necessary data bandwidth to keep our 128-MAC arrays fed!
