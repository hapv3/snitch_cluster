// Copyright 2026 NPU IP
// Verilator Testbench for NPU Control Core (with mini RISC-V ISS)

#include "Vnpu_control_core.h"
#include "verilated.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <iomanip>

using namespace std;

// Memory mappings
#define I_SPM_BASE   0x00001000
#define I_SPM_SIZE   0x00008000
#define MBOX_BASE    0x40000000
#define DMA_BASE     0x50000000
#define NPU_BASE     0x60000000

uint32_t ram[I_SPM_SIZE / 4];
uint32_t regs[32];
uint32_t pc = 0x1000;

Vnpu_control_core* dut;
vluint64_t main_time = 0;

uint32_t tcdm_data_rsp = 0;
bool tcdm_rsp_pending = false;

void tick() {
    dut->clk_i = 1;
    dut->eval();
    main_time += 5;
    dut->clk_i = 0;
    
    // Simulate TCDM/DMA responses
    if (dut->tcdm_req_valid_o) {
        dut->tcdm_req_ready_i = 1;
        if (!dut->tcdm_req_write_o) {
            tcdm_rsp_pending = true;
            if (dut->tcdm_req_addr_o == 0x50000010) { // DMA STATUS
                tcdm_data_rsp = 0; // DMA Idle
            } else if (dut->tcdm_req_addr_o == 0x60000000) { // NPU CTRL
                tcdm_data_rsp = 0; // FREP Idle
            } else {
                tcdm_data_rsp = 0xDEADBEEF;
            }
        } else {
            tcdm_rsp_pending = true;
            tcdm_data_rsp = 0; // Ignore write data in simple simulation
        }
    } else {
        dut->tcdm_req_ready_i = 0;
    }
    
    if (tcdm_rsp_pending) {
        dut->tcdm_rsp_valid_i = 1;
        dut->tcdm_rsp_data_i = tcdm_data_rsp;
        tcdm_rsp_pending = false;
    } else {
        dut->tcdm_rsp_valid_i = 0;
    }

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
    while (!dut->core_req_ready_o) tick();
    tick();
    dut->core_req_valid_i = 0;
    while (!dut->core_rsp_valid_o) tick();
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
    while (!dut->core_req_ready_o) tick();
    tick();
    dut->core_req_valid_i = 0;
    while (!dut->core_rsp_valid_o) tick();
    tick();
}

void load_bin(const char* filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[ERROR] Cannot open " << filename << endl;
        return;
    }
    file.read((char*)ram, I_SPM_SIZE);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vnpu_control_core;

    // Reset
    dut->clk_i = 0;
    dut->rst_ni = 0;
    dut->host_rsp_ready_i = 1;
    dut->fw_rsp_ready_i = 1;
    tick(); tick();
    dut->rst_ni = 1;
    tick();

    load_bin("fw/firmware.bin");

    cout << "[INFO] Started RISC-V Firmware Execution" << endl;
    for (int i = 0; i < 32; i++) regs[i] = 0;
    regs[2] = I_SPM_BASE + I_SPM_SIZE; // SP

    int inst_count = 0;
    bool wfi = false;

    // Simulate ARM Host interacting with the Mailbox
    uint32_t host_task[] = {
        0x1234, // OP
        0x20000000, // ACT PTR
        0x30000000, // WGT PTR
        0x40000000, // OUT PTR
        32, // M
        32, // N
        32  // K
    };

    while (!Verilated::gotFinish() && inst_count < 1000) {
        if (wfi) {
            // Trigger ARM Host interaction to wake up
            cout << "[INFO] Snitch in WFI. ARM Host triggering Mailbox task..." << endl;
            for (int i = 0; i < 7; i++) {
                dut->host_req_valid_i = 1;
                dut->host_req_write_i = 1;
                dut->host_req_addr_i = MBOX_BASE + 0x08 + i * 4;
                dut->host_req_data_i = host_task[i];
                while (!dut->host_req_ready_o) tick();
                tick();
                dut->host_req_valid_i = 0;
                while (!dut->host_rsp_valid_o) tick();
                tick();
            }
            // Trigger start
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

        if (op == 0x37) { // LUI
            if (rd) regs[rd] = u_imm;
        } else if (op == 0x17) { // AUIPC
            if (rd) regs[rd] = pc + u_imm;
        } else if (op == 0x6F) { // JAL
            if (rd) regs[rd] = pc + 4;
            next_pc = pc + j_imm;
        } else if (op == 0x67) { // JALR
            if (rd) regs[rd] = pc + 4;
            next_pc = (regs[rs1] + i_imm) & ~1;
        } else if (op == 0x63) { // BRANCH
            if (funct3 == 0) { // BEQ
                if (regs[rs1] == regs[rs2]) next_pc = pc + b_imm;
            } else if (funct3 == 1) { // BNE
                if (regs[rs1] != regs[rs2]) next_pc = pc + b_imm;
            }
        } else if (op == 0x03) { // LOAD
            uint32_t addr = regs[rs1] + i_imm;
            uint32_t val = mem_read(addr);
            if (rd) regs[rd] = val;
            // cout << "LW r" << rd << " = mem[" << hex << addr << "] (" << val << ")" << endl;
        } else if (op == 0x23) { // STORE
            uint32_t addr = regs[rs1] + s_imm;
            mem_write(addr, regs[rs2]);
            // cout << "SW mem[" << hex << addr << "] = r" << rs2 << " (" << regs[rs2] << ")" << endl;
            if (addr == MBOX_BASE + 0x04 && regs[rs2] == 2) {
                cout << "[INFO] Firmware signaled Task Complete!" << endl;
                break;
            }
        } else if (op == 0x13) { // OP-IMM
            if (funct3 == 0) { // ADDI
                if (rd) regs[rd] = regs[rs1] + i_imm;
            } else if (funct3 == 7) { // ANDI
                if (rd) regs[rd] = regs[rs1] & i_imm;
            } else if (funct3 == 1) { // SLLI
                if (rd) regs[rd] = regs[rs1] << (i_imm & 0x1F);
            }
        } else if (op == 0x33) { // OP (including M extension)
            if (funct7 == 1 && funct3 == 0) { // MUL
                if (rd) regs[rd] = regs[rs1] * regs[rs2];
            }
        } else if (op == 0x73) { // SYSTEM
            if (i_imm == 0x105) { // WFI
                wfi = true;
            }
        } else {
            cout << "[ERROR] Unknown instruction " << hex << inst << " at PC " << pc << endl;
            break;
        }

        pc = next_pc;
        inst_count++;
    }

    cout << "[INFO] Simulation completed after " << inst_count << " instructions." << endl;
    delete dut;
    return 0;
}
