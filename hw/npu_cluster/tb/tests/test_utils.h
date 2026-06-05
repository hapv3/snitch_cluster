#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "../npu_testbench.h"
#include "../../../../sw/npu_driver/npu_mmio.h"
#include <iostream>
#include <vector>
#include <cassert>

using namespace std;

// Helper to write command
inline void write_cmd(NpuClusterTestbench& tb, uint32_t cmd_base, int idx, uint8_t opcode, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    uint32_t addr = cmd_base + idx * 16;
    uint32_t word0 = opcode | (0 << 8) | (0 << 16); // opcode, flags, cluster_id
    tb.write_dram(addr + 0, word0);
    tb.write_dram(addr + 4, arg0);
    tb.write_dram(addr + 8, arg1);
    tb.write_dram(addr + 12, arg2);
}

// Helper to setup command queue header
inline uint32_t setup_cmd_queue(NpuClusterTestbench& tb, uint32_t queue_addr, int tail, int capacity) {
    tb.write_dram(queue_addr + 0, 0); // head
    tb.write_dram(queue_addr + 4, tail); // tail
    tb.write_dram(queue_addr + 8, capacity); // capacity
    tb.write_dram(queue_addr + 12, 0); // flags
    return queue_addr + 16; // Return cmd_base
}

// Helper to load firmware and run
inline bool run_firmware(NpuClusterTestbench& tb, const char* fw_path) {
    // Reset first to put hardware in clean state (DUT outputs known)
    tb.reset();

    // Load firmware into I-SPM while in reset-released state
    if (!tb.load_firmware(fw_path)) {
        return false;
    }

    // Reset again so Snitch starts cleanly from PC=0x1000 with firmware loaded
    tb.reset();

    // Give firmware time to boot: run through _start (setup stack, zero .bss) and reach wfi
    for (int i = 0; i < 5000; i++) {
        tb.tick();
    }

    // Ring doorbell (Host sends trigger to Mailbox)
    tb.axi_lite_write(0x40000004, 1);

    // Wait for completion (allow up to 500 million cycles for 256x256 matmul)
    bool success = tb.wait_for_interrupt(500000000);
    return success;
}

#endif // TEST_UTILS_H
