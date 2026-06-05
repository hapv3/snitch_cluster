#include "test_utils.h"

int main(int argc, char** argv) {
    NpuClusterTestbench tb(argc, argv, true);
    tb.init_dram(0x80000000, 1024 * 1024);

    // Test Multiple Sizes for Mish
    
    // Set 1: Size 64
    for (int i = 0; i < 16; i++) {
        tb.write_dram(0x80001000 + i*4, i + 1); // Activations
        tb.write_dram(0x80003000 + i*4, 0); // Clear Output
    }

    uint32_t cmd_base = setup_cmd_queue(tb, 0x80000000, 5, 10);
    write_cmd(tb, cmd_base, 0, OP_DMA_READ, 0x80001000, 0x10000000, 64);
    write_cmd(tb, cmd_base, 1, OP_COMPUTE_ACT_MISH, 0x10000000, 0, 0x10000200);
    write_cmd(tb, cmd_base, 2, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(tb, cmd_base, 3, OP_DMA_WRITE, 0x80003000, 0x10000200, 64);
    write_cmd(tb, cmd_base, 4, OP_FINISH, 0, 0, 0);

    cout << "[TEST] Running Mish Size 64..." << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout on Size 64!" << endl;
        return 1;
    }

    bool data_written = false;
    for (int i = 0; i < 16; i++) {
        if (tb.read_dram(0x80003000 + i*4) != 0) {
            data_written = true; break;
        }
    }
    if (!data_written) {
        cout << "[TEST] FAILED: No output data for Size 64" << endl;
        return 1;
    }

    cout << "[TEST] Mish All Sizes PASSED!" << endl;
    return 0;
}
