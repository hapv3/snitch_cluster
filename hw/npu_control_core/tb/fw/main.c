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

// DMA MMIO Addresses (Mapped at 0x7000_0000)
#define DMA_BASE 0x70000000
#define DMA_SRC_ADDR ((volatile unsigned int*)(DMA_BASE + 0x00))
#define DMA_DST_ADDR ((volatile unsigned int*)(DMA_BASE + 0x04))
#define DMA_DIM_X    ((volatile unsigned int*)(DMA_BASE + 0x08))
#define DMA_DIM_Y    ((volatile unsigned int*)(DMA_BASE + 0x0C))
#define DMA_STRIDE_S ((volatile unsigned int*)(DMA_BASE + 0x10))
#define DMA_STRIDE_D ((volatile unsigned int*)(DMA_BASE + 0x14))
#define DMA_TRIGGER  ((volatile unsigned int*)(DMA_BASE + 0x18))
#define DMA_STATUS   ((volatile unsigned int*)(DMA_BASE + 0x1C))

// NPU Compute Core Config Addresses (Mapped at 0x6000_0000)
#define NPU_BASE 0x60000000
#define NPU_BCAST_CTRL     ((volatile unsigned int*)(NPU_BASE + 0x0F00))
#define NPU_BCAST_ACT_PTR  ((volatile unsigned int*)(NPU_BASE + 0x0F04))
#define NPU_BCAST_WGT_PTR  ((volatile unsigned int*)(NPU_BASE + 0x0F08))
#define NPU_BCAST_VEC_LEN  ((volatile unsigned int*)(NPU_BASE + 0x0F0C))
#define NPU_CLUSTER_MASK   ((volatile unsigned int*)(NPU_BASE + 0x0F10))
#define NPU_CORE0_STATUS   ((volatile unsigned int*)(NPU_BASE + 0x0010))

// TCDM Base Address
#define TCDM_BASE 0x10000000

void wait_for_interrupt() {
    // Simple inline assembly for WFI (Wait For Interrupt)
    __asm__ volatile ("wfi");
}

int main() {
    // Initialization
    *NPU_CLUSTER_MASK = 0x000003FF; // Enable all 10 cores (bits 0-9)

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

        // 4. Program DMA to fetch Activations to TCDM (Broadcast weights later)
        *DMA_SRC_ADDR = act_ptr;
        *DMA_DST_ADDR = TCDM_BASE; // Buffer A
        *DMA_DIM_X = 128; // 128 bytes
        *DMA_DIM_Y = 1;
        *DMA_STRIDE_S = 128;
        *DMA_STRIDE_D = 128;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0); // Wait for DMA to complete

        *DMA_SRC_ADDR = wgt_ptr;
        *DMA_DST_ADDR = TCDM_BASE + 128; // Buffer B
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0);

        // 5. Broadcast computation to all 10 Cores concurrently!
        *NPU_BCAST_ACT_PTR = TCDM_BASE;
        *NPU_BCAST_WGT_PTR = TCDM_BASE + 128;
        *NPU_BCAST_CTRL = 0x3; // Bit 0 = Start, Bit 1 = Clear Accumulator

        // 6. Wait for Compute Cores to finish (poll Core 0 for simplicity, since they run in lockstep)
        while ((*NPU_CORE0_STATUS & 0x1) != 0);
        
        // 6.5. Unicast test on Core 5
        volatile unsigned int* CORE5_ACT_PTR = (volatile unsigned int*)(NPU_BASE + (5 * 32) + 0x04);
        volatile unsigned int* CORE5_WGT_PTR = (volatile unsigned int*)(NPU_BASE + (5 * 32) + 0x08);
        volatile unsigned int* CORE5_CTRL = (volatile unsigned int*)(NPU_BASE + (5 * 32) + 0x00);
        volatile unsigned int* CORE5_STATUS = (volatile unsigned int*)(NPU_BASE + (5 * 32) + 0x10);

        *CORE5_ACT_PTR = TCDM_BASE;
        *CORE5_WGT_PTR = TCDM_BASE + 128;
        *CORE5_CTRL = 0x3; // Start Core 5 only
        
        while ((*CORE5_STATUS & 0x1) != 0); // Wait for Core 5 to finish

        // 7. Signal Task Complete back to ARM Host (Bit 1 of CONTROL)
        *MBOX_CONTROL = 0x2;
    }
    return 0;
}
