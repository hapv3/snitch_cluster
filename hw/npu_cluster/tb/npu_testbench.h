#ifndef NPU_TESTBENCH_H
#define NPU_TESTBENCH_H

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "Vnpu_cluster_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

class NpuClusterTestbench {
public:
    Vnpu_cluster_top* dut;
    VerilatedVcdC* tfp;
    vluint64_t main_time;
    int max_sim_time;
    bool trace_enabled;

    NpuClusterTestbench(int argc, char** argv, bool trace = true);
    ~NpuClusterTestbench();

    void tick();
    void reset();
    
    // AXI-Lite interactions
    void axi_lite_write(uint32_t addr, uint32_t data);
    uint32_t axi_lite_read(uint32_t addr);

    // High level helpers
    bool load_firmware(const char* filepath);
    void start_task(const std::vector<uint32_t>& task_data);
    bool wait_for_interrupt(int max_cycles);
    
    // Simulated DRAM Memory
    std::vector<uint32_t> dram_memory;
    uint32_t dram_base_addr;
    
    // Setup DRAM for AXI Master simulation
    void init_dram(uint32_t base_addr, size_t size_bytes);
    void write_dram(uint32_t addr, uint32_t data);
    uint32_t read_dram(uint32_t addr);
};

#endif
