---
name: npu-hardware-design
description: >
  Guidelines for RTL design of NPUs including systolic arrays, MAC units, pipeline design,
  on-chip buffers, address generation units (AGUs), and low-power techniques.
---

# NPU Hardware Design Skill

Use this skill when developing, refactoring, or reviewing SystemVerilog/Verilog RTL designs for NPU building blocks, high-speed datapaths, compute engines, memory controllers, and peripheral modules.

## Key RTL Design Principles

### 1. Compute Datapath Design
* **Multiply-Accumulate (MAC) Units**: Structure MAC trees with proper register pipe stages to optimize operating frequency ($F_{max}$). Avoid long combinatorial multiplier paths.
* **Precision Handling**: Design rounding and saturation logic carefully. When multiplying INT8 or FP8 inputs, ensure intermediate accumulator bits are wide enough (typically 32-bit registers) to avoid overflow.
* **Activation Engines**: Implement low-latency approximations for complex transcendental activation functions (e.g., GELU, Swish, Sigmoid) using lookup tables (LUTs) combined with piecewise linear interpolation.

### 2. Control & Memory Access
* **Address Generation Units (AGUs)**: Build configurable stride controllers to handle multi-dimensional tensor indexing (e.g., convolution sliding windows, matrix transpose strides) in hardware.
* **DMA Engines**: Design high-performance direct memory access blocks with asynchronous FIFOs to bridge different clock domains between core logic and the Network-on-Chip (NoC)/DRAM controllers.
* **Double-Buffer Control**: Implement robust handshake state machines (valid/ready protocols) to swap active SRAM ping-pong blocks without losing compute clock cycles.

### 3. Low-Power Design Techniques
* **Fine-grained Clock Gating**: Proactively gate clock trees of unused PE regions or idle activation blocks during specific layer executions.
* **Operand Isolation**: Freeze input signals feeding large multiplier arrays when the valid handshake is low to prevent dynamic toggling power losses.

---

## Hardware Design Checklist

- [ ] **Timing Violations & Pipeling**: Verify that combinatorial logic between register boundaries does not cause negative slack. Insert pipe stages inside arithmetic adder trees if necessary.
- [ ] **Reset & Clock Domains**: Ensure all registers have a defined, synchronous/asynchronous reset state. Ensure CDC (Clock Domain Crossing) blocks use synchronized handshakes or dual-clock FIFOs.
- [ ] **Data Saturation**: Check that arithmetic overflow logic cleanly saturates to the max/min numerical bounds instead of wrapping around.
- [ ] **Synthesizability**: Avoid non-synthesizable SystemVerilog constructs (e.g., `initial` blocks for runtime logic, direct delays `#`, non-constant loop bounds) inside production RTL.
