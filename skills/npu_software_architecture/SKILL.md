---
name: npu-software-architecture
description: >
  Guidance on compiler infrastructure (TVM, MLIR), runtime environments, graph-level
  optimizations, and quantization mapping on neural processing unit (NPU) targets.
---

# NPU Software Architecture Skill

Use this skill when designing compiler pipelines, defining NPU runtime/driver APIs, integrating models from frameworks like PyTorch or TensorFlow, or defining quantization schemes for spatial accelerator systems.

## Key Compiler & Optimization Areas

### 1. Multi-Level IR compilation (MLIR & TVM)
* **High-Level Dialects/Graphs**: Lower from PyTorch (TorchDynamo) or ONNX down into high-level dialects (e.g., TOSA, Linalg, or TVM Relax) for graph optimizations.
* **Low-Level Dialects**: Lower to hardware-specific dialects representing physical memory buffers, loops, DMA instructions, and PE execution commands.
* **Code Generation**: Transform multi-dimensional loops to micro-kernels targetting specific hardware vector units or tensor engines.

### 2. Graph & Memory Optimizations
* **Operator Fusion**: Group consecutive operations (e.g., Conv2D + BiasAdd + ReLU) to keep activations in local high-speed SRAM registers rather than writing back to external DRAM.
* **Static Memory Planning**: Pre-allocate physical addresses in on-chip SRAM for inputs, outputs, and intermediate scratch buffers before runtime to completely avoid dynamic allocation overheads and heap fragmentation.
* **Tiling & Scheduling**: Split large tensors into smaller blocks (tiles) that fit inside the local scratchpads. Schedule DMA transactions concurrently with PE processing loops.

### 3. Quantization Frameworks
* **PTQ (Post-Training Quantization)**: Quantize FP32/FP16 models to INT8 or FP8 using calibration datasets. Calculate scaling factors and offsets per-tensor or per-channel.
* **QAT (Quantization-Aware Training)**: Simulate quantization during model training to adapt model weights to low-precision limitations, recovering model accuracy.
* **Mixed Precision Mapping**: Schedule execution profiles that allow specific sensitive layers (e.g., final classification layers) to run in FP16 while standard convolutions run in INT8.

---

## Architectural Checklist

- [ ] **Memory Footprint Validation**: Map the tensor lifecycles across compilation phases and confirm that peak memory usage does not exceed local scratchpad limits.
- [ ] **DMA-Compute Overlap**: Ensure compilation schedules leverage double buffering by issuing non-blocking DMA reads/writes ahead of execution time.
- [ ] **Quantization Accuracy**: Perform cosine similarity and perplexity checks on output logits to verify that quantization did not degrade model performance below threshold levels.
- [ ] **Driver Command Queuing**: Define runtime command queues to decouple host CPU enqueue logic from device-side NPU hardware schedulers.
