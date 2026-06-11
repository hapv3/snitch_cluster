#include "test_utils.h"
#include "../../../../sw/npu_driver/npu_mmio.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <algorithm>

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

    uint32_t act_addr = 0x80001000;
    uint32_t out_addr_2x2 = 0x80003000;
    uint32_t out_addr_3x3 = 0x80003010;
    uint32_t out_addr_5x5 = 0x80003020;

    // 8x8 Image for Testing YOLOv8 configs
    uint8_t input_img[64];
    for (int i=0; i<64; i++) input_img[i] = i + 1;
    
    for (int i=0; i<64; i++) {
        write_dram_8(act_addr + i, input_img[i]);
    }
    for (int i=0; i<16; i++) write_dram_8(out_addr_2x2 + i, 0);
    for (int i=0; i<16; i++) write_dram_8(out_addr_3x3 + i, 0);
    for (int i=0; i<16; i++) write_dram_8(out_addr_5x5 + i, 0);

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
    uint32_t tcdm_out = 0x10000100;
    
    // Read the 8x8 image into TCDM
    write_cmd(cmd_idx++, OP_DMA_READ, act_addr, tcdm_act, 64);

    // Test 1: 2x2 Pooling, Stride 2 (Standard Downsample)
    uint32_t arg1_2x2 = 8 | (8 << 8) | (2 << 16) | (2 << 24); // height=8, width=8, kernel=2, stride=2
    write_cmd(cmd_idx++, OP_COMPUTE_MAXPOOL, tcdm_act, arg1_2x2, tcdm_out);
    write_cmd(cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(cmd_idx++, OP_DMA_WRITE, out_addr_2x2, tcdm_out, 16); // 4x4 out = 16 bytes

    // Test 2: 3x3 Pooling, Stride 2
    uint32_t arg1_3x3 = 8 | (8 << 8) | (3 << 16) | (2 << 24); // height=8, width=8, kernel=3, stride=2
    write_cmd(cmd_idx++, OP_COMPUTE_MAXPOOL, tcdm_act, arg1_3x3, tcdm_out);
    write_cmd(cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(cmd_idx++, OP_DMA_WRITE, out_addr_3x3, tcdm_out, 9); // 3x3 out = 9 bytes

    // Test 3: 5x5 Pooling, Stride 1 (YOLOv8 SPPF config)
    uint32_t arg1_5x5 = 8 | (8 << 8) | (5 << 16) | (1 << 24); // height=8, width=8, kernel=5, stride=1
    write_cmd(cmd_idx++, OP_COMPUTE_MAXPOOL, tcdm_act, arg1_5x5, tcdm_out);
    write_cmd(cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(cmd_idx++, OP_DMA_WRITE, out_addr_5x5, tcdm_out, 16); // 4x4 out = 16 bytes

    write_cmd(cmd_idx++, OP_FINISH, 0, 0, 0);

    setup_cmd_queue(tb, queue_addr, cmd_idx, 10);

    cout << "[TEST] Running MaxPool Verification..." << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) return 1;

    bool success = true;

    auto verify = [&](string name, uint32_t out_addr, int out_h, int out_w, int k, int s, int in_w) {
        for (int i=0; i<out_h; i++) {
            for (int j=0; j<out_w; j++) {
                int start_y = i * s;
                int start_x = j * s;
                uint8_t expected = 0;
                for (int y=0; y<k; y++) {
                    for (int x=0; x<k; x++) {
                        expected = max(expected, input_img[(start_y + y) * in_w + (start_x + x)]);
                    }
                }
                uint8_t actual = read_dram_8(out_addr + i * out_w + j);
                if (actual != expected) {
                    cout << "[TEST] " << name << " Mismatch at (" << i << "," << j << ")! Expected: " << (int)expected << " Actual: " << (int)actual << endl;
                    success = false;
                }
            }
        }
    };

    verify("2x2 Stride 2", out_addr_2x2, 4, 4, 2, 2, 8);
    verify("3x3 Stride 2", out_addr_3x3, 3, 3, 3, 2, 8);
    verify("5x5 Stride 1 (YOLOv8)", out_addr_5x5, 4, 4, 5, 1, 8);

    if (success) {
        cout << "[TEST] Golden Match: PASSED!" << endl;
    } else {
        cout << "[TEST] Golden Match: FAILED!" << endl;
    }
    return success ? 0 : 1;
}
