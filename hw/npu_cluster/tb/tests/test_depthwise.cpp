#include "test_utils.h"
#include "../../../../sw/npu_driver/npu_mmio.h"
#include <iostream>
#include <vector>
#include <cassert>

using namespace std;

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
    uint32_t N = 4;

    uint32_t act_addr = 0x80001000;
    uint32_t wgt_addr = 0x80002000;
    uint32_t out_addr = 0x80003000;

    for (uint32_t k = 0; k < K; k++) write_dram_8(act_addr + k, k + 1);
    for (uint32_t n = 0; n < N; n++) {
        for (uint32_t k = 0; k < K; k++) write_dram_8(wgt_addr + n * K + k, 1);
    }
    for (uint32_t i = 0; i < M * N; i++) write_dram_8(out_addr + i, 0);

    uint32_t queue_addr = 0x80000000;
    uint32_t cmd_idx = 0;
    uint32_t cmd_base = 0x80000010;

    auto write_cmd = [&](uint32_t idx, uint8_t opcode, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
        uint32_t addr = cmd_base + idx * 16;
        tb.write_dram(addr + 0, opcode | (0 << 8) | (0 << 16));
        tb.write_dram(addr + 4, arg0);
        tb.write_dram(addr + 8, arg1);
        tb.write_dram(addr + 12, arg2);
    };

    uint32_t tcdm_act = 0x10000000;
    uint32_t tcdm_wgt = 0x10000100;
    uint32_t tcdm_out = 0x10000200;
    
    write_cmd(cmd_idx++, OP_DMA_READ, act_addr, tcdm_act, K);
    write_cmd(cmd_idx++, OP_DMA_READ, wgt_addr, tcdm_wgt, N * K);
    write_cmd(cmd_idx++, 0x34, M, K, N);
    write_cmd(cmd_idx++, OP_COMPUTE_CONV2D, tcdm_act, tcdm_wgt, tcdm_out);
    write_cmd(cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(cmd_idx++, OP_DMA_WRITE, out_addr, tcdm_out, M * N);
    write_cmd(cmd_idx++, OP_FINISH, 0, 0, 0);

    setup_cmd_queue(tb, queue_addr, cmd_idx, 10);

    cout << "[TEST] Running Depthwise..." << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) return 1;

    bool success = true;
    for (uint32_t n = 0; n < N; n++) {
        int8_t actual = read_dram_8(out_addr + n);
        int32_t expected = 0;
        for (uint32_t k = 0; k < K; k++) expected += (k + 1) * 1;
        int8_t expected_clipped = expected > 127 ? 127 : (expected < -128 ? -128 : expected);
        if (actual != expected_clipped) {
            cout << "[TEST] Mismatch at N=" << n << " | Expected: " << (int)expected_clipped << " Actual: " << (int)actual << endl;
            success = false;
        }
    }

    if (success) cout << "[TEST] Golden Match: PASSED!" << endl;
    else cout << "[TEST] Golden Match: FAILED!" << endl;
    return success ? 0 : 1;
}
