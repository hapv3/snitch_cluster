#include "test_utils.h"

// Golden model for the Mock NPU Compute Core
// The mock core reads 32 bytes of activations and 128 bytes of weights
// and outputs exactly 4 bytes (1 byte per lane).
uint32_t compute_golden(uint32_t size, uint32_t act_addr, uint32_t wgt_addr, NpuClusterTestbench& tb) {
    uint32_t out = 0;
    for (int l = 0; l < 4; l++) {
        int32_t acc = 0;
        for (int i = 0; i < 32; i++) {
            // Act and Wgt are 8-bit signed
            int8_t a = 0;
            int8_t w = 0;
            
            if (i < size) {
                uint32_t a_word = tb.read_dram(act_addr + (i/4)*4);
                a = (int8_t)((a_word >> ((i%4)*8)) & 0xFF);
            }
            if ((l*32 + i) < size) {
                uint32_t w_word = tb.read_dram(wgt_addr + ((l*32 + i)/4)*4);
                w = (int8_t)((w_word >> (((l*32 + i)%4)*8)) & 0xFF);
            }
            acc += (int32_t)a * (int32_t)w;
        }
        // Requantization (scale=1, shift=0, zero_point=0)
        int32_t offset = acc;
        int16_t clipped = offset;
        if (offset > 32767) clipped = 32767;
        else if (offset < -32768) clipped = -32768;
        
        // Act_none
        int8_t act_out;
        if (clipped > 127) act_out = 127;
        else if (clipped < -128) act_out = -128;
        else act_out = (int8_t)clipped;
        
        out |= ((uint32_t)(uint8_t)act_out) << (l * 8);
    }
    return out;
}

int main(int argc, char** argv) {
    NpuClusterTestbench tb(argc, argv, true);
    // Increase DRAM size to 8MB to support 1024 tests without overlaps
    tb.init_dram(0x80000000, 8 * 1024 * 1024);

    cout << "[TEST] Setting up tests for sizes 1 to 1024..." << endl;
    
    int num_tests = 1024;
    uint32_t queue_addr = 0x80000000;
    // Capacity needs to hold: 5 commands per test * 1024 + FINISH
    int capacity = num_tests * 5 + 10;
    uint32_t cmd_base = setup_cmd_queue(tb, queue_addr, num_tests * 5 + 1, capacity);
    
    int cmd_idx = 0;
    for (int size = 1; size <= 1024; size++) {
        // Space tests by 4KB to avoid overlap (max size is 1024 bytes)
        uint32_t act_addr = 0x80100000 + (size - 1) * 1024;
        uint32_t wgt_addr = 0x80200000 + (size - 1) * 1024;
        uint32_t out_addr = 0x80300000 + (size - 1) * 1024;
        
        // Write data
        int words = (size + 3) / 4;
        for (int j = 0; j < words; j++) {
            tb.write_dram(act_addr + j*4, j + 1);
            tb.write_dram(wgt_addr + j*4, j + 100);
            tb.write_dram(out_addr + j*4, 0); // Clear Output
        }

        // TCDM address spacing (avoid overlapping in TCDM too)
        // Let's use same address for each test since they run sequentially
        uint32_t tcdm_act = 0x10000000;
        uint32_t tcdm_wgt = 0x10004000;
        uint32_t tcdm_out = 0x10008000;

        // Write commands
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, act_addr, tcdm_act, size);
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, wgt_addr, tcdm_wgt, size);
        write_cmd(tb, cmd_base, cmd_idx++, OP_COMPUTE_CONV2D, tcdm_act, tcdm_wgt, tcdm_out);
        write_cmd(tb, cmd_base, cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_WRITE, out_addr, tcdm_out, size);
    }
    
    // Final command
    write_cmd(tb, cmd_base, cmd_idx++, OP_FINISH, 0, 0, 0);

    cout << "[TEST] Running Conv2D Sizes 1 to 1024..." << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout!" << endl;
        return 1;
    }

    // Verify all 1024 outputs against Golden Model
    for (int size = 1; size <= 1024; size++) {
        uint32_t act_addr = 0x80100000 + (size - 1) * 1024;
        uint32_t wgt_addr = 0x80200000 + (size - 1) * 1024;
        uint32_t out_addr = 0x80300000 + (size - 1) * 1024;
        
        uint32_t expected_word = compute_golden(size, act_addr, wgt_addr, tb);
        uint32_t actual_word = tb.read_dram(out_addr);
        
        if (actual_word != expected_word) {
            cout << "[TEST] FAILED: Golden Match Error for Size " << size << "!" << endl;
            cout << "       Expected: 0x" << hex << expected_word << " | Actual: 0x" << actual_word << dec << endl;
            return 1;
        }
        
        // The mock RTL only outputs 1 word. If size > 4 bytes, the remaining output words in DRAM should be untouched (0).
        int words = (size + 3) / 4;
        for (int j = 1; j < words; j++) {
            if (tb.read_dram(out_addr + j*4) != 0) {
                cout << "[TEST] FAILED: Unexpected non-zero data at offset " << j*4 << " for Size " << size << endl;
                return 1;
            }
        }
    }

    cout << "[TEST] Conv2D All Sizes PASSED!" << endl;
    return 0;
}
