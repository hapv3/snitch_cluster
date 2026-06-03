// NPU Cluster Top Level Verilator Testbench (RTL Snitch Core)
#include <iostream>
#include <fstream>
#include "Vnpu_cluster_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

using namespace std;

#define I_SPM_BASE   0x00001000
#define I_SPM_SIZE   0x00008000

Vnpu_cluster_top* dut;
VerilatedVcdC* tfp;
vluint64_t main_time = 0;

double sc_time_stamp() {
    return main_time;
}

void tick() {
    dut->clk_i = 1;
    dut->eval();
    if (tfp) tfp->dump(main_time);
    main_time++;
    dut->clk_i = 0;
    dut->eval();
    if (tfp) tfp->dump(main_time);
    main_time++;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    dut = new Vnpu_cluster_top;

    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("trace.vcd");

    dut->rst_ni = 0;
    dut->s_axi_awvalid_i = 0;
    dut->s_axi_wvalid_i = 0;
    dut->s_axi_arvalid_i = 0;
    dut->s_axi_bready_i = 1;
    dut->s_axi_rready_i = 1;
    
    // Default AXI handshakes
    dut->axi_ar_ready_i = 1;
    dut->axi_r_valid_i = 0;
            dut->eval();
    tick(); tfp->dump(main_time); tick(); tfp->dump(main_time);
    dut->rst_ni = 1;
    tick(); tfp->dump(main_time);

    cout << "[INFO] Loading firmware ../../npu_control_core/tb/fw/mobilenet.bin into I-SPM..." << endl;
    FILE* fp = fopen("../../npu_control_core/tb/fw/mobilenet.bin", "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        int size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t* buf = new uint8_t[size];
        fread(buf, 1, size, fp);
        fclose(fp);
        
        for (int w = 0; w < size; w += 4) {
            uint32_t word = 0;
            if (w < size) word |= buf[w];
            if (w+1 < size) word |= buf[w+1] << 8;
            if (w+2 < size) word |= buf[w+2] << 16;
            if (w+3 < size) word |= buf[w+3] << 24;
            
            dut->s_axi_awvalid_i = 1;
            dut->s_axi_awaddr_i = I_SPM_BASE + w;
            dut->s_axi_wvalid_i = 1;
            dut->s_axi_wdata_i = word;
            dut->eval();
            
            while (!(dut->s_axi_awready_o && dut->s_axi_wready_o)) {
                tick();
            }
            tick();
            dut->s_axi_awvalid_i = 0;
            dut->s_axi_wvalid_i = 0;
            dut->eval();
            
            while (!dut->s_axi_bvalid_o) {
                tick();
            }
            tick();
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
    
    int MAX_SIM_TIME = 200000;
    bool success = false;
    
    // Not needed anymore since we use s_axi_bready_i and s_axi_rready_i
    // dut->host_rsp_ready_i = 1;

    uint32_t host_task[] = {
        0x1234, // OP
        0x20000000, // ACT PTR
        0x30000000, // WGT PTR
        0x40000000, // OUT PTR
        32, // M
        32, // N
        32  // K
    };
    int task_idx = 0;
    bool start_task = false;

    while (main_time < MAX_SIM_TIME && !Verilated::gotFinish()) {
        // Mock AXI Read Response for DMA
        bool r_handshake = dut->axi_r_valid_i && dut->axi_r_ready_o;
        
        tick();
        
        if (r_handshake) {
            cout << "[" << main_time << "] [TB] AXI Read Rsp Accepted" << endl;
            dut->axi_r_valid_i = 0;
            dut->axi_r_last_i = 0;
            dut->eval();
        }
        
        if (dut->axi_ar_valid_o && dut->axi_ar_ready_i) {
            cout << "[" << main_time << "] [TB] AXI Read Req Addr: " << hex << dut->axi_ar_addr_o << dec << endl;
            dut->axi_r_valid_i = 1;
            dut->axi_r_data_i[0] = 0xDEADBEEF;
            dut->axi_r_data_i[1] = 0;
            dut->axi_r_data_i[2] = 0;
            dut->axi_r_data_i[3] = 0;
            dut->axi_r_last_i = 1; // Simplify: just respond with 1 beat
            dut->eval();
        }
        
        // Check for success (Mailbox interrupt or whatever indicates done)
        if (dut->irq_to_host_o) {
            cout << "[SUCCESS] Cluster execution finished!" << endl;
            success = true;
            break;
        }
        
        static bool task_sent = false;
        // Trigger start from Mailbox at cycle 9000 (after firmware load and 2nd reset)
        if (main_time >= 9000 && !task_sent) {
            cout << "[INFO] Host sending task to Mailbox via AXI-Lite at time " << main_time << "..." << endl;
            task_sent = true;
            for (int i=0; i<8; i++) {
                dut->s_axi_awvalid_i = 1;
                dut->s_axi_wvalid_i = 1;
                if (i < 7) {
                    dut->s_axi_awaddr_i = 0x40000008 + i * 4;
                    dut->s_axi_wdata_i = host_task[i];
                } else {
                    dut->s_axi_awaddr_i = 0x40000004; // Start command
                    dut->s_axi_wdata_i = 1;
                }
                dut->eval();
                while (!(dut->s_axi_awready_o && dut->s_axi_wready_o)) {
                    tick();
                }
                tick();
                dut->s_axi_awvalid_i = 0;
                dut->s_axi_wvalid_i = 0;
                dut->eval();
                while (!dut->s_axi_bvalid_o) {
                    tick();
                }
                tick();
            }
        }
    }
    
    if (!success) {
        cout << "[ERROR] Timeout at time " << main_time << "!" << endl;
        // Debug prints
        cout << "  irq_to_host_o = " << (int)dut->irq_to_host_o << endl;
        cout << "  axi_ar_valid_o = " << (int)dut->axi_ar_valid_o << endl;
        cout << "  axi_ar_ready_i = " << (int)dut->axi_ar_ready_i << endl;
        cout << "  axi_r_valid_i = " << (int)dut->axi_r_valid_i << endl;
        cout << "  axi_r_valid_i = " << (int)dut->axi_r_valid_i << endl;
        // Print the compute_irq from the internal cluster if possible (Verilator exposes internal states if public)
    }
    
    delete dut;
    return 0;
}
