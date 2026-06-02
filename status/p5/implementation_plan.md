# NPU Phase 5: Architecture Verification (TensorLite Support)

With the hardware datapath (Phase 1), control core (Phase 2), memory subsystem (Phase 3), and multi-cluster integration (Phase 4 & 4.5) successfully verified, Phase 5 focuses on **Architecture Verification**.

The goal is to ensure the NPU hardware and firmware can correctly execute all critical TFLite operators required by standard vision models (e.g., MobileNetV2, ResNet50, YOLOv8-nano).

---

## Proposed Changes

### 5.1 Performance Metrics Infrastructure

Add profiling counters to measure operator efficiency across the hardware stack.

#### [MODIFY] [npu_core_wrapper.sv](file:///home/dev01/snitch_cluster/hw/npu_cluster/src/npu_core_wrapper.sv)
- Add a 32-bit `mac_active_cycles` counter that increments every cycle `valid_i` is high.
- Add a 32-bit `total_cycles` counter that increments every cycle after reset deasserts.
- Expose both counters as read-only MMIO registers at `0x18` and `0x1C`.

#### [MODIFY] [npu_dma_engine.sv](file:///home/dev01/snitch_cluster/hw/npu_memory_system/src/npu_dma_engine.sv)
- Add `dma_read_count` and `dma_write_count` 32-bit counters.
- Increment on each accepted DMA read/write request.
- Expose as read-only MMIO registers.

#### [NEW] `hw/npu_cluster/tb/fw/tensorlite_ops.c`
- Read `mcycle` CSR before and after each operator to compute firmware-side cycle count.
- At the end, write the cycle delta and operator status (pass/fail) to Mailbox for the testbench to read.

---

### 5.2 Native Hardware Operators

These operators run directly on the MAC array and Activation Engine hardware.

#### Test: `CONV_2D`
- **Method**: Configure DMA to load a 4×4×4 input feature map and 3×3 kernel weights into TCDM. Set `reg_stride_slide` for 2D sliding window. Trigger MAC array. Read output from TCDM.
- **Verification**: Compare output against a C reference `conv2d_ref()` function computed in firmware. Return 0 on match, 1 on mismatch.
- **File**: [NEW] `hw/npu_cluster/tb/fw/tensorlite_ops.c`

#### Test: `FULLY_CONNECTED`
- **Method**: Set `reg_stride_slide = 0` (no sliding window). Load weight matrix and input vector into TCDM. Trigger MAC array for matrix-vector multiply.
- **Verification**: Compare output against C reference `fc_ref()`.
- **File**: [MODIFY] `hw/npu_cluster/tb/fw/tensorlite_ops.c`

#### Test: `RELU` / `RELU6` / `LEAKY_RELU`
- **Method**: Configure `act_type` field in the Control Register (MMIO `0x00`) to `ACT_RELU` / `ACT_RELU6`. Feed a vector containing both positive and negative values through the Activation Engine.
- **Verification**: Verify positive values are unchanged, negative values are clamped to 0 (RELU) or range [0, 6] (RELU6).
- **File**: [MODIFY] `hw/npu_cluster/tb/fw/tensorlite_ops.c`

> [!NOTE]
> `LEAKY_RELU` is not currently supported in `act_type_e` enum (only `ACT_NONE`, `ACT_RELU`, `ACT_RELU6`, `ACT_SIGMOID`). We need to either add `ACT_LEAKY_RELU` to `npu_compute_core_pkg.sv` or handle it in firmware with a shift-and-add approximation.

---

### 5.3 Firmware & Co-Processing Operators

These operators are mapped to the hardware via firmware control logic or executed purely on the RISC-V Control Core.

#### Test: `DEPTHWISE_CONV_2D`
- **Method**: Firmware partitions weights so each Compute Core processes one output channel independently. No cross-channel accumulation. Each core receives its own channel slice via unicast MMIO writes.
- **Verification**: Compare per-channel output against C reference `depthwise_conv2d_ref()`.

#### Test: `AVERAGE_POOL_2D`
- **Method**: Map to `CONV_2D` hardware block. Firmware sets all kernel weights to `1/KernelSize` (quantized as fixed-point INT8). Configure sliding window stride equal to kernel size.
- **Verification**: Compare output against C reference `avg_pool_ref()`.

#### Test: `MAX_POOL_2D`
- **Method**: Purely firmware. RISC-V Control Core reads input tiles from TCDM, applies `max()` across the kernel window, writes result to TCDM output buffer.
- **Verification**: Compare output against C reference `max_pool_ref()`.

#### Test: `ADD` / `SUB` / `MUL` / `DIV` (Element-wise)
- **Method**: Firmware loops over TCDM vectors using standard RISC-V `lw`/`sw` + arithmetic instructions.
- **Verification**: Compare element-by-element against C reference functions.

#### Test: `SOFTMAX`
- **Method**: Firmware uses a fixed-point exponent lookup table (256-entry INT8→INT16 LUT embedded in firmware header). Computes `exp(x)` via table lookup, then normalizes by the sum.
- **Verification**: Compare against C reference `softmax_ref()` with tolerance ≤ 1 LSB.

#### Test: `CONCATENATION` / `RESHAPE` / `SQUEEZE` / `EXPAND_DIMS`
- **Method**: Zero-cost operations. Firmware adjusts DMA source addresses and stride configurations. No actual data movement; only pointer arithmetic.
- **Verification**: Verify that the output tensor's logical layout matches the expected shape by reading specific elements.

#### Test: `PAD`
- **Method**: Firmware fills the TCDM output buffer with zeros first, then DMA copies the input tensor into the correct offset within the padded buffer.
- **Verification**: Verify padded regions are zero and inner region matches input data.

---

## User Review Required

> [!IMPORTANT]
> **Element-wise operators & MAX_POOL implementation**: Should we implement these purely in C (compiled by `riscv64-unknown-elf-gcc`), or use inline assembly for performance-critical inner loops?

> [!IMPORTANT]
> **LEAKY_RELU support**: Should we extend the hardware `act_type_e` enum in `npu_compute_core_pkg.sv` to add `ACT_LEAKY_RELU`, or handle it in firmware?

> [!IMPORTANT]
> **Softmax LUT**: We will embed a fixed-point exponent LUT header (`softmax_lut.h`) in the firmware. The LUT will be 256 entries mapping INT8 input to INT16 `exp(x)` output. Is this acceptable, or do you prefer a Taylor expansion approach?

---

## Verification Plan

### New Files
| File | Type | Description |
|------|------|-------------|
| `hw/npu_cluster/tb/fw/tensorlite_ops.c` | [NEW] | Firmware containing unit tests for all TFLite operators |
| `hw/npu_cluster/tb/fw/softmax_lut.h` | [NEW] | Fixed-point exponent LUT for Softmax |
| `hw/npu_cluster/tb/tensorlite_test.cpp` | [NEW] | C++ Verilator testbench for Phase 5 operator verification |

### Modified Files
| File | Description |
|------|-------------|
| `hw/npu_cluster/src/npu_core_wrapper.sv` | Add `mac_active_cycles` and `total_cycles` performance counters |
| `hw/npu_memory_system/src/npu_dma_engine.sv` | Add `dma_read_count` and `dma_write_count` counters |
| `hw/npu_compute_core/src/npu_compute_core_pkg.sv` | (Optional) Add `ACT_LEAKY_RELU` to `act_type_e` |
| `hw/Makefile` | Add `test_phase5` target for TensorLite verification |

### Automated Tests
- Run `make test_phase5` in `hw/` to build firmware, compile the Verilator testbench, and execute all operator tests.
- Run `make` in `hw/` to run the full regression (Phase 1 through Phase 5).
- Each operator test prints `[PASS] CONV_2D` or `[FAIL] CONV_2D` to stdout.
- At the end of execution, the testbench prints:
  ```
  === Performance Metrics ===
  MAC Utilization:      XX.X%
  Total Cycles:         XXXX
  DMA Read Transactions:  XX
  DMA Write Transactions: XX
  ```

### Manual Verification
- Review waveform (VCD) for each operator to confirm correct DMA timing and MAC pipeline behavior.
