---
name: npu-hardware-architecture
description: >
  Guidance on NPU hardware architecture design, processing element (PE) arrays,
  on-chip memory hierarchies, interconnect design (NoC), and dataflow strategy selection.
---

# NPU Hardware Architecture Skill

Use this skill when defining or analyzing high-level microarchitectural specifications for Neural Processing Units (NPUs), choosing system configurations, planning memory hierarchies, or optimizing tensor execution dataflows.

## Core Microarchitecture Concepts

### 1. Processing Element (PE) Array Topologies
* **Systolic Arrays**: 2D grid of PEs with local, direct connections to neighbors. Data flows synchronously through the grid, reducing high-fanout global buses. Highly optimal for dense matrix multiplications.
* **Vector/SIMD Processors**: Individual PEs that execute a single instruction across multiple data channels (wide ALU architectures). Good for versatile workloads like element-wise activations, pooling, and normalization.

### 2. Dataflow Optimization Strategies
Select the optimal dataflow pattern to maximize data reuse and minimize off-chip DRAM accesses:
* **Weight-Stationary (WS)**: Weights are loaded into the PEs and kept stationary. Inputs are streamed across the grid, and partial sums are accumulated across another axis or output vertically. Highly efficient when weights are small enough to fit completely in local PE buffers.
* **Output-Stationary (OS)**: Partial sums are kept stationary inside the PE accumulators. Inputs and weights are streamed through the array. Minimizes writeback traffic but requires high register count/accumulators in every PE.
* **Row-Stationary (RS)**: (e.g., Eyeriss style) Binds rows of weights, inputs, and partial sums to rows of PEs. Optimizes for energy efficiency by maximizing 2D spatial reuse of all three datatypes.
* **Input-Stationary (IS)**: Activations are loaded and kept stationary in the PE array, streaming weights and accumulating outputs. Excellent for large batch sizes or high activation reuse across models.

### 3. Memory Hierarchy Design
* **Double Buffering (Ping-Pong)**: Parallelize off-chip transfer with local compute. While the compute engine works on Buffer A, the DMA controller loads the next tile into Buffer B.
* **Scratchpad Memory (SPM)**: Addressable on-chip SRAM managed explicitly by NPU compiler or hardware controller (avoids the overhead/latency unpredictability of traditional hardware caches).
* **HBM vs LPDDR**: High-bandwidth off-chip interfaces crucial for model weight streaming. Use dual-channel or multi-channel memory controller designs to match PE processing bandwidth.

---

## Architectural Checklist

- [ ] **Data Reuse Analysis**: Calculate the arithmetic intensity (Ops/Byte) for target layers (Convolution, Linear) to determine if performance is compute-bound or memory-bound.
- [ ] **SRAM Sizing**: Ensure SRAM buffers are large enough to support the minimum required tile sizes for high-utilization GEMM (General Matrix Multiply).
- [ ] **NoC Bandwidth**: Verify that the Network-on-Chip (NoC) has sufficient injection rate and routing bandwidth to prevent PEs from stalling during weight reload cycles.
- [ ] **Bit-width Precision**: Balance dynamic range vs memory footprint. Plan support for INT8/FP8/FP16/bfloat16 formats based on application accuracy goals.
