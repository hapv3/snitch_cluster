#include "test_utils.h"
#include "../../../../sw/npu_driver/npu_mmio.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>

using namespace std;

const int8_t lut_silu[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -2, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127
};

int main(int argc, char** argv) {
    NpuClusterTestbench tb(argc, argv, true);
    tb.init_dram(0x80000000, 1024 * 1024);

    auto write_dram_8 = [&](uint32_t addr, uint8_t val) {
        uint32_t word_addr = addr & ~3;
        uint32_t offset = addr & 3;
        uint32_t word = tb.read_dram(word_addr);
        word &= ~(0xFF << (offset * 8));
        word |= (val << (offset * 8));
        tb.write_dram(word_addr, word);
    };

    auto read_dram_8 = [&](uint32_t addr) -> uint8_t {
        uint32_t word_addr = addr & ~3;
        uint32_t offset = addr & 3;
        uint32_t word = tb.read_dram(word_addr);
        return (word >> (offset * 8)) & 0xFF;
    };

    uint32_t M = 1;
    uint32_t K = 32;
    uint32_t N = 256;

    uint32_t act_addr = 0x80001000;
    uint32_t wgt_addr = 0x80002000;
    uint32_t out_addr = 0x80004000;

    // Initialize ACT: M=1, K=32. We set ACT[0] = 1, others 0.
    for (uint32_t k = 0; k < K; k++) {
        uint8_t val = (k == 0) ? 1 : 0;
        write_dram_8(act_addr + k, val);
    }

    // Initialize WGT: N=256, K=32. We set WGT[n*K + 0] = n - 128, others 0.
    for (uint32_t n = 0; n < N; n++) {
        for (uint32_t k = 0; k < K; k++) {
            int8_t val = (k == 0) ? (n - 128) : 0;
            write_dram_8(wgt_addr + n * K + k, (uint8_t)val);
        }
    }

    // Clear output
    for (uint32_t i = 0; i < M * N; i++) {
        write_dram_8(out_addr + i, 0);
    }

    uint32_t queue_addr = 0x80000000;
    uint32_t cmd_idx = 0;
    uint32_t cmd_base = 0x80000010;

    // Helper to write command
    auto write_cmd = [&](uint32_t idx, uint8_t opcode, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
        uint32_t addr = cmd_base + idx * 16;
        uint32_t word0 = opcode | (0 << 8) | (0 << 16);
        tb.write_dram(addr + 0, word0);
        tb.write_dram(addr + 4, arg0);
        tb.write_dram(addr + 8, arg1);
        tb.write_dram(addr + 12, arg2);
    };

    uint32_t tcdm_act = 0x10000000;
    uint32_t tcdm_wgt = 0x10000000 + K; // K = 32
    uint32_t tcdm_out = tcdm_wgt + (N * K); // 256 * 32 = 8192
    
    write_cmd(cmd_idx++, OP_DMA_READ, act_addr, tcdm_act, K);
    write_cmd(cmd_idx++, OP_DMA_READ, wgt_addr, tcdm_wgt, N * K);
    write_cmd(cmd_idx++, 0x34, M, K, N); // OP_CFG_MATMUL
    write_cmd(cmd_idx++, OP_COMPUTE_ACT_SILU, tcdm_act, tcdm_wgt, tcdm_out);
    write_cmd(cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(cmd_idx++, OP_DMA_WRITE, out_addr, tcdm_out, M * N);
    write_cmd(cmd_idx++, OP_FINISH, 0, 0, 0);

    setup_cmd_queue(tb, queue_addr, cmd_idx, 10);

    cout << "[TEST] Running SiLU Full LUT Coverage..." << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout!" << endl;
        return 1;
    }

    bool success = true;
    int mismatches = 0;
    for (uint32_t n = 0; n < N; n++) {
        int8_t actual = read_dram_8(out_addr + n);
        int8_t expected = lut_silu[n];
        if (actual != expected) {
            if (mismatches < 10) {
                cout << "[TEST] Mismatch at input " << (n - 128) << " | Expected: " << (int)expected << " Actual: " << (int)actual << endl;
            }
            mismatches++;
            success = false;
        }
    }

    if (success) {
        cout << "[TEST] Golden Match: PASSED! (Coverage: 256/256 LUT values verified)" << endl;
    } else {
        cout << "[TEST] Golden Match: FAILED! (" << mismatches << " mismatches)" << endl;
    }

    return success ? 0 : 1;
}
