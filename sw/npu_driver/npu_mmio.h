// npu_mmio.h - Hardware Memory Map and Micro-coded Command Definitions
// Defines the interface between Host CPU and NPU Multi-Cluster

#ifndef NPU_MMIO_H
#define NPU_MMIO_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned int uintptr_t;

// ==========================================
// 1. Memory Map (Host Perspective)
// ==========================================
#define NPU_BASE_ADDR        0x60000000 // Base of all NPU registers (if exposed)
#define NPU_DDR_MEMORY_BASE  0x80000000 // Start of DDR memory where tensors live
#define NPU_MBOX_BASE        0x40000000 // Mailbox base for cluster 0 (add offset for others)

// Mailbox Offsets
#define MBOX_STATUS_OFFSET   0x00
#define MBOX_TRIGGER_OFFSET  0x04

// ==========================================
// 2. Micro-coded Command Opcodes
// ==========================================
typedef enum {
    OP_NOP           = 0x00,
    
    // DMA Operations
    OP_DMA_READ      = 0x10, // Read from DDR to TCDM (Input/Weight)
    OP_DMA_WRITE     = 0x11, // Write from TCDM to DDR (Output)
    
    // Synchronization
    OP_WAIT_DMA      = 0x20, // Block until all pending DMA reads/writes are done
    OP_WAIT_COMPUTE  = 0x21, // Block until Compute Array is idle
    
    // Compute Operations (Hardware LUTs and MAC Array)
    OP_COMPUTE_CONV2D    = 0x30, // Trigger standard Convolution
    OP_COMPUTE_ACT_SILU  = 0x31, // Trigger SiLU Activation
    OP_COMPUTE_ACT_MISH  = 0x32, // Trigger Mish Activation
    OP_COMPUTE_ACT_SIGMOID = 0x33, // Trigger Sigmoid Activation
    
    // Firmware Fallback Operations (Executed by RISC-V Firmware)
    OP_FW_MAXPOOL    = 0x40,
    OP_FW_SOFTMAX    = 0x41,
    OP_FW_RESIZE_NN  = 0x42,
    OP_FW_SLICE      = 0x43,
    OP_FW_TRANSPOSE  = 0x44,

    // End of Queue
    OP_FINISH        = 0xFF
} npu_opcode_t;

// ==========================================
// 3. Command Queue Data Structures
// ==========================================

// A single micro-op instruction
typedef struct {
    uint8_t  opcode;     // From npu_opcode_t
    uint8_t  flags;      // e.g., Interrupt on completion
    uint16_t cluster_id; // Target cluster (0-3), or 0xFFFF for broadcast
    
    // 12 bytes of arguments (context dependent)
    union {
        // For OP_DMA_READ / OP_DMA_WRITE
        struct {
            uint32_t ext_addr;   // DDR Address
            uint32_t tcdm_addr;  // TCDM Offset
            uint32_t size;       // Size in bytes (or 2D striding config)
        } dma;
        
        // For OP_COMPUTE_*
        struct {
            uint32_t act_addr;   // TCDM Address of Activations
            uint32_t wgt_addr;   // TCDM Address of Weights
            uint32_t out_addr;   // TCDM Address for Output
        } compute;
        
        // For generic firmware ops
        struct {
            uint32_t arg0;
            uint32_t arg1;
            uint32_t arg2;
        } fw_op;
    } args;
} npu_cmd_t;

// A Queue header located at a known memory address
typedef struct {
    uint32_t head;         // Written by NPU Firmware
    uint32_t tail;         // Written by Host Driver
    uint32_t capacity;     // Max number of commands in queue
    uint32_t status_flags; // Bit 0 = NPU Error, Bit 1 = Queue Full, etc.
    npu_cmd_t cmds[0];     // Variable length array
} npu_cmd_queue_t;

#endif // NPU_MMIO_H
