#include "test_utils.h"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>

using namespace std;

// Generate test data
void generate_matrix(vector<int8_t>& mat, size_t size) {
    for (size_t i = 0; i < size; i++) {
        mat[i] = (rand() % 10) - 5; // values between -5 and 4
    }
}

// Clear padding elements
void clear_padding_A(vector<int8_t>& A, uint32_t M, uint32_t K, uint32_t k_pad) {
    for (uint32_t m = 0; m < M; m++) {
        for (uint32_t k = K; k < k_pad; k++) {
            A[m * k_pad + k] = 0;
        }
    }
}

void clear_padding_B(vector<int8_t>& B, uint32_t K, uint32_t N, uint32_t k_pad, uint32_t n_pad) {
    for (uint32_t n = 0; n < N; n++) {
        for (uint32_t k = K; k < k_pad; k++) {
            uint32_t block_idx = (n / 4) * (k_pad / 32) + (k / 32);
            B[block_idx * 128 + (n % 4) * 32 + (k % 32)] = 0;
        }
    }
}

// Compute golden
void compute_matmul_golden(uint32_t M, uint32_t K, uint32_t N, 
                           const vector<int8_t>& A, const vector<int8_t>& B, 
                           vector<int8_t>& C_golden) {
    uint32_t k_pad = ((K + 31) / 32) * 32;
    for (uint32_t n = 0; n < N; n += 4) {
        for (uint32_t m = 0; m < M; m++) {
            for (uint32_t l = 0; l < 4 && (n + l) < N; l++) {
                int32_t acc = 0;
                for (uint32_t k = 0; k < K; k++) {
                    int8_t a = A[m * k_pad + k];
                    
                    uint32_t block_idx = (n / 4) * (k_pad / 32) + (k / 32);
                    int8_t w = B[block_idx * 128 + l * 32 + (k % 32)];
                    
                    acc += (int32_t)a * (int32_t)w;
                }
                
                int32_t offset = acc;
                int16_t clipped = offset;
                if (offset > 32767) clipped = 32767;
                else if (offset < -32768) clipped = -32768;
                
                int8_t act_out;
                if (clipped > 127) act_out = 127;
                else if (clipped < -128) act_out = -128;
                else act_out = (int8_t)clipped;
                
                uint32_t o_idx = (n / 4) * (M * 4) + m * 4 + l;
                C_golden[o_idx] = act_out;
            }
        }
    }
}

// Write matrix to DRAM
void write_matrix_to_dram(NpuClusterTestbench& tb, uint32_t addr, const vector<int8_t>& mat) {
    for (size_t i = 0; i < mat.size(); i += 4) {
        uint32_t word = 0;
        for (int b = 0; b < 4 && (i + b) < mat.size(); b++) {
            word |= ((uint32_t)(uint8_t)mat[i + b]) << (b * 8);
        }
        tb.write_dram(addr + i, word);
    }
}

struct MatMulTest {
    uint32_t M, K, N;
    uint32_t act_addr, wgt_addr, out_addr;
    vector<int8_t> C_golden;
};

int main(int argc, char** argv) {
    srand(42);
    NpuClusterTestbench tb(argc, argv, false);
    tb.init_dram(0x80000000, 32 * 1024 * 1024); // 32MB

    uint32_t cmd_base = 0x80000010;
    int cmd_idx = 0;
    
    // Test Configurations
    vector<MatMulTest> tests;
    tb.max_sim_time = 2000000;
    uint32_t current_dram_addr = 0x80100000;
    
    // 10 targeted random test cases covering different edge conditions
    // Seed fixed for reproducibility
    struct { uint32_t M, K, N; } cases[] = {
        {256, 256, 256},// 256x256 test!
    };
    for (auto& c : cases) {
        MatMulTest t = { c.M, c.K, c.N, 0, 0, 0 };
        tests.push_back(t);
    }
    
    cout << "[TEST] Generating " << tests.size() << " MatMul tests..." << endl;
    
    int multi_tile_cnt = 0;
    int acc_cnt = 0;
    
    for (size_t i = 0; i < tests.size(); i++) {
        MatMulTest& t = tests[i];
        
        uint32_t k_pad = ((t.K + 31) / 32) * 32;
        uint32_t n_pad = ((t.N + 3) / 4) * 4;
        
        uint32_t size_A = t.M * k_pad;
        uint32_t size_B = n_pad * k_pad;
        uint32_t size_C = t.M * n_pad; // 4 bytes per N-block per M
        
        t.act_addr = current_dram_addr; current_dram_addr += size_A;
        t.wgt_addr = current_dram_addr; current_dram_addr += size_B;
        t.out_addr = current_dram_addr; current_dram_addr += size_C;
        
        // Align to 4KB for neatness
        current_dram_addr = (current_dram_addr + 4095) & ~4095;
        
        vector<int8_t> A(size_A, 0);
        vector<int8_t> B(size_B, 0);
        generate_matrix(A, size_A);
        generate_matrix(B, size_B);
        clear_padding_A(A, t.M, t.K, k_pad);
        clear_padding_B(B, t.K, t.N, k_pad, n_pad);
        
        t.C_golden.resize(size_C, 0);
        compute_matmul_golden(t.M, t.K, t.N, A, B, t.C_golden);
        
        write_matrix_to_dram(tb, t.act_addr, A);
        write_matrix_to_dram(tb, t.wgt_addr, B);
        
        // Clear C
        for (uint32_t j = 0; j < size_C; j += 4) tb.write_dram(t.out_addr + j, 0);
        
        uint32_t tcdm_act = 0x10000000;
        uint32_t tcdm_wgt = tcdm_act + ((size_A + 4095) & ~4095);
        uint32_t tcdm_out = tcdm_wgt + ((size_B + 4095) & ~4095);
        
        // DMA In
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, t.act_addr, tcdm_act, size_A);
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, t.wgt_addr, tcdm_wgt, size_B);
        
        // CFG & COMPUTE
        write_cmd(tb, cmd_base, cmd_idx++, 0x34, t.M, t.K, t.N); // OP_CFG_MATMUL
        write_cmd(tb, cmd_base, cmd_idx++, 0x35, tcdm_act, tcdm_wgt, tcdm_out); // OP_COMPUTE_MATMUL
        write_cmd(tb, cmd_base, cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
        
        // DMA Out
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_WRITE, t.out_addr, tcdm_out, size_C);
        
        if (t.M * k_pad > 128) multi_tile_cnt++;
        if (t.K > 32) acc_cnt++;
    }
    
    write_cmd(tb, cmd_base, cmd_idx++, OP_FINISH, 0, 0, 0);
    
    // Setup command queue header at DDR base
    uint32_t queue_addr = 0x80000000;
    setup_cmd_queue(tb, queue_addr, cmd_idx, cmd_idx + 10);
    
    cout << "[TEST] Running Firmware... (Total Commands: " << cmd_idx << ")" << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout!" << endl;
        return 1;
    }
    
    // Verify
    int failed = 0;
    for (size_t i = 0; i < tests.size(); i++) {
        MatMulTest& t = tests[i];
        uint32_t n_pad = ((t.N + 3) / 4) * 4;
        uint32_t size_C = t.M * n_pad;
        
        for (uint32_t j = 0; j < size_C; j += 4) {
            uint32_t actual = tb.read_dram(t.out_addr + j);
            uint32_t expected = 0;
            for (int b = 0; b < 4 && (j + b) < size_C; b++) {
                expected |= ((uint32_t)(uint8_t)t.C_golden[j + b]) << (b * 8);
            }
            if (actual != expected) {
                cout << "[TEST] Mismatch Test " << i << " (M=" << t.M << ",K=" << t.K << ",N=" << t.N << ")" << endl;
                cout << "       Offset " << j << " | Expected: " << hex << expected << " Actual: " << actual << dec << endl;
                failed++;
                break;
            }
        }
    }
    
    cout << "========================================" << endl;
    cout << " MATMUL TEST STATISTICS                 " << endl;
    cout << "========================================" << endl;
    cout << " Total Tests Run : " << tests.size() << endl;
    cout << " Tests Passed    : " << (tests.size() - failed) << endl;
    cout << " Tests Failed    : " << failed << endl;
    cout << " Edge Cases Hit:" << endl;
    cout << " - Multi-Tile M  : " << multi_tile_cnt << " tests" << endl;
    cout << " - Accumulate K  : " << acc_cnt << " tests" << endl;
    cout << "========================================" << endl;
    
    if (failed > 0) return 1;
    cout << "[TEST] ALL TESTS PASSED!" << endl;
    return 0;
}
