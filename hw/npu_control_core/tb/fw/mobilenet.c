// Firmware for MobileNetV2 Layer - Distributed across 4 clusters
// Each cluster processes 1/4th of the output channels

#define MBOX_BASE 0x40000000
#define MBOX_STATUS  ((volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_CONTROL ((volatile unsigned int*)(MBOX_BASE + 0x04))

#define DMA_BASE 0x70000000
#define DMA_SRC_ADDR ((volatile unsigned int*)(DMA_BASE + 0x00))
#define DMA_DST_ADDR ((volatile unsigned int*)(DMA_BASE + 0x04))
#define DMA_DIM_X    ((volatile unsigned int*)(DMA_BASE + 0x08))
#define DMA_DIM_Y    ((volatile unsigned int*)(DMA_BASE + 0x0C))
#define DMA_STRIDE_S ((volatile unsigned int*)(DMA_BASE + 0x10))
#define DMA_STRIDE_D ((volatile unsigned int*)(DMA_BASE + 0x14))
#define DMA_TRIGGER  ((volatile unsigned int*)(DMA_BASE + 0x18))
#define DMA_STATUS   ((volatile unsigned int*)(DMA_BASE + 0x1C))

#define NPU_BASE 0x60000000
#define NPU_CLUSTER_MASK   ((volatile unsigned int*)(NPU_BASE + 0x0F10))
#define NPU_CORE_CTRL(c)   ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x00))
#define NPU_CORE_ACT(c)    ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x04))
#define NPU_CORE_WGT(c)    ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x08))
#define NPU_CORE_OUT(c)    ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x0C))
#define NPU_CORE_STATUS(c) ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x10))
#define NPU_CORE_STRIDE_SLIDE(c) ((volatile unsigned int*)(NPU_BASE + (c * 32) + 0x14))

#define TCDM_BASE 0x10000000

void wait_for_interrupt() {
    __asm__ volatile ("wfi");
}

int main() {
    // Cluster ID is passed in register a0 (x10) by the testbench
    int cluster_id;
    __asm__ volatile ("mv %0, a0" : "=r"(cluster_id));

    *NPU_CLUSTER_MASK = 0x000003FF; // Enable all cores

    while (1) {
        while ((*MBOX_STATUS & 0x1) == 0) wait_for_interrupt();
        *MBOX_CONTROL = 0x1; // Ack

        // In a real MobileNetV2 layer, we would fetch tiles.
        // For this integration test, we do a dummy fetch to simulate bandwidth
        
        // Fetch Activations
        *DMA_SRC_ADDR = 0x20000000 + cluster_id * 1024;
        *DMA_DST_ADDR = TCDM_BASE;
        *DMA_DIM_X = 512;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0);

        // Fetch Weights
        *DMA_SRC_ADDR = 0x30000000 + cluster_id * 2048;
        *DMA_DST_ADDR = TCDM_BASE + 512;
        *DMA_DIM_X = 1152;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0);

        // Simulate a small parallel processing task across 4 cores
        for (int w = 0; w < 9; w++) {
            int is_first = (w == 0);
            int is_last = (w == 8);

            // Core 0
            *NPU_CORE_ACT(0) = TCDM_BASE;
            *NPU_CORE_WGT(0) = TCDM_BASE + 512 + w * 128;
            *NPU_CORE_OUT(0) = TCDM_BASE + 2000;
            *NPU_CORE_STRIDE_SLIDE(0) = (3 << 8) | 1; // slide_count = 3, stride = 1
            *NPU_CORE_CTRL(0) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

            // Core 1
            *NPU_CORE_ACT(1) = TCDM_BASE + 32;
            *NPU_CORE_WGT(1) = TCDM_BASE + 512 + w * 128;
            *NPU_CORE_OUT(1) = TCDM_BASE + 2004 + 16;
            *NPU_CORE_STRIDE_SLIDE(1) = (3 << 8) | 1;
            *NPU_CORE_CTRL(1) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

            // Core 2
            *NPU_CORE_ACT(2) = TCDM_BASE + 64;
            *NPU_CORE_WGT(2) = TCDM_BASE + 512 + w * 128;
            *NPU_CORE_OUT(2) = TCDM_BASE + 2008 + 32;
            *NPU_CORE_STRIDE_SLIDE(2) = (3 << 8) | 1;
            *NPU_CORE_CTRL(2) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

            // Core 3
            *NPU_CORE_ACT(3) = TCDM_BASE + 96;
            *NPU_CORE_WGT(3) = TCDM_BASE + 512 + w * 128;
            *NPU_CORE_OUT(3) = TCDM_BASE + 2012 + 48;
            *NPU_CORE_STRIDE_SLIDE(3) = (3 << 8) | 1;
            *NPU_CORE_CTRL(3) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

            while ((*NPU_CORE_STATUS(0) & 0x1) != 0);
            while ((*NPU_CORE_STATUS(1) & 0x1) != 0);
            while ((*NPU_CORE_STATUS(2) & 0x1) != 0);
            while ((*NPU_CORE_STATUS(3) & 0x1) != 0);
        }

        // Notify Host that this cluster finished
        *MBOX_CONTROL = 0x2;
    }
    return 0;
}
