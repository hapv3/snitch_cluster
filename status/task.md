# Tasks

## Architectural Design Phase

### A1. Requirements Analysis & Constraints
- [x] Define performance target: 10 TOPS @ 1 GHz (INT8 inference)
- [x] Define target application domain: Vision-based CNNs (MobileNetV2, ResNet50, YOLOv8)
- [x] Define host integration: ARM-based SoC, AXI4/CHI bus interface
- [x] Define software compatibility: 100% TensorFlow Lite operator coverage
- [x] Define internal control ISA: RISC-V (Snitch core with Xssr/Xfrep/Xdma)

### A2. Snitch Cluster Reference Architecture Study
- [x] Analyze Snitch core microarchitecture (pseudo dual-issue, SSR, FREP)
- [x] Analyze Snitch cluster topology (TCDM banking, logarithmic interconnect, DMA)
- [x] Analyze Snitch ISA extensions: `Xssr` (stream semantic registers), `Xfrep` (floating-point repetition), `Xdma` (DMA control)
- [x] Document which Snitch architectural concepts are reusable vs. need adaptation for INT8 NPU
  - Reusable: TCDM banking, logarithmic interconnect, DMA engine, Xfrep loop control
  - Adaptation needed: FPU → INT8 MAC array, SSR streams → tensor tile streams

### A3. Top-Level Architecture Definition
- [x] Define cluster count: **4 clusters**
- [x] Define cores per cluster: **1 Control Core (RISC-V) + 10 Compute Cores**
- [x] Define MAC array size per Compute Core: **128 INT8 MACs**
- [x] Throughput calculation: 4 × 10 × 128 × 2 ops/MAC = 10,240 ops/cycle = **10.24 TOPS @ 1 GHz**
- [x] Define dataflow strategy: **Weight-Stationary** (optimized for CNN weight reuse)
- [x] Define system bus interface: AXI4 master (NPU → LPDDR), AXI4 slave (ARM host → NPU registers)

### A4. Memory Hierarchy Specification
- [x] Define TCDM size per cluster: **128–256 KB** (scaled from Snitch cluster conventions)
- [x] Define TCDM bank count: 32 banks × 8 KB/bank = 256 KB (or 16 banks for 128 KB)
- [x] Define TCDM port width: 64-bit per bank, single-cycle access
- [x] Define Instruction SPM (I-SPM) for Control Core: 16–32 KB per cluster
- [x] Define off-chip memory interface: AXI4, 128-bit data width, burst length up to 256 beats
- [x] Define double-buffering scheme: ping-pong TCDM regions for DMA ↔ compute overlap

### A5. Compute Core Datapath Specification
- [x] INT8 multiplier architecture: 8×8 → 16-bit product
- [x] Accumulator width: 32-bit (prevents overflow for up to 65,536 accumulations)
- [x] Accumulator output: configurable saturation + rounding to INT8/INT16/INT32
- [x] Activation engine: dedicated ReLU/ReLU6 logic + LUT-based Sigmoid/tanh (256-entry, 16-bit)
- [x] SSR stream interface: 2 read streams (weights, activations) + 1 write stream (output)
- [x] FREP loop controller: programmable nested loop (up to 3 levels) with auto-increment addressing

### A6. DMA Engine Specification
- [x] 2D/3D transfer descriptor format: src_addr, dst_addr, stride_src, stride_dst, block_size, repeat_count
- [x] Maximum outstanding transfers: 4 per cluster
- [x] Double-buffer state machine: valid/ready handshake between DMA and Compute Cores
- [x] Priority arbitration: round-robin between clusters at top-level AXI crossbar

### A7. Host Interface & Control Flow
- [x] ARM host programs NPU via MMIO registers mapped into AXI4 slave address space
- [x] Command mailbox: 64-byte descriptor per task (op type, tensor addresses, dimensions, quantization params)
- [x] Interrupt mechanism: NPU → ARM GIC, one IRQ line per cluster (task-complete, error)
- [x] Boot sequence: ARM host loads Control Core firmware into I-SPM, then asserts cluster reset release

### A8. TFLite Operator Mapping
- [x] **NPU-accelerated operators**: Conv2D, DepthwiseConv2D, FullyConnected, AveragePool2D, MaxPool2D, Add, Mul, Concatenation, Softmax, Reshape (static), Pad, Mean
- [x] **ARM-fallback operators**: If, While, NonMaxSuppression, Reshape (dynamic), SkipGram, custom/string ops
- [x] Operator fusion rules: Conv2D+BiasAdd+ReLU, Conv2D+BatchNorm+ReLU, DepthwiseConv+BiasAdd+ReLU6

### A9. Quantization Specification
- [x] Primary format: INT8 symmetric per-channel quantization (TFLite default)
- [x] Scale/zero-point handling: programmed into accumulator post-processing pipeline
- [x] Output requantization: fused multiply-shift-saturate in hardware after accumulation

### A10. Design Documents Finalized
- [x] High-Level Architecture Design Document (implementation_plan.md)
- [x] Skill set alignment: all 5 NPU skills mapped to architecture components

## Architecture Verification Phase
- [ ] Verify throughput math: 4 clusters × 10 cores × 128 MACs × 1 GHz ≥ 10 TOPS
- [ ] Roofline analysis: confirm TCDM bandwidth sustains MAC array without stalls
- [ ] SRAM sizing validation: tile sizes for target CNN layers (MobileNetV2, ResNet50) fit in 128–256 KB TCDM
- [ ] TFLite operator audit: map all TFLite builtin ops to NPU-supported vs ARM-fallback
- [ ] Area/power feasibility estimate (gate-count projection for 4-cluster topology)
- [ ] Cycle-accurate micro-benchmark on key layers (Conv2D 3×3, Depthwise Conv, FullyConnected)

## Implementation Phase 1: RTL — Compute Core & MAC Array
- [x] Define SystemVerilog interfaces and port lists for the 128-MAC Compute Core
- [x] Implement INT8 multiply-accumulate datapath with 32-bit accumulator
- [x] Implement saturation and rounding logic on accumulator output
- [x] Implement ReLU / activation engine (LUT + piecewise linear)
- [x] Implement `Xssr` stream interface to TCDM read ports
- [x] Implement `Xfrep` hardware loop controller
- [x] **✅ Gate: Unit UVM testbench for MAC array**
  - [x] Constrained-random stimulus covering all INT8 input combinations
  - [x] DPI-C reference model comparison (cycle-by-cycle)
  - [x] 100% code coverage on MAC datapath
- [ ] **✅ Gate: Lint & synthesis trial** (Synopsys DC / Yosys) — confirm no timing violations at 1 GHz

## Implementation Phase 2: RISC-V Snitch Control Core & Firmware
- [x] Design Mailbox (`npu_mailbox.sv`) for MMIO signaling
- [x] Design I-SPM and Boot ROM (`npu_ispm.sv`)
- [x] Integrate RISC-V control core wrap (`npu_control_core.sv`)
- [x] Develop Bare-metal Firmware (`main.c`, `link.ld`, `start.S`)
- [x] Develop C++ Testbench / mini-ISS (`npu_control_core_tb.cpp`)
- [x] **✅ Gate: RTL Verification & C++ mini-ISS Simulation**
  - [x] Run a minimal firmware on the Control Core in RTL simulation
  - [x] Verify DMA descriptor programming triggers correct TCDM writes
  - [x] Verify `Xfrep` loop correctly sequences Compute Core operations

## Implementation Phase 3: RTL — TCDM, Interconnect & DMA
- [x] Implement `npu_tcdm_bank.sv` (SRAM wrapper)
- [x] Implement `npu_tcdm_interconnect.sv` (Pipelined crossbar 5x32)
- [x] Implement `npu_dma_engine.sv` (2D strided AXI4 to TCDM DMA)
- [x] Create testbench `npu_memory_tb.sv` for collision and bandwidth verification
- [x] **Gate: Memory Subsystem Simulation**
  - [x] Verify concurrent TCDM accesses without data corruption
  - [x] Verify 2D DMA loading memory tiles correctly
  - [x] SVA assertions for collision-free banking under concurrent access
  - [x] Functional covergroups for bank conflict scenarios
  - [x] Bandwidth stress test: all 10 Compute Cores reading simultaneously
- [x] **✅ Gate: DMA engine verification**
  - [x] UVM testbench: 2D tile transfers with varying strides (Done via Verilator C++)
  - [x] Double-buffer handshake: verify zero-stall ping-pong operation
  - [x] AXI protocol compliance check (VIP or custom SVA)

## Implementation Phase 4: RTL — Cluster Integration & Scale-Out
- [x] Modify `npu_memory_subsystem.sv` to support 32 masters (30 SSRs, 1 DMA, 1 RISC-V)
- [x] Implement MMIO Address Decoder with Unicast and Broadcast/Multicast support
- [x] Implement `npu_cluster_top.sv` (Instantiating 1 Control Core, 10 Compute Cores, 1 Memory Subsystem)
- [x] Update bare-metal firmware (`main.c`) to configure and trigger multiple cores
- [x] Create system-level testbench `npu_cluster_tb.cpp`
- [x] **Gate: Cluster Integration Simulation**
  - [x] Integrate Control Core, Memory Subsystem, and 10 Compute Cores into `npu_cluster_top`
  - [x] Write `npu_cluster_tb.cpp` testbench with firmware loader and AXI memory responder
  - [x] Fix AXI interconnect simulation bugs
  - [x] Debug firmware stalls and fix TCDM arbitration / state machine bugs
  - [x] Verify Cluster-level Data flow (DMA -> TCDM -> Cores -> MAC)
  - [x] Verify Broadcast and Unicast core triggering
  - [x] Simulate 10 Compute Cores fetching weights concurrently from TCDM
  - [x] Verify Broadcast MMIO triggers all cores simultaneously
- [x] Implement interrupt controller (NPU → ARM GIC)
- [x] **✅ Gate: Single-cluster integration test**
  - [x] Develop `npu_cluster/tb/conv2d_test.cpp` Verilator testbench
  - [x] Initialize TCDM with random 4×4×32 activations and 3×3×32 weights
  - [x] Trigger `conv2d.bin` execution on the Snitch core
  - [x] Verify 4 cores parallel execution over the spatial output dimension
  - [x] Assert matching results against DPI-C TFLite reference model
  - [x] Profile: confirm MAC utilization ≥ 80% on compute-bound layer
- [x] **✅ Gate: Multi-cluster integration test**
  - [x] Distribute a tiled MobileNetV2 layer across 4 clusters
  - [x] Verify correct data partitioning and result aggregation
  - [x] Measure aggregate throughput vs 10 TOPS target

## Implementation Phase 4.5: Sliding Window Support
- [x] **✅ Gate: Sliding Window Implementation**
  - [x] Update `npu_core_wrapper.sv` to support sliding window (2D convolution) instead of 1D dot products.
  - [x] Implement `reg_stride_slide` write-back logic.
  - [x] Verify Conv2D 3x3 sliding window on the MAC array testbench.

## Implementation Phase 5: Architecture Verification (TensorLite Support)
- [x] **Performance Metrics Tracking**
  - [x] Add cycle counters to C++ Verilator testbenches
  - [x] Add DMA read/write transaction counters to testbenches
  - [x] Print MAC utilization percentage at end of simulation
- [x] **Native Hardware Operators**
  - [x] Verify `CONV_2D` on MAC array.
  - [x] Verify `FULLY_CONNECTED` on MAC array.
  - [x] Verify `RELU` / `RELU6` / `LEAKY_RELU` on Activation Engine.
- [x] **Firmware & Co-Processing Operators**
  - [x] Map and verify `DEPTHWISE_CONV_2D` via broadcast configuration.
  - [x] Map and verify `AVERAGE_POOL_2D` via CONV2D unit weights.
  - [x] Implement and verify `MAX_POOL_2D` on RISC-V firmware.
  - [x] Implement and verify `ADD` / `SUB` / `MUL` / `DIV` (Element-wise) on RISC-V firmware.
  - [x] Implement and verify `SOFTMAX` using LUTs on RISC-V firmware.
  - [x] Map and verify `CONCATENATION` / `RESHAPE` / `SQUEEZE` / `EXPAND_DIMS` via pointer arithmetic.
  - [x] Implement and verify `PAD` via DMA or firmware padding.
  - [x] **✅ Gate: Kernel unit tests** — each kernel tested against NumPy reference

## Implementation Phase 5.5: YOLO Model Operators Expansion
- [x] **Hardware Activation Engine Update**
  - [x] Update `act_type_e` in hardware to include `ACT_SILU`, `ACT_SIGMOID`, `ACT_MISH`.
  - [x] Implement a 256-entry hardware Lookup Table (LUT) in `npu_activation_engine.sv`.
- [x] **Firmware & Data Transformation Operators**
  - [x] Implement and verify `RESIZE_NEAREST_NEIGHBOR` using DMA striding in `tensorlite_ops.c`.
  - [x] Implement and verify `TRANSPOSE` using DMA striding / nested loops in firmware.
  - [x] Implement and verify `STRIDED_SLICE` / `SPLIT` via pointer arithmetic.
- [x] **Testbench Integration**
  - [x] Add unit tests for SiLU, Sigmoid, UpSampling, and Transpose in `tensorlite_test.cpp`.
  - [x] Verify testbench outputs against numpy references.

## Implementation Phase 6: Software Stack (Co-work HW ↔ SW Engineer)

### 6.1 NPU Bare-metal Driver API (Host Side)
- [ ] Tạo file `npu_mmio.h` định nghĩa lại bộ nhớ Mailbox, thanh ghi điều khiển NPU.
- [ ] Xây dựng thư viện quản lý bộ nhớ: `npu_alloc()` và `npu_free()` để cấp phát bộ nhớ liên tục trên External DDR cho mô hình.
- [ ] Thiết kế kiến trúc Command Queue (`struct npu_cmd`) để Host (ARM) đẩy các task Inference xuống cho NPU.
- [ ] Viết trình xử lý ngắt (Interrupt Handler) để Host nhận tín hiệu hoàn thành từ RISC-V qua Mailbox.

### 6.2 NPU Runtime & DMA Management (Firmware Side - RISC-V)
- [ ] Viết `npu_runtime.c` chạy trên lõi RISC-V Control Core liên tục poll Command Queue từ DDR.
- [ ] Cấu hình bộ nạp DMA (DMA descriptors) để hỗ trợ truyền tải dữ liệu 2D/3D (Strided DMA) phục vụ việc nạp/xuất Tile.
- [ ] Thiết lập cơ chế **Double Buffering** trong TCDM (64KB): Vừa chạy Compute Tile N, vừa nạp DMA Tile N+1.
- [ ] Tích hợp logic đồng bộ (Wait-for-DMA, Wait-for-Compute) giữa DMA Engine và 10 Compute Cores.

### 6.3 Deep Learning Compiler (TVM / MLIR Backend)
- [ ] Định nghĩa `target="snitch_npu"` bên trong framework trình biên dịch (TVM hoặc MLIR).
- [ ] Xây dựng các Pass tối ưu hóa Graph:
  - [ ] **Operator Fusion Pass**: Gộp `Conv2D + BatchNorm + ReLU/SiLU` thành một siêu toán tử (Macro-op) duy nhất.
  - [ ] **Tiling Pass**: Tự động chia cắt các Tensor lớn (ví dụ ảnh 224x224x3) thành các Tile nhỏ vừa với 64KB TCDM.
  - [ ] **Memory Planning Pass**: Lập lịch tái sử dụng bộ nhớ TCDM tĩnh để giảm thiểu overhead.
- [ ] Trình sinh mã (Code Generation): Biên dịch đồ thị mạng Neural thành file nhị phân tĩnh (`.bin`) chứa danh sách các lệnh NPU.
- [ ] **✅ Gate: Compiler regression** — Đưa file `.tflite` của MobileNetV2, ResNet50, YOLOv8 vào và biên dịch thành công ra file `.bin` không có lỗi.

### 6.4 TFLite Delegate (Edge AI Runtime)
- [ ] Tạo lớp `SnitchNPUDelegate` (kế thừa từ `TfLiteOpaqueDelegate` trong TFLite C++ API).
- [ ] Viết Node Visitor (Partitioning): Phân tách đồ thị. Các Node NPU hỗ trợ (Conv2D, YOLO activations, etc.) -> Đẩy vào NPU subgraph. Các Node không hỗ trợ -> Chạy fallback bằng CPU ARM.
- [ ] Viết hàm `Invoke()` của Delegate: Gửi subgraph xuống NPU Driver, chờ ngắt hoàn thành, và đồng bộ dữ liệu ra Output Tensor.
- [ ] **✅ Gate: TFLite operator audit** — Quét tự động toàn bộ chuẩn ops của TFLite, đảm bảo fallback an toàn.

### 6.5 End-to-End System Integration
- [ ] Tích hợp toàn bộ Stack: TFLite Interpreter -> Snitch Delegate -> NPU Driver -> Verilator HW Simulation.
- [ ] **✅ Gate: End-to-end SW smoke test** — Chạy thử nghiệm thành công inference 1 ảnh qua mô hình MobileNetV2 INT8 và YOLOv8 INT8, lấy kết quả bounding box chính xác trên môi trường mô phỏng RTL.

## Implementation Phase 7: System Verification & Sign-off
- [ ] Top-level UVM regression suite (all phases combined)
- [ ] Code coverage closure: statement ≥ 95%, branch ≥ 90%, toggle ≥ 85%
- [ ] Functional coverage closure: all covergroups hit 100%
- [ ] Performance benchmarking suite
  - [ ] MobileNetV2 INT8: measure latency (ms) and throughput (TOPS)
  - [ ] ResNet50 INT8: measure latency (ms) and throughput (TOPS)
  - [ ] YOLOv8-nano INT8: measure latency (ms) and throughput (TOPS)
  - [ ] **✅ Gate: Sustained throughput ≥ 10 TOPS on at least 2 of 3 benchmark models**
- [ ] Power analysis (gate-level simulation with switching activity)
- [ ] Final design review sign-off
