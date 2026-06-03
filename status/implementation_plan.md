# Phase 6.1 & 6.2: NPU Driver API & Firmware Runtime

This implementation plan details the architecture and tasks for developing the core software stack (Phase 6.1 and 6.2) that allows a Host CPU to offload deep learning workloads to the NPU Multi-Cluster subsystem.

## Goal Description

We need to establish a robust software communication protocol between the **Host ARM CPU** and the **NPU RISC-V Control Cores**. 
This involves defining the Memory-Mapped I/O (MMIO) layout, the Command Queue structure for sending inference tasks, and a firmware runtime that polls for these tasks and manages Double-Buffered DMA transfers.

## Proposed Changes

### 1. NPU Bare-metal Driver API (Host Side)
We will create a lightweight bare-metal C driver (`sw/npu_driver/`) for the Host CPU to interact with the NPU.

#### [NEW] `sw/npu_driver/npu_mmio.h`
- Define base addresses for the NPU subsystem from the Host's perspective.
- Define Mailbox registers (`MBOX_TRIGGER`, `MBOX_STATUS`).
- Define the `npu_cmd_t` structure (Command Queue element).

#### [NEW] `sw/npu_driver/npu_driver.c` & `.h`
- `npu_init()`: Initialize the NPU clusters and clear mailboxes.
- `npu_alloc()` / `npu_free()`: Simple external memory allocator for placing models and tensors.
- `npu_enqueue_task(npu_cmd_t* cmd)`: Push a command to the DDR Command Queue and ring the NPU doorbell (via Mailbox).
- `npu_wait_task()`: Poll or sleep until the NPU Mailbox indicates completion.

### 2. NPU Runtime Firmware (RISC-V Side)
We will create a new runtime loop running on the NPU's Control Core. It will replace the hardcoded `tensorlite_ops.c` with a dynamic task executor.

#### [NEW] `hw/npu_cluster/tb/fw/npu_runtime.c`
- Implement a `main()` loop that blocks on `wfi` (Wait For Interrupt).
- When the Mailbox doorbell rings, read the `npu_cmd_t` pointer from a known mailbox address.
- Parse the command:
  - If `CMD_RUN_LAYER`: Setup DMA descriptors to fetch the input tile and weight tile.
  - Implement a **Double-Buffering Loop**:
    1. DMA Fetch Tile N (Ping buffer)
    2. Wait DMA done
    3. Trigger Compute on Tile N
    4. Concurrently DMA Fetch Tile N+1 (Pong buffer)
    5. Wait Compute done
    6. DMA Writeback Tile N output.
- Write completion status back to the Host Mailbox and trigger the Host interrupt.

## Decisions

> [!NOTE]
> **Host CPU Emulation**: We will write the NPU Driver API in standard C and cross-compile it for RISC-V (bare-metal). We will spawn an extra RISC-V ISS in the Verilator testbench to act as the Host CPU. Since the driver only interacts with memory-mapped I/O (MMIO), porting it to an ARM target later will be completely trivial (only requires updating base addresses and using an ARM compiler).

> [!NOTE]
> **Command Structure**: We will use a **micro-coded command list**. The Command Queue will contain low-level instructions such as `OP_DMA_READ`, `OP_COMPUTE`, `OP_DMA_WRITE`, `OP_WAIT_DMA`, `OP_WAIT_COMPUTE`. The NPU firmware will act as a tiny interpreter, executing these micro-ops sequentially. This gives the Host (and later the Compiler) ultimate fine-grained control over double-buffering and memory allocation.

## Verification Plan

### C++ Testbench Integration
- Create `sw_smoke_test.cpp` that links against the Host C++ Driver API.
- The testbench will allocate mock tensors, push a `CMD_RUN_LAYER` command, and wait for the NPU.
- The NPU Firmware will execute the layer using double-buffered DMA and signal back.
- Verify that the simulated output matches the NumPy reference for that layer.
