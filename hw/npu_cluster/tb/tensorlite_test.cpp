// tensorlite_test.cpp - Phase 5 Architecture Verification Testbench
// Tests all TFLite operators via the RISC-V Control Core firmware
#include <iostream>
#include <fstream>
#include <cstring>
#include "Vnpu_cluster_top.h"
#include "verilated.h"

using namespace std;

#define I_SPM_BASE   0x00001000
#define I_SPM_SIZE   0x00008000
#define MBOX_BASE    0x40000000
#define DMA_BASE     0x70000000
#define NPU_BASE     0x60000000
#define TCDM_BASE    0x10000000

uint32_t ram[I_SPM_SIZE / 4];
uint32_t regs[32];
uint32_t pc = 0x1000;

Vnpu_cluster_top* dut;
vluint64_t main_time = 0;

// Performance metrics
uint64_t total_sim_cycles = 0;

void tick() {
    bool ar_valid_sampled = dut->axi_ar_valid_o;
    bool ar_ready_sampled = dut->axi_ar_ready_i;
    bool r_valid_sampled = dut->axi_r_valid_i;
    bool r_ready_sampled = dut->axi_r_ready_o;

    dut->clk_i = 1;
    dut->eval();
    main_time += 5;
    total_sim_cycles++;

    static int axi_burst_len = 0;

    if (r_valid_sampled && r_ready_sampled) {
        if (axi_burst_len > 0) axi_burst_len--;
    }

    if (ar_valid_sampled && ar_ready_sampled) {
        axi_burst_len = dut->axi_ar_len_o + 1;
    }

    if (axi_burst_len > 0) {
        dut->axi_r_valid_i = 1;
        dut->axi_r_data_i[0] = 0x01020304;
        dut->axi_r_data_i[1] = 0x05060708;
        dut->axi_r_data_i[2] = 0x090A0B0C;
        dut->axi_r_data_i[3] = 0x0D0E0F10;
        dut->axi_r_last_i = (axi_burst_len == 1);
        dut->axi_ar_ready_i = 0;
    } else {
        dut->axi_r_valid_i = 0;
        dut->axi_r_last_i = 0;
        dut->axi_ar_ready_i = 1;
    }

    dut->clk_i = 0;
    dut->eval();
    main_time += 5;
}

uint32_t mem_read(uint32_t addr) {
    if (addr >= I_SPM_BASE && addr < I_SPM_BASE + I_SPM_SIZE) {
        return ram[(addr - I_SPM_BASE) / 4];
    }
    dut->core_req_valid_i = 1;
    dut->core_req_write_i = 0;
    dut->core_req_addr_i = addr;
    int timeout = 0;
    while (!dut->core_req_ready_o) {
        tick();
        if (timeout++ > 1000) { cout << "HANG IN MEM_READ REQ_READY addr=" << hex << addr << endl; exit(1); }
    }
    tick();
    dut->core_req_valid_i = 0;
    timeout = 0;
    while (!dut->core_rsp_valid_o) {
        tick();
        if (timeout++ > 1000) { cout << "HANG IN MEM_READ RSP_VALID addr=" << hex << addr << endl; exit(1); }
    }
    uint32_t data = dut->core_rsp_data_o;
    tick();
    return data;
}

void mem_write(uint32_t addr, uint32_t data) {
    if (addr >= I_SPM_BASE && addr < I_SPM_BASE + I_SPM_SIZE) {
        ram[(addr - I_SPM_BASE) / 4] = data;
        return;
    }
    dut->core_req_valid_i = 1;
    dut->core_req_write_i = 1;
    dut->core_req_addr_i = addr;
    dut->core_req_data_i = data;
    int timeout = 0;
    while (!dut->core_req_ready_o) {
        tick();
        if (timeout++ > 1000) { cout << "HANG IN MEM_WRITE REQ_READY addr=" << hex << addr << endl; exit(1); }
    }
    tick();
    dut->core_req_valid_i = 0;
    
    // TCDM interconnect does not generate response for writes, only reads
    if (!(addr >= TCDM_BASE && addr < TCDM_BASE + 0x10000000)) {
        timeout = 0;
        while (!dut->core_rsp_valid_o) {
            tick();
            if (timeout++ > 1000) { cout << "HANG IN MEM_WRITE RSP_VALID addr=" << hex << addr << endl; exit(1); }
        }
    }
    tick();
}

void load_bin(const char* filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[ERROR] Cannot open " << filename << endl;
        return;
    }
    file.read((char*)ram, I_SPM_SIZE);
    cout << "[INFO] Loaded firmware: " << filename << endl;
}

// Test names for reporting
const char* test_names[] = {
    "CONV_2D",
    "FULLY_CONNECTED",
    "RELU",
    "RELU6",
    "LEAKY_RELU",
    "DEPTHWISE_CONV_2D",
    "AVERAGE_POOL_2D",
    "MAX_POOL_2D",
    "ADD",
    "SUB",
    "MUL",
    "SOFTMAX",
    "RESHAPE",
    "PAD"
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vnpu_cluster_top;

    // Reset
    dut->clk_i = 0;
    dut->rst_ni = 0;
    dut->host_rsp_ready_i = 1;
    dut->axi_ar_ready_i = 1;
    dut->axi_r_valid_i = 0;
    tick();
    tick();
    dut->rst_ni = 1;
    tick();

    load_bin("fw/tensorlite_ops.bin");

    cout << endl;
    cout << "==================================================" << endl;
    cout << " Phase 5: TensorLite Operator Verification" << endl;
    cout << "==================================================" << endl;
    cout << endl;

    for (int i = 0; i < 32; i++) regs[i] = 0;
    regs[2] = I_SPM_BASE + I_SPM_SIZE; // SP

    int inst_count = 0;
    bool wfi = false;
    bool done = false;

    while (!Verilated::gotFinish() && inst_count < 500000 && !done) {
        if (wfi) {
            // Host triggers mailbox task
            dut->host_req_valid_i = 1;
            dut->host_req_write_i = 1;
            dut->host_req_addr_i = MBOX_BASE + 0x04;
            dut->host_req_data_i = 1;
            while (!dut->host_req_ready_o) tick();
            tick();
            dut->host_req_valid_i = 0;
            while (!dut->host_rsp_valid_o) tick();
            tick();
            wfi = false;
        }

        uint32_t inst = mem_read(pc);
        uint32_t op = inst & 0x7F;
        uint32_t rd = (inst >> 7) & 0x1F;
        uint32_t rs1 = (inst >> 15) & 0x1F;
        uint32_t rs2 = (inst >> 20) & 0x1F;
        uint32_t funct3 = (inst >> 12) & 0x7;
        uint32_t funct7 = (inst >> 25) & 0x7F;

        uint32_t i_imm = inst >> 20;
        if (inst & 0x80000000) i_imm |= 0xFFFFF000;
        uint32_t u_imm = inst & 0xFFFFF000;
        uint32_t s_imm = ((inst >> 25) << 5) | ((inst >> 7) & 0x1F);
        if (inst & 0x80000000) s_imm |= 0xFFFFF800;
        uint32_t b_imm = ((inst >> 31) << 12) | (((inst >> 7) & 1) << 11) | (((inst >> 25) & 0x3F) << 5) | (((inst >> 8) & 0xF) << 1);
        if (inst & 0x80000000) b_imm |= 0xFFFFE000;
        uint32_t j_imm = ((inst >> 31) << 20) | (((inst >> 12) & 0xFF) << 12) | (((inst >> 20) & 1) << 11) | (((inst >> 21) & 0x3FF) << 1);
        if (inst & 0x80000000) j_imm |= 0xFFE00000;

        uint32_t next_pc = pc + 4;

        if (op == 0x37) { if (rd) regs[rd] = u_imm; }
        else if (op == 0x17) { if (rd) regs[rd] = pc + u_imm; }
        else if (op == 0x6F) { if (rd) regs[rd] = pc + 4; next_pc = pc + j_imm; }
        else if (op == 0x67) { if (rd) regs[rd] = pc + 4; next_pc = (regs[rs1] + i_imm) & ~1; }
        else if (op == 0x63) {
            bool taken = false;
            if (funct3 == 0) taken = (regs[rs1] == regs[rs2]);
            else if (funct3 == 1) taken = (regs[rs1] != regs[rs2]);
            else if (funct3 == 4) taken = ((int32_t)regs[rs1] < (int32_t)regs[rs2]);
            else if (funct3 == 5) taken = ((int32_t)regs[rs1] >= (int32_t)regs[rs2]);
            else if (funct3 == 6) taken = (regs[rs1] < regs[rs2]);
            else if (funct3 == 7) taken = (regs[rs1] >= regs[rs2]);
            if (taken) next_pc = pc + b_imm;
        }
        else if (op == 0x03) {
            uint32_t addr = regs[rs1] + i_imm;
            uint32_t val = mem_read(addr & ~3u);
            if (funct3 == 0) { // LB
                int shift = (addr & 3) * 8;
                int8_t byte_val = (int8_t)((val >> shift) & 0xFF);
                val = (uint32_t)(int32_t)byte_val;
            } else if (funct3 == 4) { // LBU
                int shift = (addr & 3) * 8;
                val = (val >> shift) & 0xFF;
            } else if (funct3 == 1) { // LH
                int shift = (addr & 2) * 8;
                int16_t hw_val = (int16_t)((val >> shift) & 0xFFFF);
                val = (uint32_t)(int32_t)hw_val;
            } else if (funct3 == 5) { // LHU
                int shift = (addr & 2) * 8;
                val = (val >> shift) & 0xFFFF;
            }
            // funct3 == 2 is LW (default, already loaded)
            if (rd) regs[rd] = val;
        }
        else if (op == 0x23) {
            uint32_t addr = regs[rs1] + s_imm;
            if (funct3 == 0) { // SB
                uint32_t aligned = addr & ~3u;
                uint32_t old = mem_read(aligned);
                int shift = (addr & 3) * 8;
                uint32_t mask = 0xFF << shift;
                uint32_t new_val = (old & ~mask) | ((regs[rs2] & 0xFF) << shift);
                mem_write(aligned, new_val);
            } else if (funct3 == 1) { // SH
                uint32_t aligned = addr & ~3u;
                uint32_t old = mem_read(aligned);
                int shift = (addr & 2) * 8;
                uint32_t mask = 0xFFFF << shift;
                uint32_t new_val = (old & ~mask) | ((regs[rs2] & 0xFFFF) << shift);
                mem_write(aligned, new_val);
            } else { // SW
                mem_write(addr, regs[rs2]);
            }

            // Check if firmware signals done
            if (addr == MBOX_BASE + 0x04 && regs[rs2] == 2) {
                done = true;
            }
        }
        else if (op == 0x13) {
            if (funct3 == 0) { if (rd) regs[rd] = regs[rs1] + i_imm; }
            else if (funct3 == 2) { if (rd) regs[rd] = ((int32_t)regs[rs1] < (int32_t)i_imm) ? 1 : 0; } // SLTI
            else if (funct3 == 3) { if (rd) regs[rd] = (regs[rs1] < i_imm) ? 1 : 0; } // SLTIU
            else if (funct3 == 4) { if (rd) regs[rd] = regs[rs1] ^ i_imm; } // XORI
            else if (funct3 == 6) { if (rd) regs[rd] = regs[rs1] | i_imm; } // ORI
            else if (funct3 == 7) { if (rd) regs[rd] = regs[rs1] & i_imm; } // ANDI
            else if (funct3 == 1) { if (rd) regs[rd] = regs[rs1] << (i_imm & 0x1F); } // SLLI
            else if (funct3 == 5) {
                if (funct7 == 0x20) { if (rd) regs[rd] = (uint32_t)((int32_t)regs[rs1] >> (i_imm & 0x1F)); } // SRAI
                else { if (rd) regs[rd] = regs[rs1] >> (i_imm & 0x1F); } // SRLI
            }
        }
        else if (op == 0x33) {
            if (funct7 == 1) { // M-extension
                if (funct3 == 0) { if (rd) regs[rd] = regs[rs1] * regs[rs2]; } // MUL
                else if (funct3 == 4) { // DIV
                    if (regs[rs2] != 0) { if (rd) regs[rd] = (uint32_t)((int32_t)regs[rs1] / (int32_t)regs[rs2]); }
                    else { if (rd) regs[rd] = 0xFFFFFFFF; }
                }
                else if (funct3 == 5) { // DIVU
                    if (regs[rs2] != 0) { if (rd) regs[rd] = regs[rs1] / regs[rs2]; }
                    else { if (rd) regs[rd] = 0xFFFFFFFF; }
                }
                else if (funct3 == 6) { // REM
                    if (regs[rs2] != 0) { if (rd) regs[rd] = (uint32_t)((int32_t)regs[rs1] % (int32_t)regs[rs2]); }
                    else { if (rd) regs[rd] = regs[rs1]; }
                }
            } else {
                if (funct3 == 0 && funct7 == 0) { if (rd) regs[rd] = regs[rs1] + regs[rs2]; } // ADD
                else if (funct3 == 0 && funct7 == 0x20) { if (rd) regs[rd] = regs[rs1] - regs[rs2]; } // SUB
                else if (funct3 == 1) { if (rd) regs[rd] = regs[rs1] << (regs[rs2] & 0x1F); } // SLL
                else if (funct3 == 2) { if (rd) regs[rd] = ((int32_t)regs[rs1] < (int32_t)regs[rs2]) ? 1 : 0; } // SLT
                else if (funct3 == 3) { if (rd) regs[rd] = (regs[rs1] < regs[rs2]) ? 1 : 0; } // SLTU
                else if (funct3 == 4) { if (rd) regs[rd] = regs[rs1] ^ regs[rs2]; } // XOR
                else if (funct3 == 5 && funct7 == 0) { if (rd) regs[rd] = regs[rs1] >> (regs[rs2] & 0x1F); } // SRL
                else if (funct3 == 5 && funct7 == 0x20) { if (rd) regs[rd] = (uint32_t)((int32_t)regs[rs1] >> (regs[rs2] & 0x1F)); } // SRA
                else if (funct3 == 6) { if (rd) regs[rd] = regs[rs1] | regs[rs2]; } // OR
                else if (funct3 == 7) { if (rd) regs[rd] = regs[rs1] & regs[rs2]; } // AND
            }
        }
        else if (op == 0x73) {
            if (i_imm == 0x105) wfi = true;
        }
        else if (inst == 0) {
            // NOP or end
        }

        pc = next_pc;
        regs[0] = 0; // x0 always zero
        inst_count++;
        
        if (inst_count > 499990) {
            cout << "PC Trace: 0x" << hex << pc << dec << endl;
        }
    }

    // ==========================================
    // Print Results
    // ==========================================
    cout << endl;
    cout << "--------------------------------------------------" << endl;
    cout << " TensorLite Operator Test Results" << endl;
    cout << "--------------------------------------------------" << endl;

    if (done) {
        cout << "[INFO] Firmware execution completed after " << inst_count << " instructions." << endl;
        cout << "[INFO] Total simulation cycles: " << total_sim_cycles << endl;
    } else {
        cout << "[FAIL] Firmware did not complete within timeout!" << endl;
    }

    // Read performance counters from Core 0
    uint32_t mac_active = mem_read(NPU_BASE + 0x18);
    uint32_t total_cyc = mem_read(NPU_BASE + 0x1C);
    uint32_t dma_rd = mem_read(DMA_BASE + 0x20);
    uint32_t dma_wr = mem_read(DMA_BASE + 0x24);

    cout << endl;
    cout << "=== Performance Metrics ===" << endl;
    if (total_cyc > 0) {
        double utilization = (double)mac_active * 100.0 / (double)total_cyc;
        cout << "MAC Utilization:        " << utilization << "%" << endl;
    }
    cout << "MAC Active Cycles:      " << mac_active << endl;
    cout << "Total HW Cycles:        " << total_cyc << endl;
    cout << "DMA Read Transactions:  " << dma_rd << endl;
    cout << "DMA Write Transactions: " << dma_wr << endl;
    cout << endl;

    cout << "==================================================" << endl;
    if (done) {
        cout << " Phase 5 TensorLite Verification COMPLETE" << endl;
    } else {
        cout << " Phase 5 TensorLite Verification TIMEOUT" << endl;
    }
    cout << "==================================================" << endl;

    delete dut;
    return done ? 0 : 1;
}
