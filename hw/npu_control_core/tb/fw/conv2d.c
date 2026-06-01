// Firmware for Conv2D $4\times4$ input, $3\times3$ kernel -> $2\times2$ output

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

#define TCDM_BASE 0x10000000

void wait_for_interrupt() {
    __asm__ volatile ("wfi");
}

// Core spatial mappings
static const int core_x[4] = {0, 1, 0, 1};
static const int core_y[4] = {0, 0, 1, 1};

int main() {
    *NPU_CLUSTER_MASK = 0x000003FF;

    while (1) {
        while ((*MBOX_STATUS & 0x1) == 0) wait_for_interrupt();
        *MBOX_CONTROL = 0x1; // Ack

        // Fetch Activations (4x4x32 bytes = 512 bytes)
        *DMA_SRC_ADDR = 0x20000000;
        *DMA_DST_ADDR = TCDM_BASE;
        *DMA_DIM_X = 512;
        *DMA_DIM_Y = 1;
        *DMA_STRIDE_S = 512;
        *DMA_STRIDE_D = 512;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0);

        // Fetch Weights (9 * 4 * 32 bytes = 1152 bytes)
        *DMA_SRC_ADDR = 0x30000000;
        *DMA_DST_ADDR = TCDM_BASE + 512;
        *DMA_DIM_X = 1152;
        *DMA_TRIGGER = 1;
        while ((*DMA_STATUS & 0x1) != 0);

        // Conv2D 3x3 over 4x4 input -> 2x2 output.
        // We use Core 0, 1, 2, 3 for the 4 output pixels.
        // Input layout: [y][x][c], where c is 32 bytes.
        // Stride is 32 bytes per x, 128 bytes per y.
        // Core 0: y=0, x=0 -> window y in [0..2], x in [0..2]
        // Core 1: y=0, x=1 -> window y in [0..2], x in [1..3]
        // Core 2: y=1, x=0 -> window y in [1..3], x in [0..2]
        // Core 3: y=1, x=1 -> window y in [1..3], x in [1..3]

        for (int wy = 0; wy < 3; wy++) {
            for (int wx = 0; wx < 3; wx++) {
                int w_idx = wy * 3 + wx;
                int is_first = (w_idx == 0);
                int is_last = (w_idx == 8);
                
                // Trigger 4 cores
                // Core 0
                *NPU_CORE_ACT(0) = TCDM_BASE + (core_y[0] * 4 + core_x[0] + wy * 4 + wx) * 32;
                *NPU_CORE_WGT(0) = TCDM_BASE + 512 + w_idx * 128;
                *NPU_CORE_OUT(0) = TCDM_BASE + 2000 + 0;
                *NPU_CORE_CTRL(0) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

                // Core 1
                *NPU_CORE_ACT(1) = TCDM_BASE + (core_y[1] * 4 + core_x[1] + wy * 4 + wx) * 32;
                *NPU_CORE_WGT(1) = TCDM_BASE + 512 + w_idx * 128;
                *NPU_CORE_OUT(1) = TCDM_BASE + 2000 + 4;
                *NPU_CORE_CTRL(1) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

                // Core 2
                *NPU_CORE_ACT(2) = TCDM_BASE + (core_y[2] * 4 + core_x[2] + wy * 4 + wx) * 32;
                *NPU_CORE_WGT(2) = TCDM_BASE + 512 + w_idx * 128;
                *NPU_CORE_OUT(2) = TCDM_BASE + 2000 + 8;
                *NPU_CORE_CTRL(2) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);

                // Core 3
                *NPU_CORE_ACT(3) = TCDM_BASE + (core_y[3] * 4 + core_x[3] + wy * 4 + wx) * 32;
                *NPU_CORE_WGT(3) = TCDM_BASE + 512 + w_idx * 128;
                *NPU_CORE_OUT(3) = TCDM_BASE + 2000 + 12;
                *NPU_CORE_CTRL(3) = 0x1 | (is_first ? 0x2 : 0) | (is_last ? 0x4 : 0);
                
                // Wait for 4 cores
                while ((*NPU_CORE_STATUS(0) & 0x1) != 0);
                while ((*NPU_CORE_STATUS(1) & 0x1) != 0);
                while ((*NPU_CORE_STATUS(2) & 0x1) != 0);
                while ((*NPU_CORE_STATUS(3) & 0x1) != 0);
            }
        }

        // Complete
        *MBOX_CONTROL = 0x2;
    }
    return 0;
}
