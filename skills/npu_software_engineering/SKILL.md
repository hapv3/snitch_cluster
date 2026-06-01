---
name: npu-software-engineering
description: >
  Guidance on high-performance tensor kernel development, NPU runtime driver API interaction,
  profiling, custom operators, and CI/CD system integration for software engineers.
---

# NPU Software Engineering Skill

Use this skill when developing custom tensor kernels, writing application layer wrappers, profiling and optimizing model latency on NPU systems, or designing test pipelines.

## Core Software Engineering Workflows

### 1. High-Performance Kernel Development
* **Hardware Intrinsics**: Write low-level tensor kernels (GEMM, LayerNorm, Conv2d) utilizing target ISA vector instructions, DMA descriptors, and tile accumulator controls.
* **Loop Transformations**: Apply loop tiling, loop unrolling, and loop permutation to maximize cache hits and optimize register allocation inside compute kernels.

### 2. Custom Operator Development
* **Framework Registration**: Register hardware-accelerated kernels as custom operators in frameworks like PyTorch (`torch.library`) or ONNX Runtime to enable seamless model execution.
* **Fallback Mechanisms**: Provide a CPU fallback implementation for each custom operator to ensure model evaluation succeeds during debugging or when NPU hardware resource allocation fails.

### 3. Profiling & Performance Analysis
* **Roofline Analysis**: Plot kernel execution profiles against peak memory bandwidth and arithmetic capabilities to determine if code performance is compute-bound or memory-bound.
* **Timeline Tracing**: Capture host CPU and NPU hardware event traces (using tools like Perf, Nsight, or custom NPU trace collectors) to identify driver launch overheads or synchronization bottlenecks.

---

## Software Engineering Checklist

- [ ] **Numerical Precision Checking**: Compare custom kernel outputs against standard double-precision PyTorch or NumPy reference outputs. Require maximum absolute and relative tolerances ($10^{-5}$ for FP32/FP16).
- [ ] **Memory Safety**: Ensure that all pointer offsets calculated within high-performance kernels are strictly bounded to target scratchpad limits to prevent segmentation faults.
- [ ] **Continuous Integration**: Include unit tests for every newly developed kernel, covering dynamic input dimensions and edge cases inside CI/CD test grids.
- [ ] **Host-Device Synchronization**: Minimize host-device synchronization barriers (e.g., blocking `memcpy` operations) during high-throughput inference runs.
