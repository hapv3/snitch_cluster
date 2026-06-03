#include <iostream>
#include <fstream>
#include <vector>
#include "Vnpu_multi_cluster_top.h"
#include "verilated.h"

using namespace std;

double sc_time_stamp() { return 0; }

#define I_SPM_BASE   0x00001000
#define I_SPM_SIZE   0x00008000
#define MBOX_BASE    0x40000000

Vnpu_multi_cluster_top* dut;
vluint64_t main_time = 0;

void tick() {
    dut->clk_i = 1;
    dut->eval();
    main_time++;
    dut->clk_i = 0;
    dut->eval();
    main_time++;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vnpu_multi_cluster_top;

    // Reset
    dut->clk_i = 0;
    dut->rst_ni = 0;
    dut->host_rsp_ready_i = 0xF;
    dut->axi_ar_ready_i = 1;
    dut->axi_r_valid_i = 0;
    dut->host_req_valid_i = 0;
    dut->fw_req_valid_i = 0;
    tick(); tick();
    dut->rst_ni = 1;
    tick();

    cout << "[INFO] Loading firmware ../../npu_control_core/tb/fw/yolo.bin into I-SPM..." << endl;
    FILE* fp = fopen("../../npu_control_core/tb/fw/yolo.bin", "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        int size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t* buf = new uint8_t[size];
        size_t read_bytes = fread(buf, 1, size, fp);
        fclose(fp);
        
        for (int i = 0; i < 4; i++) {
            for (int w = 0; w < size; w += 4) {
                uint32_t word = 0;
                if (w < size) word |= buf[w];
                if (w+1 < size) word |= buf[w+1] << 8;
                if (w+2 < size) word |= buf[w+2] << 16;
                if (w+3 < size) word |= buf[w+3] << 24;
                
                dut->fw_req_valid_i |= (1 << i);
                dut->fw_req_write_i |= (1 << i);
                
                uint32_t addrs[4];
                uint32_t datas[4];
                for(int k=0; k<4; k++) { addrs[k] = dut->fw_req_addr_i[k]; datas[k] = dut->fw_req_data_i[k]; }
                addrs[i] = I_SPM_BASE + w;
                datas[i] = word;
                
                dut->fw_req_addr_i[0] = addrs[0]; dut->fw_req_addr_i[1] = addrs[1]; dut->fw_req_addr_i[2] = addrs[2]; dut->fw_req_addr_i[3] = addrs[3];
                dut->fw_req_data_i[0] = datas[0]; dut->fw_req_data_i[1] = datas[1]; dut->fw_req_data_i[2] = datas[2]; dut->fw_req_data_i[3] = datas[3];
                
                tick();
                dut->fw_req_valid_i &= ~(1 << i);
                dut->fw_req_write_i &= ~(1 << i);
                tick();
                tick();
            }
        }
        delete[] buf;
    } else {
        cout << "[ERROR] Cannot open firmware file!" << endl;
        return 1;
    }
    cout << "[INFO] Firmware loaded." << endl;
    cout << "[INFO] Resetting core to restart execution from I-SPM..." << endl;
    dut->rst_ni = 0;
    tick(); tick();
    dut->rst_ni = 1;
    tick();


    int total_cycles = 0;
    int pending_axi_reads = 0;
    bool triggered[4] = {false, false, false, false};
    
    while (!Verilated::gotFinish() && total_cycles < 200000) {
        
        // Trigger all clusters once
        if (total_cycles == 100) {
            for (int i = 0; i < 4; i++) {
                dut->host_req_valid_i |= (1 << i);
                dut->host_req_write_i |= (1 << i);
                uint32_t addrs[4]; uint32_t datas[4];
                for(int k=0; k<4; k++) { addrs[k] = dut->host_req_addr_i[k]; datas[k] = dut->host_req_data_i[k]; }
                addrs[i] = MBOX_BASE + 0x04;
                datas[i] = 1; // trigger
                dut->host_req_addr_i[0] = addrs[0]; dut->host_req_addr_i[1] = addrs[1]; dut->host_req_addr_i[2] = addrs[2]; dut->host_req_addr_i[3] = addrs[3];
                dut->host_req_data_i[0] = datas[0]; dut->host_req_data_i[1] = datas[1]; dut->host_req_data_i[2] = datas[2]; dut->host_req_data_i[3] = datas[3];
            }
        }
        
        for (int i = 0; i < 4; i++) {
            if (dut->host_req_valid_i & (1 << i)) {
                if (dut->host_req_ready_o & (1 << i)) {
                    dut->host_req_valid_i &= ~(1 << i);
                    triggered[i] = true;
                }
            }
        }

        // AXI Responder state machine
        if (dut->axi_ar_valid_o && dut->axi_ar_ready_i) {
            pending_axi_reads++;
        }

        if (pending_axi_reads > 0) {
            dut->axi_r_valid_i = 1;
            dut->axi_r_last_i = 1;
            dut->axi_r_data_i[0] = 0x01020304; 
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

        bool all_done = true;
        for (int i = 0; i < 4; i++) {
            if (!(dut->irq_to_host_o & (1 << i))) {
                all_done = false;
            }
        }
        
        if (total_cycles > 200 && all_done) {
            cout << "[SUCCESS] Task Complete in " << total_cycles << " cycles!" << endl;
            break;
        }
        
        tick();
        total_cycles++;
    }

    if (total_cycles >= 200000) cout << "[ERROR] Timeout!" << endl;
    
    delete dut;
    return 0;
}
