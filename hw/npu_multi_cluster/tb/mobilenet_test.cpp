// NPU Multi-Cluster Top Level Verilator Testbench (with RISC-V ISS)
#include <iostream>
#include <fstream>
#include <vector>
#include "Vnpu_multi_cluster_top.h"
#include "verilated.h"

using namespace std;

#define I_SPM_BASE   0x00001000
#define I_SPM_SIZE   0x00008000
#define MBOX_BASE    0x40000000
#define DMA_BASE     0x70000000
#define NPU_BASE     0x60000000
#define TCDM_BASE    0x10000000

Vnpu_multi_cluster_top* dut;
vluint64_t main_time = 0;

void tick() {
    dut->clk_i = 1;
    dut->eval();
    main_time += 5;
    dut->clk_i = 0;
    dut->eval();
    main_time += 5;
}

class RiscvISS {
public:
    int cluster_id;
    uint32_t ram[I_SPM_SIZE / 4];
    uint32_t regs[32];
    uint32_t pc;
    bool wfi;
    bool done;
    int inst_count;

    RiscvISS(int id) {
        cluster_id = id;
        for (int i = 0; i < 32; i++) regs[i] = 0;
        pc = 0x1000;
        regs[2] = I_SPM_BASE + I_SPM_SIZE;
        wfi = false;
        done = false;
        inst_count = 0;
    }

    void load_bin(const char* filename) {
        ifstream file(filename, ios::binary);
        if (file) {
            file.read((char*)ram, I_SPM_SIZE);
        }
    }

    uint32_t mem_read(uint32_t addr) {
        if (addr >= I_SPM_BASE && addr < I_SPM_BASE + I_SPM_SIZE) {
            return ram[(addr - I_SPM_BASE) / 4];
        }
        
        dut->core_req_valid_i |= (1 << cluster_id);
        dut->core_req_write_i &= ~(1 << cluster_id);
        
        uint32_t current_addrs[4];
        for(int i=0; i<4; i++) current_addrs[i] = dut->core_req_addr_i[i];
        current_addrs[cluster_id] = addr;
        dut->core_req_addr_i[0] = current_addrs[0];
        dut->core_req_addr_i[1] = current_addrs[1];
        dut->core_req_addr_i[2] = current_addrs[2];
        dut->core_req_addr_i[3] = current_addrs[3];

        while (!(dut->core_req_ready_o & (1 << cluster_id))) tick();
        tick();
        dut->core_req_valid_i &= ~(1 << cluster_id);
        while (!(dut->core_rsp_valid_o & (1 << cluster_id))) tick();
        uint32_t data = dut->core_rsp_data_o[cluster_id];
        tick();
        return data;
    }

    void mem_write(uint32_t addr, uint32_t data) {
        if (addr >= I_SPM_BASE && addr < I_SPM_BASE + I_SPM_SIZE) {
            ram[(addr - I_SPM_BASE) / 4] = data;
            return;
        }

        dut->core_req_valid_i |= (1 << cluster_id);
        dut->core_req_write_i |= (1 << cluster_id);
        
        uint32_t current_addrs[4];
        uint32_t current_data[4];
        for(int i=0; i<4; i++) {
            current_addrs[i] = dut->core_req_addr_i[i];
            current_data[i] = dut->core_req_data_i[i];
        }
        current_addrs[cluster_id] = addr;
        current_data[cluster_id] = data;
        
        dut->core_req_addr_i[0] = current_addrs[0];
        dut->core_req_addr_i[1] = current_addrs[1];
        dut->core_req_addr_i[2] = current_addrs[2];
        dut->core_req_addr_i[3] = current_addrs[3];
        
        dut->core_req_data_i[0] = current_data[0];
        dut->core_req_data_i[1] = current_data[1];
        dut->core_req_data_i[2] = current_data[2];
        dut->core_req_data_i[3] = current_data[3];

        while (!(dut->core_req_ready_o & (1 << cluster_id))) tick();
        tick();
        dut->core_req_valid_i &= ~(1 << cluster_id);
        
        // TCDM interconnect does not generate response for writes
        if (!(addr >= TCDM_BASE && addr < TCDM_BASE + 0x10000000)) {
            while (!(dut->core_rsp_valid_o & (1 << cluster_id))) tick();
        }
        tick();
    }

    void step() {
        if (done) return;
        if (wfi) return;

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
            if (funct3 == 0) { if (regs[rs1] == regs[rs2]) next_pc = pc + b_imm; }
            else if (funct3 == 1) { if (regs[rs1] != regs[rs2]) next_pc = pc + b_imm; }
        }
        else if (op == 0x03) {
            uint32_t addr = regs[rs1] + i_imm;
            uint32_t val = mem_read(addr);
            if (rd) regs[rd] = val;
        }
        else if (op == 0x23) {
            uint32_t addr = regs[rs1] + s_imm;
            mem_write(addr, regs[rs2]);
            if (addr == MBOX_BASE + 0x04 && regs[rs2] == 2) {
                done = true;
            }
        }
        else if (op == 0x13) {
            if (funct3 == 0) { if (rd) regs[rd] = regs[rs1] + i_imm; }
            else if (funct3 == 7) { if (rd) regs[rd] = regs[rs1] & i_imm; }
            else if (funct3 == 1) { if (rd) regs[rd] = regs[rs1] << (i_imm & 0x1F); }
        }
        else if (op == 0x33) {
            if (funct7 == 1 && funct3 == 0) { if (rd) regs[rd] = regs[rs1] * regs[rs2]; }
            else if (funct7 == 0 && funct3 == 0) { if (rd) regs[rd] = regs[rs1] + regs[rs2]; }
        }
        else if (op == 0x73) {
            if (i_imm == 0x105) wfi = true;
        }

        pc = next_pc;
        inst_count++;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vnpu_multi_cluster_top;

    // Reset
    dut->clk_i = 0;
    dut->rst_ni = 0;
    dut->host_rsp_ready_i = 0xF;
    dut->axi_ar_ready_i = 1;
    dut->axi_r_valid_i = 0;
    tick(); tick();
    dut->rst_ni = 1;
    tick();

    vector<RiscvISS*> clusters;
    for (int i = 0; i < 4; i++) {
        RiscvISS* iss = new RiscvISS(i);
        iss->load_bin("../../npu_control_core/tb/fw/mobilenet.bin");
        // Each cluster gets a cluster ID passed in an initial register (e.g. x10 / a0)
        iss->regs[10] = i; 
        clusters.push_back(iss);
    }

    cout << "[INFO] Started RISC-V Firmware Execution for 4 Clusters (MobileNetV2)" << endl;

    int total_cycles = 0;
    int pending_axi_reads = 0;
    bool triggered[4] = {false, false, false, false};
    while (!Verilated::gotFinish() && total_cycles < 200000) {
        
        // Handle WFI / Host Mailbox trigger
        for (int i = 0; i < 4; i++) {
            if (clusters[i]->wfi && !triggered[i]) {
                dut->host_req_valid_i |= (1 << i);
                dut->host_req_write_i |= (1 << i);
                
                uint32_t addrs[4];
                uint32_t datas[4];
                for(int k=0; k<4; k++) { addrs[k] = dut->host_req_addr_i[k]; datas[k] = dut->host_req_data_i[k]; }
                addrs[i] = MBOX_BASE + 0x04;
                datas[i] = 1;
                dut->host_req_addr_i[0] = addrs[0]; dut->host_req_addr_i[1] = addrs[1]; dut->host_req_addr_i[2] = addrs[2]; dut->host_req_addr_i[3] = addrs[3];
                dut->host_req_data_i[0] = datas[0]; dut->host_req_data_i[1] = datas[1]; dut->host_req_data_i[2] = datas[2]; dut->host_req_data_i[3] = datas[3];
            }
        }
        
        bool all_done = true;
        for (int i = 0; i < 4; i++) {
            if (!(clusters[i]->wfi && triggered[i])) {
                all_done = false;
                break;
            }
        }
        
        if (all_done) break;

        for (int i = 0; i < 4; i++) {
            clusters[i]->step();
        }

        // AXI Responder state machine
        if (dut->axi_ar_valid_o && dut->axi_ar_ready_i) {
            pending_axi_reads++;
        }

        if (pending_axi_reads > 0) {
            dut->axi_r_valid_i = 1;
            dut->axi_r_last_i = 1;
            dut->axi_r_data_i[0] = 0x01020304; // Dummy data
            dut->axi_r_data_i[1] = 0x05060708;
            dut->axi_r_data_i[2] = 0x090A0B0C;
            dut->axi_r_data_i[3] = 0x0D0E0F10;
            if (dut->axi_r_valid_i && dut->axi_r_ready_o) {
                pending_axi_reads--;
            }
        } else {
            dut->axi_r_valid_i = 0;
            dut->axi_r_last_i = 0;
        }

        tick();
        
        // Post-tick check
        for (int i = 0; i < 4; i++) {
            if (clusters[i]->wfi && !triggered[i] && (dut->host_req_ready_o & (1 << i))) {
                dut->host_req_valid_i &= ~(1 << i);
                clusters[i]->wfi = false; // Core is allowed to proceed
                triggered[i] = true; // Mark as triggered
            }
        }

        if (total_cycles % 10000 == 0) {
            cout << "[TRACE] Cycle " << total_cycles << ", PCs: ";
            for (int i=0; i<4; i++) cout << hex << clusters[i]->pc << dec << " ";
            cout << endl;
        }

        total_cycles++;
    }

    if (total_cycles >= 200000) cout << "[ERROR] Timeout!" << endl;
    else cout << "[SUCCESS] MobileNetV2 Multi-Cluster Task Complete in " << total_cycles << " cycles!" << endl;

    for (int i = 0; i < 4; i++) delete clusters[i];
    delete dut;
    return 0;
}
