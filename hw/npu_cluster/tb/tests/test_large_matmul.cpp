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

int main(int argc, char** argv) {
    srand(42);
    NpuClusterTestbench tb(argc, argv, false);
    tb.init_dram(0x80000000, 32 * 1024 * 1024); // 32MB

    uint32_t cmd_base = 0x80000010;
    int cmd_idx = 0;
    
    uint32_t M = 256;
    uint32_t K = 256;
    uint32_t N = 256;
    
    cout << "[TEST] Generating Large MatMul test M=" << M << ", K=" << K << ", N=" << N << endl;
    fflush(stdout);
    
    uint32_t k_pad = ((K + 31) / 32) * 32;
    uint32_t n_pad = ((N + 3) / 4) * 4;
    
    uint32_t size_A = M * k_pad;
    uint32_t size_B = n_pad * k_pad;
    uint32_t size_C = M * n_pad;
    
    uint32_t act_addr = 0x80100000;
    uint32_t wgt_addr = 0x80100000 + size_A;
    uint32_t out_addr = wgt_addr + size_B;
    
    vector<int8_t> A(size_A, 0);
    vector<int8_t> B(size_B, 0);
    generate_matrix(A, size_A);
    generate_matrix(B, size_B);
    
    cout << "[TEST] Computing Golden Model..." << endl;
    fflush(stdout);
    vector<int8_t> C_golden(size_C, 0);
    compute_matmul_golden(M, K, N, A, B, C_golden);
    
    cout << "[TEST] Writing Data to DRAM..." << endl;
    fflush(stdout);
    write_matrix_to_dram(tb, act_addr, A);
    write_matrix_to_dram(tb, wgt_addr, B);
    for (uint32_t j = 0; j < size_C; j += 4) tb.write_dram(out_addr + j, 0);
    
    // CFG & COMPUTE LARGE
    write_cmd(tb, cmd_base, cmd_idx++, 0x34, M, K, N); // OP_CFG_MATMUL
    write_cmd(tb, cmd_base, cmd_idx++, 0x36, act_addr, wgt_addr, out_addr); // OP_COMPUTE_MATMUL_LARGE
    write_cmd(tb, cmd_base, cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
    write_cmd(tb, cmd_base, cmd_idx++, OP_FINISH, 0, 0, 0);
    
    uint32_t queue_addr = 0x80000000;
    setup_cmd_queue(tb, queue_addr, cmd_idx, 100);
    
    cout << "[TEST] Running Firmware for Large MatMul..." << endl;
    fflush(stdout);
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout!" << endl;
        return 1;
    }
    
    cout << "[TEST] Verifying Results..." << endl;
    int failed = 0;
    for (uint32_t j = 0; j < size_C; j += 4) {
        uint32_t actual = tb.read_dram(out_addr + j);
        uint32_t expected = 0;
        for (int b = 0; b < 4 && (j + b) < size_C; b++) {
            expected |= ((uint32_t)(uint8_t)C_golden[j + b]) << (b * 8);
        }
        if (actual != expected) {
            cout << "[TEST] Mismatch! Offset " << j << " | Expected: " << hex << expected << " Actual: " << actual << dec << endl;
            failed++;
            if (failed > 10) break; // only print first 10 errors
        }
    }
    
    if (failed > 0) {
        cout << "[TEST] FAILED! Found " << failed << " mismatches." << endl;
        return 1;
    }
    cout << "[TEST] ALL TESTS PASSED!" << endl;
    return 0;
}
