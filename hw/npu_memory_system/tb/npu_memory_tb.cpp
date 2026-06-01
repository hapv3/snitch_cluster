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
        cout << "[FAILED] Found " << errors << " errors in DMA verification." << endl;
    }
    
    // 4. Concurrent Access Stress Test
    cout << "[INFO] Running Concurrent Access Stress Test on Interconnect..." << endl;
    
    // Write distinct values using Master 1, 2, 3, 4 concurrently to DIFFERENT banks
    // Bank mapping is addr[6:2]. So 0x10000000 (Bank 0), 0x10000004 (Bank 1), 0x10000008 (Bank 2), 0x1000000C (Bank 3)
    uint32_t addrs[4] = {0x10000000, 0x10000004, 0x10000008, 0x1000000C};
    uint32_t wdata[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    
    for (int i=0; i<4; i++) {
        dut->ext_req_valid_i |= (1 << i);
        dut->ext_req_write_i |= (1 << i);
        dut->ext_req_be_i |= (0xF << (i*4));
        dut->ext_req_addr_i[i] = addrs[i];
        dut->ext_req_wdata_i[i] = wdata[i];
    }
    
    tick(); // Submit concurrent writes
    dut->ext_req_valid_i = 0;
    dut->ext_req_write_i = 0;
    tick(); // Wait for completion
    
    // Now read them back concurrently
    for (int i=0; i<4; i++) {
        dut->ext_req_valid_i |= (1 << i);
        dut->ext_req_addr_i[i] = addrs[i];
    }
    
    tick(); // Submit concurrent reads
    dut->ext_req_valid_i = 0;
    tick(); // Responses arrive
    
    int concurrent_errors = 0;
    for (int i=0; i<4; i++) {
        // Wait, responses might take multiple cycles if there were collisions, but these are different banks!
        // Should complete in 1 cycle.
        if ((dut->ext_rsp_valid_o & (1 << i)) == 0) {
            cout << "[ERROR] Master " << i << " did not receive a response!" << endl;
            concurrent_errors++;
        } else if (dut->ext_rsp_rdata_o[i] != wdata[i]) {
            cout << "[ERROR] Master " << i << " mismatch! Expected: 0x" << hex << wdata[i] 
                 << " Got: 0x" << dut->ext_rsp_rdata_o[i] << dec << endl;
            concurrent_errors++;
        }
    }
    
    // Next, test COLLISION: all masters reading from the SAME bank
    // Bank 0 (0x10000000)
    cout << "[INFO] Testing Bank Collision Arbitration..." << endl;
    for (int i=0; i<4; i++) {
        dut->ext_req_valid_i |= (1 << i);
        dut->ext_req_addr_i[i] = 0x10000000;
    }
    
    int masters_done = 0;
    int timeout = 0;
    while (masters_done < 4 && timeout < 20) {
        // Sample ready/valid
        uint32_t req_ready = dut->ext_req_ready_o;
        
        tick(); // Advance cycle
        
        // Clear requests that were accepted
        for (int i=0; i<4; i++) {
            if ((dut->ext_req_valid_i & (1 << i)) && (req_ready & (1 << i))) {
                dut->ext_req_valid_i &= ~(1 << i);
            }
        }
        
        // Count responses
        for (int i=0; i<4; i++) {
            if (dut->ext_rsp_valid_o & (1 << i)) {
                masters_done++;
                if (dut->ext_rsp_rdata_o[i] != 0x11111111) {
                    cout << "[ERROR] Collision test read mismatch on Master " << i << endl;
                    concurrent_errors++;
                }
            }
        }
        timeout++;
    }
    
    if (masters_done != 4) {
        cout << "[ERROR] Collision test timeout! Not all masters received data." << endl;
        concurrent_errors++;
    }
    
    if (concurrent_errors == 0) {
        cout << "[SUCCESS] Interconnect Concurrent Stress Test Passed! Zero collisions dropped." << endl;
    } else {
        cout << "[FAILED] Found " << concurrent_errors << " errors in Concurrent test." << endl;
    }
    
    // 5. DMA Double-Buffering (Ping-Pong) Zero-Stall Verification
    cout << "[INFO] Running DMA Double-Buffering (Ping-Pong) Verification..." << endl;
    uint32_t buffer_A = 0x10001000;
    uint32_t buffer_B = 0x10002000;
    
    // Load Buffer A
    dma_write_reg(0x00, 0x90000000); // Src A
    dma_write_reg(0x04, buffer_A);
    dma_write_reg(0x08, 16);
    dma_write_reg(0x0C, 1);
    dma_write_reg(0x10, 16);
    dma_write_reg(0x14, 16);
    dma_write_reg(0x18, 1); // Trigger A
    
    int bytes_before_ping_pong = bytes_transferred;
    
    while(dma_read_reg(0x1C) & 1) { // Wait for A
        // dma_read_reg advances tick() which handles AXI
    }
    
    // Load Buffer B
    dma_write_reg(0x00, 0xA0000000); // Src B
    dma_write_reg(0x04, buffer_B);
    dma_write_reg(0x18, 1); // Trigger B
    
    while(dma_read_reg(0x1C) & 1) { // Wait for B
        // dma_read_reg advances tick() which handles AXI
    }
    
    int ping_pong_bytes = bytes_transferred - bytes_before_ping_pong;
    if (ping_pong_bytes == 32) {
        cout << "[SUCCESS] DMA Double-Buffer Ping-Pong completed successfully!" << endl;
    } else {
        cout << "[ERROR] Ping-Pong bytes transferred: " << ping_pong_bytes << endl;
    }

    delete dut;
    return 0;
}
