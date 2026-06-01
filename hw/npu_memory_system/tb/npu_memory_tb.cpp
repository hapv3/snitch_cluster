// Copyright 2026 NPU IP
// C++ Verilator Testbench for NPU Memory Subsystem

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include "Vnpu_memory_subsystem.h"
#include "verilated.h"

using namespace std;

Vnpu_memory_subsystem* dut;
vluint64_t main_time = 0;

int bytes_transferred = 0;

void tick() {
    // Sample ready signals before clock edge to know what was accepted
    bool r_ready_sampled = dut->axi_r_ready_o;
    bool ar_ready_sampled = dut->axi_ar_ready_i;
    bool ar_valid_sampled = dut->axi_ar_valid_o;
    bool r_valid_sampled = dut->axi_r_valid_i;
    
    dut->clk_i = 1;
    dut->eval();
    main_time += 5;
    
    // Simulate AXI Responses on clock high
    if (ar_valid_sampled && ar_ready_sampled && !dut->axi_r_valid_i) {
        // Provide fake read data on next cycles
        dut->axi_r_valid_i = 1;
        dut->axi_r_data_i[0] = dut->axi_ar_addr_o;
        dut->axi_r_data_i[1] = 0;
        dut->axi_r_data_i[2] = 0;
        dut->axi_r_data_i[3] = 0;
        dut->axi_r_last_i = 1; // Single beat for now
    }
    
    // Clear R valid if accepted
    if (r_valid_sampled && r_ready_sampled) {
        dut->axi_r_valid_i = 0;
        bytes_transferred += 4;
    }
    
    dut->clk_i = 0;
    dut->eval();
    main_time += 5;
}

void reset() {
    dut->rst_ni = 0;
    tick();
    tick();
    dut->rst_ni = 1;
    tick();
}

void dma_write_reg(uint32_t offset, uint32_t val) {
    dut->dma_ctrl_req_valid_i = 1;
    dut->dma_ctrl_req_write_i = 1;
    dut->dma_ctrl_req_addr_i = offset;
    dut->dma_ctrl_req_data_i = val;
    tick(); // Submit request
    dut->dma_ctrl_req_valid_i = 0;
    tick(); // Wait for response
}

uint32_t dma_read_reg(uint32_t offset) {
    dut->dma_ctrl_req_valid_i = 1;
    dut->dma_ctrl_req_write_i = 0;
    dut->dma_ctrl_req_addr_i = offset;
    tick(); // Submit request
    dut->dma_ctrl_req_valid_i = 0;
    tick(); // Wait for response
    return dut->dma_ctrl_rsp_data_o;
}

uint32_t tcdm_read(uint32_t addr) {
    dut->ext_req_valid_i = 1; // Master 1
    dut->ext_req_write_i = 0;
    dut->ext_req_addr_i[0] = addr;
    dut->ext_req_be_i = 0xF;
    tick(); // Submit request
    dut->ext_req_valid_i = 0;
    tick(); // Response arrives 1 cycle later
    return dut->ext_rsp_rdata_o[0];
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vnpu_memory_subsystem;
    
    // Init inputs
    dut->dma_ctrl_req_valid_i = 0;
    dut->ext_req_valid_i = 0;
    dut->axi_ar_ready_i = 1; // Always accept AR
    dut->axi_r_valid_i = 0;
    
    reset();
    
    cout << "[INFO] Starting Memory Subsystem Simulation..." << endl;
    
    // 1. Program DMA for 2D transfer
    uint32_t src_base = 0x80000000;
    uint32_t dst_base = 0x10000000;
    uint32_t dim_x = 16; // 16 bytes per line
    uint32_t dim_y = 4;  // 4 lines
    uint32_t stride_src = 32; // Skip 16 bytes every line in source
    uint32_t stride_dst = 16; // Contiguous in destination
    
    dma_write_reg(0x00, src_base);
    dma_write_reg(0x04, dst_base);
    dma_write_reg(0x08, dim_x);
    dma_write_reg(0x0C, dim_y);
    dma_write_reg(0x10, stride_src);
    dma_write_reg(0x14, stride_dst);
    
    cout << "[INFO] Triggering DMA Engine..." << endl;
    dma_write_reg(0x18, 1); // Trigger
    
    // 2. Wait for DMA
    int total_bytes = dim_x * dim_y;
    bool dma_busy = true;
    while(dma_busy || bytes_transferred < total_bytes) {
        if ((main_time % 100) == 0) {
            uint32_t status = dma_read_reg(0x1C);
            dma_busy = (status & 1);
        } else {
            tick();
        }
        
        if (main_time > 100000) {
            cout << "[ERROR] Simulation timeout!" << endl;
            break;
        }
    }
    
    cout << "[INFO] DMA Transfer Completed! Transferred " << bytes_transferred << " bytes." << endl;
    
    // 3. Verify TCDM contents
    cout << "[INFO] Verifying TCDM Contents via Crossbar Interconnect..." << endl;
    int errors = 0;
    
    // We wrote (dim_x * dim_y) bytes. Let's read them out.
    // The data written should be the source AXI address.
    uint32_t curr_dst = dst_base;
    uint32_t curr_src = src_base;
    
    for (int y = 0; y < dim_y; y++) {
        for (int x = 0; x < dim_x; x += 4) {
            uint32_t val = tcdm_read(curr_dst);
            if (val != curr_src) {
                cout << "[ERROR] Mismatch at TCDM 0x" << hex << curr_dst 
                     << " Expected: 0x" << curr_src << " Got: 0x" << val << dec << endl;
                errors++;
            }
            curr_dst += 4;
            curr_src += 4;
        }
        // Apply strides at end of line
        curr_src = curr_src - dim_x + stride_src;
        curr_dst = curr_dst - dim_x + stride_dst;
    }
    
    if (errors == 0) {
        cout << "[SUCCESS] TCDM Interconnect and DMA 2D Striding verified successfully!" << endl;
    } else {
        cout << "[FAILED] Found " << errors << " errors." << endl;
    }
    
    delete dut;
    return 0;
}
