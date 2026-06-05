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
#define CORE_OUT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x0c))
#define CORE_STATUS(c)  (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x10))
#define CORE_SLIDE(c)   (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x14))

// Global variables for MatMul configuration (uninitialized -> .bss, zero by start.S)
uint32_t matmul_M;
uint32_t matmul_K;
uint32_t matmul_N;

// Utility Functions
static void wait_core_idle(int core) {
    while (CORE_STATUS(core) & 1) {}
}

static void wait_dma_idle(void) {
    while (DMA_STATUS & 1) {}
}

// Global state
static volatile npu_cmd_queue_t* cmd_queue = 0; // Host must pass this pointer via arg or known addr

static uint32_t soft_mul(uint32_t a, uint32_t b) {
    uint32_t res = 0;
    while (b > 0) {
        if (b & 1) res += a;
        a <<= 1;
        b >>= 1;
    }
    return res;
}

void execute_op(const npu_cmd_t* cmd) {
    switch (cmd->opcode) {
        case OP_NOP:
            break;
            
        case OP_DMA_READ: {
            wait_dma_idle();
            DMA_SRC = cmd->args.dma.ext_addr;
            DMA_DST = cmd->args.dma.tcdm_addr;
            DMA_DIM_X = cmd->args.dma.size;
            DMA_TRIGGER = 1;
            wait_dma_idle();
            break;
        }
        case OP_DMA_WRITE: {
            wait_dma_idle();
            DMA_SRC = cmd->args.dma.tcdm_addr;
            DMA_DST = cmd->args.dma.ext_addr;
            DMA_DIM_X = cmd->args.dma.size;
            DMA_TRIGGER = 1;
            wait_dma_idle();
            break;
        }
            
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
            
        case OP_CFG_MATMUL: { // 0x34
            matmul_M = cmd->args.fw_op.arg0;
            matmul_K = cmd->args.fw_op.arg1;
            matmul_N = cmd->args.fw_op.arg2;
            break;
        }
            
        case OP_COMPUTE_MATMUL: { // 0x35
            uint32_t act_base = cmd->args.compute.act_addr;
            uint32_t wgt_base = cmd->args.compute.wgt_addr;
            uint32_t out_base = cmd->args.compute.out_addr;
            
            uint32_t TILE_M = 1;
            uint32_t TILE_K = 32;
            uint32_t TILE_N = 4;
            
            // hardware requires K to be padded to multiple of 32
            uint32_t k_pad = ((matmul_K + 31) >> 5) << 5;
            uint32_t n_pad = ((matmul_N + 3) >> 2) << 2;
            
            uint32_t num_m_tiles = (matmul_M + TILE_M - 1) / TILE_M;
            uint32_t num_k_tiles = (matmul_K + TILE_K - 1) >> 5;
            uint32_t num_n_tiles = (matmul_N + TILE_N - 1) >> 2;
            
            // Dispatch across cores
            int core_idx = 0;
            for (uint32_t n = 0; n < num_n_tiles; n++) {
                for (uint32_t m = 0; m < num_m_tiles; m++) {
                    uint32_t act_addr = act_base + soft_mul(m, k_pad);
                    uint32_t wgt_addr = wgt_base + soft_mul((n << 2), k_pad);
                    uint32_t out_addr = out_base + soft_mul((n << 2), matmul_M) + (m << 2);
                    
                    wait_core_idle(core_idx);
                    
                    CORE_ACT(core_idx) = act_addr;
                    CORE_WGT(core_idx) = wgt_addr;
                    CORE_OUT(core_idx) = out_addr;
                    CORE_SLIDE(core_idx) = num_k_tiles;
                    CORE_CTRL(core_idx) = 0x05; // Trigger + Write_Out
                    
                    core_idx = core_idx + 1;
                    if (core_idx >= 10) core_idx = 0;
                }
            }
            break;
        }

        default:
            break;
    }
}

int main(void) {
    // 1. Setup: Host will pass the queue pointer in register x10 (a0) or x11 (a1).
    // For now, let's assume the Host places the queue at a fixed DDR address 
    // or passes it during boot. We'll use a fixed address for simplicity.
    uint32_t queue_addr = 0x1003f000; // TCDM address instead of DDR
    
    // In our Verilator tb, we pass Cluster ID in x10
    int cluster_id;
    __asm__ volatile ("mv %0, x10" : "=r"(cluster_id));
    
    // Each cluster gets its own queue (simplified layout)
    queue_addr += cluster_id * 0x100; 
    cmd_queue = (volatile npu_cmd_queue_t*)queue_addr;
    
    // Initialize head
    cmd_queue->head = 0;

    while (1) {
        // 2. Wait for Host Doorbell
        __asm__ volatile ("wfi");
        
        // Host rang the doorbell!
        
        // Use DMA to pull the command queue from Host DDR (0x80000000) to TCDM (0x1003f000)
        // Wait for any prior DMA to finish just in case
        while (DMA_STATUS & 1) {}
        DMA_SRC = 0x80000000 + cluster_id * 0x100000; // Source in DDR
        DMA_DST = queue_addr;                         // Dest in TCDM
        DMA_DIM_X = 2048;                             // Copy 2KB
        DMA_DIM_Y = 1;
        DMA_STRIDE_S = 0;
        DMA_STRIDE_D = 0;
        DMA_TRIGGER = 1;
        
        // Wait for DMA to complete
        while (DMA_STATUS & 1) {}
        
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
                head = head + 1;
                if (head >= cmd_queue->capacity) {
                    head = 0;
                }
                cmd_queue->head = head;
                break;
            }
            
            // Advance head
            head = head + 1;
            if (head >= cmd_queue->capacity) {
                head = 0;
            }
            cmd_queue->head = head;
        }
    }
    
    return 0;
}
