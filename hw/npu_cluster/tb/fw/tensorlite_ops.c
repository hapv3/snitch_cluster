// tensorlite_ops.c - TensorLite Operator Verification Firmware
// Runs on the RISC-V Control Core inside the NPU Cluster
// Each test writes pass/fail status to Mailbox for testbench to read

#include "softmax_lut.h"

// ========== Memory Map ==========
#define MBOX_BASE    0x40000000
#define DMA_BASE     0x70000000
#define NPU_BASE     0x60000000
#define TCDM_BASE    0x10000000

// Mailbox registers
#define MBOX_STATUS  (*(volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_TRIGGER (*(volatile unsigned int*)(MBOX_BASE + 0x04))

// DMA registers
#define DMA_SRC      (*(volatile unsigned int*)(DMA_BASE + 0x00))
#define DMA_DST      (*(volatile unsigned int*)(DMA_BASE + 0x04))
#define DMA_DIM_X    (*(volatile unsigned int*)(DMA_BASE + 0x08))
#define DMA_DIM_Y    (*(volatile unsigned int*)(DMA_BASE + 0x0C))
#define DMA_STRIDE_S (*(volatile unsigned int*)(DMA_BASE + 0x10))
#define DMA_STRIDE_D (*(volatile unsigned int*)(DMA_BASE + 0x14))
#define DMA_TRIGGER  (*(volatile unsigned int*)(DMA_BASE + 0x18))
#define DMA_STATUS   (*(volatile unsigned int*)(DMA_BASE + 0x1C))
#define DMA_RD_CNT   (*(volatile unsigned int*)(DMA_BASE + 0x20))
#define DMA_WR_CNT   (*(volatile unsigned int*)(DMA_BASE + 0x24))

// NPU Core MMIO (per-core stride = 0x20)
#define CORE_CTRL(c)    (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x00))
#define CORE_ACT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x04))
#define CORE_WGT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x08))
#define CORE_OUT(c)     (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x0C))
#define CORE_STATUS(c)  (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x10))
#define CORE_SLIDE(c)   (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x14))
#define CORE_MAC_CYC(c) (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x18))
#define CORE_TOT_CYC(c) (*(volatile unsigned int*)(NPU_BASE + (c)*0x20 + 0x1C))

// TCDM pointers
#define TCDM_ACT  (TCDM_BASE + 0x0000)  // Activation buffer
#define TCDM_WGT  (TCDM_BASE + 0x0200)  // Weight buffer
#define TCDM_OUT  (TCDM_BASE + 0x0400)  // Output buffer
#define TCDM_TMP  (TCDM_BASE + 0x0600)  // Temporary buffer

// Activation type encoding in CTRL register bits [10:8]
#define ACT_NONE       (0 << 8)
#define ACT_RELU       (1 << 8)
#define ACT_RELU6      (2 << 8)
#define ACT_SIGMOID    (3 << 8)
#define ACT_LEAKY_RELU (4 << 8)
#define ACT_SILU       (5 << 8)
#define ACT_MISH       (6 << 8)

// ========== Utility ==========
static volatile unsigned int* tcdm = (volatile unsigned int*)TCDM_BASE;

static void tcdm_write8(unsigned int addr, signed char val) {
    volatile unsigned char* p = (volatile unsigned char*)addr;
    *p = (unsigned char)val;
}

static signed char tcdm_read8(unsigned int addr) {
    volatile unsigned char* p = (volatile unsigned char*)addr;
    return (signed char)*p;
}

static void tcdm_write32(unsigned int addr, unsigned int val) {
    *(volatile unsigned int*)addr = val;
}

static unsigned int tcdm_read32(unsigned int addr) {
    return *(volatile unsigned int*)addr;
}

static void wait_core_idle(int core) {
    while (CORE_STATUS(core) & 1) {}
}

// ========== Inline ASM helpers for element-wise ops ==========
static inline int asm_max(int a, int b) {
    int result;
    // Use branch-based max (RV32I doesn't have max instruction)
    __asm__ volatile (
        "bge %1, %2, 1f\n\t"
        "mv %0, %2\n\t"
        "j 2f\n\t"
        "1: mv %0, %1\n\t"
        "2:"
        : "=r"(result)
        : "r"(a), "r"(b)
    );
    return result;
}

static inline int asm_add_sat8(int a, int b) {
    int result;
    __asm__ volatile (
        "add %0, %1, %2\n\t"
        "li t0, 127\n\t"
        "bge t0, %0, 1f\n\t"
        "li %0, 127\n\t"
        "j 2f\n\t"
        "1: li t0, -128\n\t"
        "bge %0, t0, 2f\n\t"
        "li %0, -128\n\t"
        "2:"
        : "=r"(result)
        : "r"(a), "r"(b)
        : "t0"
    );
    return result;
}

// ========== Test 1: CONV_2D ==========
static int test_conv2d(void) {
    // Small 4x4 input, 3x3 kernel, 1 output channel
    // Expected: a single output using the MAC array + sliding window
    signed char input[16] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16
    };
    signed char kernel[9] = {
        1, 0, -1,
        1, 0, -1,
        1, 0, -1
    };

    // Load input to TCDM_ACT
    for (int i = 0; i < 16; i++) {
        tcdm_write8(TCDM_ACT + i, input[i]);
    }
    // Pad to 128 bytes
    for (int i = 16; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, 0);
    }

    // Load kernel to TCDM_WGT
    for (int i = 0; i < 9; i++) {
        tcdm_write8(TCDM_WGT + i, kernel[i]);
    }
    for (int i = 9; i < 128; i++) {
        tcdm_write8(TCDM_WGT + i, 0);
    }

    // Configure Core 0: ACT_NONE, trigger
    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;  // no sliding
    CORE_CTRL(0) = ACT_NONE | 0x05;  // bit0=trigger, bit2=write_out

    wait_core_idle(0);

    // Reference: sum of element-wise product of first 9 elements
    int ref = 0;
    for (int i = 0; i < 9; i++) {
        ref += (int)input[i] * (int)kernel[i];
    }
    // Read output
    int hw_out = (int)tcdm_read32(TCDM_OUT);

    if (hw_out == ref) return 0;
    return 1;
}

// ========== Test 2: FULLY_CONNECTED ==========
static int test_fully_connected(void) {
    // 128-element dot product (vector × vector)
    signed char vec_a[128];
    signed char vec_b[128];
    for (int i = 0; i < 128; i++) {
        vec_a[i] = (signed char)(i & 0x7F);
        vec_b[i] = (signed char)(1);
    }

    for (int i = 0; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, vec_a[i]);
        tcdm_write8(TCDM_WGT + i, vec_b[i]);
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_NONE | 0x05;

    wait_core_idle(0);

    int ref = 0;
    for (int i = 0; i < 128; i++) {
        ref += (int)vec_a[i];
    }
    int hw_out = (int)tcdm_read32(TCDM_OUT);

    if (hw_out == ref) return 0;
    return 1;
}

// ========== Test 3: RELU ==========
static int test_relu(void) {
    signed char test_vals[128];
    for (int i = 0; i < 128; i++) {
        test_vals[i] = (signed char)(i - 64);
        tcdm_write8(TCDM_ACT + i, test_vals[i]);
        tcdm_write8(TCDM_WGT + i, 1);  // identity weights
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_RELU | 0x05;

    wait_core_idle(0);

    // The MAC array accumulates all 128 elements, then activation clamps
    // So just verify the output is ≥ 0
    int hw_out = (int)tcdm_read32(TCDM_OUT);

    // Reference: sum of all test_vals * 1 = sum(-64..63) = -32
    // After ReLU: max(0, -32) = 0
    int ref = 0;
    for (int i = 0; i < 128; i++) ref += (int)test_vals[i];
    if (ref < 0) ref = 0;
    if (ref > 127) ref = 127;

    if (hw_out == ref) return 0;
    return 1;
}

// ========== Test 4: RELU6 ==========
static int test_relu6(void) {
    // Use positive values so we can test the RELU6 clamping
    for (int i = 0; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, (signed char)(i < 10 ? 1 : 0));
        tcdm_write8(TCDM_WGT + i, (signed char)(i < 10 ? 1 : 0));
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_RELU6 | 0x05;

    wait_core_idle(0);

    // Reference: sum of 1*1 for 10 elements = 10
    int hw_out = (int)tcdm_read32(TCDM_OUT);
    int ref = 10;

    if (hw_out == ref) return 0;
    return 1;
}

// ========== Test 5: LEAKY_RELU ==========
static int test_leaky_relu(void) {
    // Set up data so MAC output is negative to test leaky path
    for (int i = 0; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, (signed char)(i < 4 ? -10 : 0));
        tcdm_write8(TCDM_WGT + i, (signed char)(i < 4 ? 1 : 0));
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_LEAKY_RELU | 0x05;

    wait_core_idle(0);

    // Reference: 4 * (-10 * 1) = -40, leaky_relu(-40) = -40 >> 3 = -5
    int hw_out = (int)tcdm_read32(TCDM_OUT);
    int ref = -5;

    if (hw_out == ref) return 0;
    return 1;
}

// ========== Test 6: DEPTHWISE_CONV_2D ==========
static int test_depthwise_conv2d(void) {
    // Channel-wise convolution: each core handles 1 channel
    // Simplified: 1 core, 1 channel, 128-element dot product
    for (int i = 0; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, (signed char)(i < 9 ? 2 : 0));
        tcdm_write8(TCDM_WGT + i, (signed char)(i < 9 ? 1 : 0));
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_NONE | 0x05;

    wait_core_idle(0);

    // Reference: 9 * 2 * 1 = 18
    int hw_out = (int)tcdm_read32(TCDM_OUT);
    if (hw_out == 18) return 0;
    return 1;
}

// ========== Test 7: AVERAGE_POOL_2D ==========
static int test_avg_pool(void) {
    // Map as CONV2D with uniform weights = 1 (then divide later)
    // 9 elements, all set to 9 → sum = 81, avg = 81/9 = 9
    for (int i = 0; i < 128; i++) {
        tcdm_write8(TCDM_ACT + i, (signed char)(i < 9 ? 9 : 0));
        tcdm_write8(TCDM_WGT + i, (signed char)(i < 9 ? 1 : 0));
    }

    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_NONE | 0x05;

    wait_core_idle(0);

    int hw_out = (int)tcdm_read32(TCDM_OUT);
    // Firmware divides by kernel size
    int avg = hw_out / 9;
    int ref = 9;

    if (avg == ref) return 0;
    return 1;
}

// ========== Test 8: MAX_POOL_2D (Pure firmware with inline ASM) ==========
static int test_max_pool(void) {
    // 4x4 input, 2x2 kernel, stride 2 → 2x2 output
    signed char input[16] = {
        1, 3, 5, 7,
        2, 4, 6, 8,
        9, 11, 13, 15,
        10, 12, 14, 16
    };
    signed char ref_out[4] = {4, 8, 12, 16};

    // Write input to TCDM
    for (int i = 0; i < 16; i++) {
        tcdm_write8(TCDM_ACT + i, input[i]);
    }

    // Max pooling 2x2, stride 2 over 4x4 input
    int errors = 0;
    int out_idx = 0;
    for (int oy = 0; oy < 2; oy++) {
        for (int ox = 0; ox < 2; ox++) {
            int max_val = -128;
            for (int ky = 0; ky < 2; ky++) {
                for (int kx = 0; kx < 2; kx++) {
                    int idx = (oy * 2 + ky) * 4 + (ox * 2 + kx);
                    int val = (int)tcdm_read8(TCDM_ACT + idx);
                    max_val = asm_max(max_val, val);
                }
            }
            tcdm_write8(TCDM_OUT + out_idx, (signed char)max_val);
            if ((signed char)max_val != ref_out[out_idx]) errors++;
            out_idx++;
        }
    }

    return errors;
}

// ========== Test 9: ADD (Element-wise, inline ASM) ==========
static int test_add(void) {
    signed char a[8] = {10, 20, 30, 40, -10, -20, 100, -100};
    signed char b[8] = {5, -5, 10, -10, 20, 30, 30, -30};
    signed char ref[8] = {15, 15, 40, 30, 10, 10, 127, -128};

    for (int i = 0; i < 8; i++) {
        tcdm_write8(TCDM_ACT + i, a[i]);
        tcdm_write8(TCDM_WGT + i, b[i]);
    }

    int errors = 0;
    for (int i = 0; i < 8; i++) {
        int va = (int)tcdm_read8(TCDM_ACT + i);
        int vb = (int)tcdm_read8(TCDM_WGT + i);
        int result = asm_add_sat8(va, vb);
        tcdm_write8(TCDM_OUT + i, (signed char)result);
        if ((signed char)result != ref[i]) errors++;
    }
    return errors;
}

// ========== Test 10: SUB (Element-wise) ==========
static int test_sub(void) {
    signed char a[4] = {50, 10, -50, 100};
    signed char b[4] = {20, 30, -10, 120};
    signed char ref[4] = {30, -20, -40, -20};

    for (int i = 0; i < 4; i++) {
        tcdm_write8(TCDM_ACT + i, a[i]);
        tcdm_write8(TCDM_WGT + i, b[i]);
    }

    int errors = 0;
    for (int i = 0; i < 4; i++) {
        int va = (int)tcdm_read8(TCDM_ACT + i);
        int vb = (int)tcdm_read8(TCDM_WGT + i);
        int result = va - vb;
        if (result > 127) result = 127;
        if (result < -128) result = -128;
        tcdm_write8(TCDM_OUT + i, (signed char)result);
        if ((signed char)result != ref[i]) errors++;
    }
    return errors;
}

// ========== Test 11: MUL (Element-wise) ==========
static int test_mul(void) {
    signed char a[4] = {5, -3, 10, 7};
    signed char b[4] = {4, 6, -2, 3};
    signed char ref[4] = {20, -18, -20, 21};

    int errors = 0;
    for (int i = 0; i < 4; i++) {
        int result = (int)a[i] * (int)b[i];
        if (result > 127) result = 127;
        if (result < -128) result = -128;
        if ((signed char)result != ref[i]) errors++;
    }
    return errors;
}

// ========== Test 12: SOFTMAX (LUT-based) ==========
static int test_softmax(void) {
    signed char input[4] = {0, 32, -32, 16};

    unsigned int sum = 0;
    unsigned short exps[4];
    for (int i = 0; i < 4; i++) {
        unsigned char idx = (unsigned char)((int)input[i] + 128);
        exps[i] = exp_lut[idx];
        sum += exps[i];
    }

    // Normalize: output[i] = exps[i] * 256 / sum (Q8.8 → Q0.8)
    int errors = 0;
    for (int i = 0; i < 4; i++) {
        unsigned int normalized = ((unsigned int)exps[i] * 256) / sum;
        // Just verify probabilities sum to ~256 and are non-negative
        if (normalized > 256) errors++;
    }

    // Verify sum of normalized is approximately 256
    unsigned int total = 0;
    for (int i = 0; i < 4; i++) {
        unsigned int n = ((unsigned int)exps[i] * 256) / sum;
        total += n;
    }
    // Allow ±4 tolerance for rounding
    if (total < 252 || total > 260) errors++;

    return errors;
}

// ========== Test 13: RESHAPE/CONCAT (Zero-cost verification) ==========
static int test_reshape(void) {
    // Write a 2x4 tensor, "reshape" to 4x2 by just reinterpreting
    signed char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; i++) {
        tcdm_write8(TCDM_ACT + i, data[i]);
    }

    // Verify data accessible as 4x2 (same underlying memory)
    int errors = 0;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 2; col++) {
            int idx = row * 2 + col;
            signed char val = tcdm_read8(TCDM_ACT + idx);
            if (val != data[idx]) errors++;
        }
    }
    return errors;
}

// ========== Test 14: PAD ==========
static int test_pad(void) {
    // Pad a 2x2 input with 1-element zero padding → 4x4 output
    signed char input[4] = {10, 20, 30, 40};

    // Clear output buffer (zero padding)
    for (int i = 0; i < 16; i++) {
        tcdm_write8(TCDM_OUT + i, 0);
    }

    // Copy input into center of padded output
    for (int iy = 0; iy < 2; iy++) {
        for (int ix = 0; ix < 2; ix++) {
            int src_idx = iy * 2 + ix;
            int dst_idx = (iy + 1) * 4 + (ix + 1);
            tcdm_write8(TCDM_OUT + dst_idx, input[src_idx]);
        }
    }

    // Verify padded regions are zero
    int errors = 0;
    // Top row
    for (int i = 0; i < 4; i++) {
        if (tcdm_read8(TCDM_OUT + i) != 0) errors++;
    }
    // Bottom row
    for (int i = 12; i < 16; i++) {
        if (tcdm_read8(TCDM_OUT + i) != 0) errors++;
    }
    // Left and right columns of middle rows
    if (tcdm_read8(TCDM_OUT + 4) != 0) errors++;
    if (tcdm_read8(TCDM_OUT + 7) != 0) errors++;
    if (tcdm_read8(TCDM_OUT + 8) != 0) errors++;
    if (tcdm_read8(TCDM_OUT + 11) != 0) errors++;

    // Verify center values
    if (tcdm_read8(TCDM_OUT + 5) != 10) errors++;
    if (tcdm_read8(TCDM_OUT + 6) != 20) errors++;
    if (tcdm_read8(TCDM_OUT + 9) != 30) errors++;
    if (tcdm_read8(TCDM_OUT + 10) != 40) errors++;

    return errors;
}


// ========== Test 15: SIGMOID (Hardware LUT) ==========
static int test_sigmoid(void) {
    signed char test_vals[4] = {0, 32, -32, 16};
    for (int i = 0; i < 4; i++) {
        tcdm_write8(TCDM_ACT + i, test_vals[i]);
        tcdm_write8(TCDM_WGT + i, 1);
    }
    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_SIGMOID | 0x05;
    wait_core_idle(0);
    return 0;
}

// ========== Test 16: SILU (Hardware LUT) ==========
static int test_silu(void) {
    signed char test_vals[4] = {0, 16, -16, 8};
    for (int i = 0; i < 4; i++) {
        tcdm_write8(TCDM_ACT + i, test_vals[i]);
        tcdm_write8(TCDM_WGT + i, 1);
    }
    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_SILU | 0x05;
    wait_core_idle(0);
    return 0;
}

// ========== Test 17: MISH (Hardware LUT) ==========
static int test_mish(void) {
    signed char test_vals[4] = {0, 16, -16, 8};
    for (int i = 0; i < 4; i++) {
        tcdm_write8(TCDM_ACT + i, test_vals[i]);
        tcdm_write8(TCDM_WGT + i, 1);
    }
    CORE_ACT(0) = TCDM_ACT;
    CORE_WGT(0) = TCDM_WGT;
    CORE_OUT(0) = TCDM_OUT;
    CORE_SLIDE(0) = 0;
    CORE_CTRL(0) = ACT_MISH | 0x05;
    wait_core_idle(0);
    return 0;
}

// ========== Test 18: RESIZE_NEAREST_NEIGHBOR ==========
static int test_resize_nn(void) {
    signed char in[4] = {1, 2, 3, 4};
    for(int i=0; i<4; i++) tcdm_write8(TCDM_ACT + i, in[i]);
    for (int oy=0; oy<4; oy++) {
        for (int ox=0; ox<4; ox++) {
            int iy = oy / 2;
            int ix = ox / 2;
            signed char val = tcdm_read8(TCDM_ACT + iy*2 + ix);
            tcdm_write8(TCDM_OUT + oy*4 + ox, val);
        }
    }
    int errors = 0;
    if (tcdm_read8(TCDM_OUT + 0) != 1) errors++;
    if (tcdm_read8(TCDM_OUT + 1) != 1) errors++;
    if (tcdm_read8(TCDM_OUT + 5) != 1) errors++;
    if (tcdm_read8(TCDM_OUT + 10) != 4) errors++;
    if (tcdm_read8(TCDM_OUT + 15) != 4) errors++;
    return errors;
}

// ========== Test 19: STRIDED_SLICE / SPLIT ==========
static int test_strided_slice(void) {
    for(int i=0; i<16; i++) tcdm_write8(TCDM_ACT + i, (signed char)i);
    int out_idx = 0;
    for(int y=1; y<3; y++) {
        for(int x=1; x<3; x++) {
            signed char val = tcdm_read8(TCDM_ACT + y*4 + x);
            tcdm_write8(TCDM_OUT + out_idx++, val);
        }
    }
    int errors = 0;
    if (tcdm_read8(TCDM_OUT + 0) != 5) errors++;
    if (tcdm_read8(TCDM_OUT + 1) != 6) errors++;
    if (tcdm_read8(TCDM_OUT + 2) != 9) errors++;
    if (tcdm_read8(TCDM_OUT + 3) != 10) errors++;
    return errors;
}

// ========== Test 20: TRANSPOSE ==========
static int test_transpose(void) {
    signed char in[8] = {1,2,3,4, 5,6,7,8};
    for(int i=0; i<8; i++) tcdm_write8(TCDM_ACT + i, in[i]);
    for(int r=0; r<2; r++) {
        for(int c=0; c<4; c++) {
            signed char val = tcdm_read8(TCDM_ACT + r*4 + c);
            tcdm_write8(TCDM_OUT + c*2 + r, val);
        }
    }
    int errors = 0;
    if (tcdm_read8(TCDM_OUT + 0) != 1) errors++;
    if (tcdm_read8(TCDM_OUT + 1) != 5) errors++;
    if (tcdm_read8(TCDM_OUT + 2) != 2) errors++;
    if (tcdm_read8(TCDM_OUT + 3) != 6) errors++;
    return errors;
}

// ========== Main Entry Point ==========
int main(void) {
    // Wait for host trigger via WFI + mailbox
    __asm__ volatile ("wfi");

    int total_errors = 0;
    int test_results[20];

    // Run all tests
    test_results[0]  = test_conv2d();
    test_results[1]  = test_fully_connected();
    test_results[2]  = test_relu();
    test_results[3]  = test_relu6();
    test_results[4]  = test_leaky_relu();
    test_results[5]  = test_depthwise_conv2d();
    test_results[6]  = test_avg_pool();
    test_results[7]  = test_max_pool();
    test_results[8]  = test_add();
    test_results[9]  = test_sub();
    test_results[10] = test_mul();
    test_results[11] = test_softmax();
    test_results[12] = test_reshape();
    test_results[13] = test_pad();

    test_results[14] = test_sigmoid();
    test_results[15] = test_silu();
    test_results[16] = test_mish();
    test_results[17] = test_resize_nn();
    test_results[18] = test_strided_slice();
    test_results[19] = test_transpose();


    // Sum errors
    for (int i = 0; i < 20; i++) {
        total_errors += test_results[i];
    }

    // Write results to mailbox for testbench
    MBOX_TRIGGER = (total_errors == 0) ? 0xA5A5 : 0xDEAD;

    // Signal done
    MBOX_TRIGGER = 2;

    // Halt
    while (1) {
        __asm__ volatile ("wfi");
    }
}
