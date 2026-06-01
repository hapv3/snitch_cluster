// Copyright 2026 NPU IP
// Firmware for NPU Control Core (RISC-V)

// Mailbox MMIO Addresses
#define MBOX_BASE 0x40000000
#define MBOX_STATUS  ((volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_CONTROL ((volatile unsigned int*)(MBOX_BASE + 0x04))
#define MBOX_TASK_OP ((volatile unsigned int*)(MBOX_BASE + 0x08))
#define MBOX_ACT_PTR ((volatile unsigned int*)(MBOX_BASE + 0x0C))
#define MBOX_WGT_PTR ((volatile unsigned int*)(MBOX_BASE + 0x10))
#define MBOX_OUT_PTR ((volatile unsigned int*)(MBOX_BASE + 0x14))
#define MBOX_DIM_M   ((volatile unsigned int*)(MBOX_BASE + 0x18))
#define MBOX_DIM_N   ((volatile unsigned int*)(MBOX_BASE + 0x1C))
#define MBOX_DIM_K   ((volatile unsigned int*)(MBOX_BASE + 0x20))

// DMA MMIO Addresses (assuming DMA is mapped at 0x50000000)
#define DMA_BASE 0x50000000
#define DMA_SRC_ADDR ((volatile unsigned int*)(DMA_BASE + 0x00))
#define DMA_DST_ADDR ((volatile unsigned int*)(DMA_BASE + 0x04))
#define DMA_SIZE     ((volatile unsigned int*)(DMA_BASE + 0x08))
#define DMA_TRIGGER  ((volatile unsigned int*)(DMA_BASE + 0x0C))
#define DMA_STATUS   ((volatile unsigned int*)(DMA_BASE + 0x10))

// NPU Compute Core Config Addresses (mapped at 0x60000000)
#define NPU_BASE 0x60000000
#define NPU_CTRL     ((volatile unsigned int*)(NPU_BASE + 0x00))
#define NPU_ACT_CFG  ((volatile unsigned int*)(NPU_BASE + 0x04))

// TCDM Base Address
#define TCDM_BASE 0x10000000

void wait_for_interrupt() {
    // Simple inline assembly for WFI (Wait For Interrupt)
    __asm__ volatile ("wfi");
}

int main() {
    while (1) {
        // 1. Wait for task from ARM Host (Bit 0 of STATUS)
        while ((*MBOX_STATUS & 0x1) == 0) {
            wait_for_interrupt();
        }

        // 2. Acknowledge task pending
        *MBOX_CONTROL = 0x1;

        // 3. Read Task Descriptor
        unsigned int op = *MBOX_TASK_OP;
        unsigned int act_ptr = *MBOX_ACT_PTR;
        unsigned int wgt_ptr = *MBOX_WGT_PTR;
        unsigned int out_ptr = *MBOX_OUT_PTR;
        unsigned int dim_m = *MBOX_DIM_M;
        unsigned int dim_n = *MBOX_DIM_N;
        unsigned int dim_k = *MBOX_DIM_K;

        // 4. Program DMA to fetch Activations to TCDM Bank 0
        *DMA_SRC_ADDR = act_ptr;
        *DMA_DST_ADDR = TCDM_BASE;
        *DMA_SIZE = dim_m * dim_k;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0); // Wait for DMA

        // 5. Program DMA to fetch Weights to TCDM Bank 1
        *DMA_SRC_ADDR = wgt_ptr;
        *DMA_DST_ADDR = TCDM_BASE + 0x10000; // Offset by 64KB
        *DMA_SIZE = dim_k * dim_n;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0); // Wait for DMA

        // 6. Configure NPU Compute Core
        *NPU_ACT_CFG = op; // Set activation (ReLU etc.)
        
        // 7. Start NPU Compute
        *NPU_CTRL = 1; 
        while ((*NPU_CTRL & 0x1) != 0); // Wait for NPU

        // 8. Program DMA to write Output back to ARM RAM
        *DMA_SRC_ADDR = TCDM_BASE + 0x20000;
        *DMA_DST_ADDR = out_ptr;
        *DMA_SIZE = dim_m * dim_n * 4; // INT32 accumulator outputs
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0); // Wait for DMA

        // 9. Signal Task Complete back to ARM Host (Bit 1 of CONTROL)
        *MBOX_CONTROL = 0x2;
    }
    return 0;
}
