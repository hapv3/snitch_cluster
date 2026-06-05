#include "npu_testbench.h"
#include <iomanip>

vluint64_t g_main_time = 0;

double sc_time_stamp() {
    return g_main_time;
}

NpuClusterTestbench::NpuClusterTestbench(int argc, char** argv, bool trace) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    dut = new Vnpu_cluster_top;
    
    trace_enabled = trace;
    if (trace_enabled) {
        tfp = new VerilatedVcdC;
        dut->trace(tfp, 99);
        tfp->open("trace.vcd");
    } else {
        tfp = nullptr;
    }
    
    main_time = 0;
    max_sim_time = 500000;
    dram_base_addr = 0x80000000;
    dram_memory.resize(0);
}

NpuClusterTestbench::~NpuClusterTestbench() {
    if (tfp) {
        tfp->close();
        delete tfp;
    }
    delete dut;
}

void NpuClusterTestbench::tick() {
    dut->clk_i = 1;
    dut->eval();
    if (tfp) tfp->dump(main_time);
    main_time++;
    g_main_time = main_time;
    dut->clk_i = 0;
    dut->eval();
    if (tfp) tfp->dump(main_time);
    main_time++;
    g_main_time = main_time;
}

void NpuClusterTestbench::reset() {
    dut->rst_ni = 0;
    dut->s_axi_awvalid_i = 0;
    dut->s_axi_wvalid_i = 0;
    dut->s_axi_arvalid_i = 0;
    dut->s_axi_bready_i = 1;
    dut->s_axi_rready_i = 1;
    dut->axi_ar_ready_i = 1;
    dut->axi_r_valid_i = 0;
    
    dut->eval();
    tick(); tick();
    dut->rst_ni = 1;
    tick();
}

void NpuClusterTestbench::axi_lite_write(uint32_t addr, uint32_t data) {
    dut->s_axi_awvalid_i = 1;
    dut->s_axi_awaddr_i = addr;
    dut->s_axi_wvalid_i = 1;
    dut->s_axi_wdata_i = data;
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

uint32_t NpuClusterTestbench::axi_lite_read(uint32_t addr) {
    dut->s_axi_arvalid_i = 1;
    dut->s_axi_araddr_i = addr;
    dut->eval();
    
    while (!dut->s_axi_arready_o) {
        tick();
    }
    tick();
    dut->s_axi_arvalid_i = 0;
    dut->eval();
    
    while (!dut->s_axi_rvalid_o) {
        tick();
    }
    uint32_t data = dut->s_axi_rdata_o;
    tick();
    return data;
}

bool NpuClusterTestbench::load_firmware(const char* filepath) {
    std::cout << "[INFO] Loading firmware " << filepath << " into I-SPM..." << std::endl;
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        std::cout << "[ERROR] Cannot open firmware file!" << std::endl;
        return false;
    }
    
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* buf = new uint8_t[size];
    size_t read_bytes = fread(buf, 1, size, fp);
    fclose(fp);
    if(read_bytes != (size_t)size) { return false; }
    
    for (int w = 0; w < size; w += 4) {
        uint32_t word = 0;
        if (w < size) word |= buf[w];
        if (w+1 < size) word |= buf[w+1] << 8;
        if (w+2 < size) word |= buf[w+2] << 16;
        if (w+3 < size) word |= buf[w+3] << 24;
        
        axi_lite_write(0x00001000 + w, word);
    }
    delete[] buf;
    std::cout << "[INFO] Firmware loaded." << std::endl;
    return true;
}

void NpuClusterTestbench::start_task(const std::vector<uint32_t>& task_data) {
    std::cout << "[INFO] Host sending task to Mailbox via AXI-Lite at time " << main_time << "..." << std::endl;
    for (size_t i = 0; i < task_data.size(); i++) {
        axi_lite_write(0x40000008 + i * 4, task_data[i]);
    }
    // Start command
    axi_lite_write(0x40000004, 1);
}

void NpuClusterTestbench::init_dram(uint32_t base_addr, size_t size_bytes) {
    dram_base_addr = base_addr;
    dram_memory.resize(size_bytes / 4, 0);
}

void NpuClusterTestbench::write_dram(uint32_t addr, uint32_t data) {
    if (addr >= dram_base_addr && addr < dram_base_addr + dram_memory.size() * 4) {
        dram_memory[(addr - dram_base_addr) / 4] = data;
    }
}

uint32_t NpuClusterTestbench::read_dram(uint32_t addr) {
    if (addr >= dram_base_addr && addr < dram_base_addr + dram_memory.size() * 4) {
        return dram_memory[(addr - dram_base_addr) / 4];
    }
    return 0;
}

bool NpuClusterTestbench::wait_for_interrupt(int max_cycles) {
    uint64_t start_time = main_time;
    
    // Read state
    uint32_t r_beats_left = 0;
    uint32_t r_current_addr = 0;
    
    // Write state  
    bool w_pending = false; // AW accepted, waiting for W
    uint32_t w_addr = 0;
    uint32_t w_beats_left = 0;

    // Initial ready signals
    dut->axi_ar_ready_i = 1;
    dut->axi_aw_ready_i = 1;
    dut->axi_w_ready_i = 0;
    dut->axi_b_valid_i = 0;
    dut->eval();

    while (main_time < start_time + max_cycles && !Verilated::gotFinish()) {
        // Sample BEFORE clock edge
        bool ar_handshake = dut->axi_ar_valid_o && dut->axi_ar_ready_i;
        uint32_t ar_addr = dut->axi_ar_addr_o;
        uint32_t ar_len  = dut->axi_ar_len_o;
        bool r_handshake = dut->axi_r_valid_i && dut->axi_r_ready_o;
        bool aw_handshake = dut->axi_aw_valid_o && dut->axi_aw_ready_i;
        uint32_t aw_addr = dut->axi_aw_addr_o;
        uint32_t aw_len  = dut->axi_aw_len_o;
        bool w_handshake = dut->axi_w_valid_o && dut->axi_w_ready_i;
        bool b_handshake = dut->axi_b_valid_i && dut->axi_b_ready_o;

        // Clock edge (keep axi_aw_ready_i=1 through the tick so DMA
        //  sees it at the rising edge and transitions AXI_AW -> AXI_W)
        tick();

        // Post-tick: After AW accepted, prepare W channel
        if (aw_handshake && !w_pending) {
            w_addr = aw_addr;
            w_beats_left = aw_len + 1;
            w_pending = true;
            dut->axi_aw_ready_i = 0;   // block next AW until write completes
            dut->axi_w_ready_i = 1;    // ready to accept W data immediately
            dut->eval();
            std::cout << "[" << main_time << "] AW Handshake. addr=" << aw_addr << " len=" << aw_len << std::endl;
        }

        // Post-tick: serve read data
        if (r_handshake) {
            if (r_beats_left > 0) {
                r_beats_left--;
                r_current_addr += 4;
                if (r_beats_left == 0) {
                    dut->axi_r_valid_i = 0;
                    dut->axi_r_last_i  = 0;
                } else {
                    dut->axi_r_data_i[0] = read_dram(r_current_addr);
                    dut->axi_r_data_i[1] = read_dram(r_current_addr + 4);
                    dut->axi_r_data_i[2] = read_dram(r_current_addr + 8);
                    dut->axi_r_data_i[3] = read_dram(r_current_addr + 12);
                    dut->axi_r_last_i = (r_beats_left == 1) ? 1 : 0;
                }
                dut->eval();
            }
        }

        if (ar_handshake) {
            r_current_addr = ar_addr;
            r_beats_left   = ar_len + 1;
            dut->axi_r_valid_i    = 1;
            dut->axi_r_data_i[0]  = read_dram(r_current_addr);
            dut->axi_r_data_i[1]  = read_dram(r_current_addr + 4);
            dut->axi_r_data_i[2]  = read_dram(r_current_addr + 8);
            dut->axi_r_data_i[3]  = read_dram(r_current_addr + 12);
            dut->axi_r_last_i     = (r_beats_left == 1) ? 1 : 0;
            dut->axi_ar_ready_i   = 0;
            dut->eval();
            std::cout << "[" << main_time << "] AR Handshake. addr=" << ar_addr << " len=" << ar_len << std::endl;
        }
        
        if (!dut->axi_r_valid_i && !dut->axi_ar_ready_i) {
            dut->axi_ar_ready_i = 1;
            dut->eval();
        }

        // W channel: write data to DRAM
        if (w_handshake && w_pending) {
            uint16_t strb = dut->axi_w_strb_o;
            for (int lane = 0; lane < 4; lane++) {
                if (((strb >> (lane * 4)) & 0xF) == 0xF) {
                    write_dram(w_addr, dut->axi_w_data_o[lane]);
                    std::cout << "[" << main_time << "] W. addr=" << std::hex << w_addr
                              << " data=" << dut->axi_w_data_o[lane] << std::dec << std::endl;
                    break;
                }
            }
            w_beats_left--;
            w_addr += 4;
            if (dut->axi_w_last_o || w_beats_left == 0) {
                dut->axi_w_ready_i = 0;
                dut->axi_b_valid_i = 1;
                dut->eval();
            }
        }

        // B channel: response complete
        if (b_handshake) {
            dut->axi_b_valid_i = 0;
            w_pending = false;
            dut->axi_aw_ready_i = 1;
            dut->eval();
            std::cout << "[" << main_time << "] B complete." << std::endl;
        }

        if (main_time % 500000 == 0) {
            std::cout << "[" << main_time << "] AR_V=" << (int)dut->axi_ar_valid_o
                      << " AW_V=" << (int)dut->axi_aw_valid_o
                      << " W_V="  << (int)dut->axi_w_valid_o
                      << " W_R="  << (int)dut->axi_w_ready_i << std::endl;
        }

        if (dut->irq_to_host_o) {
            std::cout << "[SUCCESS] Done at time " << main_time << "!" << std::endl;
            return true;
        }
    }
    std::cout << "[ERROR] Timeout!" << std::endl;
    return false;
}

