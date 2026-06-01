---
name: npu-hardware-verification
description: >
  Guidance on UVM testbench architectures, co-simulation with reference models,
  SystemVerilog Assertions (SVA), functional coverage, and NPU hardware verification strategies.
---

# NPU Hardware Verification Skill

Use this skill when developing verification plans, building or extending UVM (Universal Verification Methodology) testbenches, writing functional coverage classes, or creating SystemVerilog Assertions (SVA) for NPU modules.

## Key Verification Methodology

### 1. UVM Testbench Architecture
* **Drivers & Monitors**: Write transaction-level drivers to feed inputs into hardware interfaces, and monitors to capture output data streams without affecting hardware execution.
* **Scoreboards**: Compare RTL outputs cycle-by-cycle or end-to-end against a reference model. Ensure out-of-order execution pathways are properly re-ordered or uniquely tagged for scoring.
* **Sequencers**: Design constrained-random generators capable of stressing corner cases, such as buffer overflows, collision conditions, and concurrent DMA-compute cycles.

### 2. Reference Model Integration (Co-Simulation)
* **DPI-C Interfaces**: Connect C/C++ or Python golden model layers (e.g., a lightweight NumPy execution model) to your SystemVerilog testbench via Direct Programming Interface (DPI-C).
* **Step-and-Compare**: Compare internal state elements (like PE register files and accumulator matrices) directly against the reference model at key execution boundaries.

### 3. Coverage-Driven Verification (CDV)
* **Functional Covergroups**: Define coverpoints for key architecture parameters (e.g., input shapes, channel configurations, kernel dimensions, and padding types).
* **SystemVerilog Assertions (SVA)**: Implement concurrent assertions to verify bus handshakes (e.g., AXI/AHB protocols), FIFO status flags (underflow/overflow), and proper credit-based flow control state transitions.

---

## Hardware Verification Checklist

- [ ] **Protocol Compliance**: Verify that bus interfaces strictly adhere to system specs using industry-standard VIP (Verification IP) or custom assertion suites.
- [ ] **Out-of-Bounds Checks**: Test model compile behaviors on extremely small/large inputs, zero values, and NaN/Inf floats to confirm robust edge-case handling.
- [ ] **Structural & Functional Coverage**: Target 100% statement, branch, and toggle coverage alongside 100% functional covergroup achievement before signing off.
- [ ] **Reset Resiliency**: Verify that assertions do not trigger falsely during power-on reset or clock frequency changes.
