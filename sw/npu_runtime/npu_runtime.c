// npu_runtime.c - NPU RISC-V Firmware Runtime
// Polls the Command Queue, parses micro-coded operations, and drives the HW cores & DMA.

#include "../npu_driver/npu_mmio.h"

// ========== Memory Map & Registers (from NPU Core perspective) ==========
#define MBOX_BASE    0x40000000
#define DMA_BASE     0x70000000
#define NPU_BASE     0x60000000
#define TCDM_BASE    0x10000000

#define MBOX_STATUS  (*(volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_TRIGGER (*(volatile unsigned int*)(MBOX_BASE + 0x04))

// DMA
#define DMA_SRC      (*(volatile unsigned int*)(DMA_BASE + 0x00))
#define DMA_DST      (*(volatile unsigned int*)(DMA_BASE + 0x04))
#define DMA_DIM_X    (*(volatile unsigned int*)(DMA_BASE + 0x08))
#define DMA_DIM_Y    (*(volatile unsigned int*)(DMA_BASE + 0x0C))
#define DMA_STRIDE_S (*(volatile unsigned int*)(DMA_BASE + 0x10))
#define DMA_STRIDE_D (*(volatile unsigned int*)(DMA_BASE + 0x14))
#define DMA_TRIGGER  (*(volatile unsigned int*)(DMA_BASE + 0x18))
#define DMA_STATUS   (*(volatile unsigned int*)(DMA_BASE + 0x1C))

// NPU Cores
#define CORE_CTRL(c)    (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x00))
#define CORE_ACT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x04))
#define CORE_WGT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x08))
#define CORE_OUT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x0C))
#define CORE_STATUS(c)  (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x10))
#define CORE_SLIDE(c)   (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x14))

// Utility Functions
static void wait_core_idle(int core) {
    while (CORE_STATUS(core) & 1) {}
}

static void wait_dma_idle(void) {
    while (DMA_STATUS & 1) {}
}

// Global state
static volatile npu_cmd_queue_t* cmd_queue = 0; // Host must pass this pointer via arg or known addr

void execute_op(const npu_cmd_t* cmd) {
    switch (cmd->opcode) {
        case OP_NOP:
            break;
            
        case OP_DMA_READ:
        case OP_DMA_WRITE:
            wait_dma_idle();
            if (cmd->opcode == OP_DMA_READ) {
                DMA_SRC = cmd->args.dma.ext_addr;
                DMA_DST = cmd->args.dma.tcdm_addr;
            } else {
                DMA_SRC = cmd->args.dma.tcdm_addr;
                DMA_DST = cmd->args.dma.ext_addr;
            }
            // For now, treat size as simple 1D transfer
            DMA_DIM_X = cmd->args.dma.size;
            DMA_DIM_Y = 1;
            DMA_STRIDE_S = 0;
            DMA_STRIDE_D = 0;
            DMA_TRIGGER = 1; // Start DMA
            break;
            
        case OP_WAIT_DMA:
            wait_dma_idle();
            break;
            
        case OP_WAIT_COMPUTE:
            for(int i=0; i<10; i++) wait_core_idle(i);
            break;
            
        case OP_COMPUTE_CONV2D:
        case OP_COMPUTE_ACT_SILU:
        case OP_COMPUTE_ACT_MISH:
        case OP_COMPUTE_ACT_SIGMOID: {
            // Distribute work to cores (simplified: just core 0 for now)
            int act_code = 0;
            if (cmd->opcode == OP_COMPUTE_ACT_SILU) act_code = (5 << 8);
            else if (cmd->opcode == OP_COMPUTE_ACT_MISH) act_code = (6 << 8);
            else if (cmd->opcode == OP_COMPUTE_ACT_SIGMOID) act_code = (3 << 8);
            
            CORE_ACT(0) = cmd->args.compute.act_addr;
            CORE_WGT(0) = cmd->args.compute.wgt_addr;
            CORE_OUT(0) = cmd->args.compute.out_addr;
            CORE_SLIDE(0) = 0;
            CORE_CTRL(0) = act_code | 0x05; // trigger + write_out
            break;
        }
            
        case OP_FW_MAXPOOL:
            // TODO: Call maxpool firmware loop
            break;
            
        default:
            break;
    }
}

int main(void) {
    // 1. Setup: Host will pass the queue pointer in register x10 (a0) or x11 (a1).
    // For now, let's assume the Host places the queue at a fixed DDR address 
    // or passes it during boot. We'll use a fixed address for simplicity.
    uint32_t queue_addr = 0x80000000; // Start of DDR
    
    // In our Verilator tb, we pass Cluster ID in x10
    int cluster_id;
    __asm__ volatile ("mv %0, x10" : "=r"(cluster_id));
    
    // Each cluster gets its own queue separated by 1MB
    queue_addr += cluster_id * 0x100000; 
    cmd_queue = (volatile npu_cmd_queue_t*)queue_addr;
    
    // Initialize head
    cmd_queue->head = 0;

    while (1) {
        // 2. Wait for Host Doorbell
        __asm__ volatile ("wfi");
        
        // Host rang the doorbell! 
        // 3. Process commands
        while (cmd_queue->head != cmd_queue->tail) {
            uint32_t head = cmd_queue->head;
            const npu_cmd_t* cmd = (const npu_cmd_t*)&cmd_queue->cmds[head];
            
            // Execute the micro-op
            execute_op(cmd);
            
            // Handle FINISH specifically
            if (cmd->opcode == OP_FINISH) {
                // Signal success to host
                MBOX_TRIGGER = 0xA5A5;
                MBOX_TRIGGER = 2; // Marks Done in ISS
                // Break to wfi loop
                cmd_queue->head = (head + 1) % cmd_queue->capacity;
                break;
            }
            
            // Advance head
            cmd_queue->head = (head + 1) % cmd_queue->capacity;
        }
    }
    
    return 0;
}
