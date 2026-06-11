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

// im2col to convert X [IH][IW][IC] to A [M][k_pad]
void im2col(const vector<int8_t>& X, vector<int8_t>& A, 
            int IH, int IW, int IC, 
            int OH, int OW, int OC, 
            int KH, int KW, int stride, int padding) {
    int K = KH * KW * IC;
    int M = OH * OW;
    int k_pad = ((K + 31) / 32) * 32;

    for (int m = 0; m < M; m++) {
        for (int k = 0; k < k_pad; k++) {
            A[m * k_pad + k] = 0;
        }
    }

    for (int oh = 0; oh < OH; oh++) {
        for (int ow = 0; ow < OW; ow++) {
            int m = oh * OW + ow;
            for (int kh = 0; kh < KH; kh++) {
                for (int kw = 0; kw < KW; kw++) {
                    for (int ic = 0; ic < IC; ic++) {
                        int ih = oh * stride - padding + kh;
                        int iw = ow * stride - padding + kw;
                        int k = kh * KW * IC + kw * IC + ic;
                        
                        if (ih >= 0 && ih < IH && iw >= 0 && iw < IW) {
                            A[m * k_pad + k] = X[(ih * IW + iw) * IC + ic];
                        }
                    }
                }
            }
        }
    }
}

// Pack W [OC][K] into B [n_pad][k_pad] matching HW layout
void pack_weights(const vector<int8_t>& W, vector<int8_t>& B, int K, int N) {
    int k_pad = ((K + 31) / 32) * 32;
    int n_pad = ((N + 3) / 4) * 4;

    for (int n = 0; n < n_pad; n++) {
        for (int k = 0; k < k_pad; k++) {
            uint32_t block_idx = (n / 4) * (k_pad / 32) + (k / 32);
            int idx_in_B = block_idx * 128 + (n % 4) * 32 + (k % 32);
            
            if (n < N && k < K) {
                B[idx_in_B] = W[n * K + k];
            } else {
                B[idx_in_B] = 0;
            }
        }
    }
}

// Compute golden MatMul
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
                int16_t clipped = offset > 32767 ? 32767 : (offset < -32768 ? -32768 : offset);
                int8_t act_out = clipped > 127 ? 127 : (clipped < -128 ? -128 : (int8_t)clipped);
                
                uint32_t o_idx = (n / 4) * (M * 4) + m * 4 + l;
                C_golden[o_idx] = act_out;
            }
        }
    }
}

void write_matrix_to_dram(NpuClusterTestbench& tb, uint32_t addr, const vector<int8_t>& mat) {
    for (size_t i = 0; i < mat.size(); i += 4) {
        uint32_t word = 0;
        for (int b = 0; b < 4 && (i + b) < mat.size(); b++) {
            word |= ((uint32_t)(uint8_t)mat[i + b]) << (b * 8);
        }
        tb.write_dram(addr + i, word);
    }
}

struct Conv2DTest {
    int IH, IW, IC;
    int KH, KW;
    int OC;
    int stride, padding;
    uint32_t M, K, N;
    uint32_t act_addr, wgt_addr, out_addr;
    vector<int8_t> C_golden;
};

int main(int argc, char** argv) {
    srand(42);
    NpuClusterTestbench tb(argc, argv, false);
    tb.init_dram(0x80000000, 32 * 1024 * 1024); // 32MB

    uint32_t queue_addr = 0x80000000;
    int cmd_idx = 0;
    uint32_t current_dram_addr = 0x80100000;
    
    // Conv2D Configurations to test
    vector<Conv2DTest> tests;
    
    struct { int IH, IW, IC, KH, KW, OC, stride, pad; } cases[] = {
        // Simple 1x1 conv (equivalent to MatMul)
        {8, 8, 32, 1, 1, 16, 1, 0},
        // Standard 3x3 conv with padding
        {16, 16, 16, 3, 3, 32, 1, 1},
        // 5x5 conv, strided
        {14, 14, 8, 5, 5, 16, 2, 0},
        // Depthwise-like dimensions (though standard conv here)
        {32, 32, 4, 3, 3, 8, 1, 1},
        // Edge cases
        {4, 4, 3, 3, 3, 4, 1, 0}, // Very small
        {64, 64, 1, 3, 3, 8, 1, 1} // Large spatial
    };
    
    for (auto& c : cases) {
        Conv2DTest t;
        t.IH = c.IH; t.IW = c.IW; t.IC = c.IC;
        t.KH = c.KH; t.KW = c.KW; t.OC = c.OC;
        t.stride = c.stride; t.padding = c.pad;
        
        int OH = (c.IH - c.KH + 2 * c.pad) / c.stride + 1;
        int OW = (c.IW - c.KW + 2 * c.pad) / c.stride + 1;
        
        t.M = OH * OW;
        t.K = c.KH * c.KW * c.IC;
        t.N = c.OC;
        
        tests.push_back(t);
    }
    
    cout << "[TEST] Generating " << tests.size() << " Conv2D tests..." << endl;
    
    for (size_t i = 0; i < tests.size(); i++) {
        Conv2DTest& t = tests[i];
        
        uint32_t k_pad = ((t.K + 31) / 32) * 32;
        uint32_t n_pad = ((t.N + 3) / 4) * 4;
        
        uint32_t size_X = t.IH * t.IW * t.IC;
        uint32_t size_A = t.M * k_pad;
        uint32_t size_W = t.N * t.K;
        uint32_t size_B = n_pad * k_pad;
        uint32_t size_C = t.M * n_pad; 
        
        t.act_addr = current_dram_addr; current_dram_addr += size_A;
        t.wgt_addr = current_dram_addr; current_dram_addr += size_B;
        t.out_addr = current_dram_addr; current_dram_addr += size_C;
        
        current_dram_addr = (current_dram_addr + 4095) & ~4095;
        
        vector<int8_t> X(size_X); generate_matrix(X, size_X);
        vector<int8_t> W(size_W); generate_matrix(W, size_W);
        
        vector<int8_t> A(size_A, 0);
        vector<int8_t> B(size_B, 0);
        
        im2col(X, A, t.IH, t.IW, t.IC, t.M / ((t.IW - t.KW + 2 * t.padding) / t.stride + 1), (t.IW - t.KW + 2 * t.padding) / t.stride + 1, t.OC, t.KH, t.KW, t.stride, t.padding);
        pack_weights(W, B, t.K, t.N);
        
        t.C_golden.resize(size_C, 0);
        compute_matmul_golden(t.M, t.K, t.N, A, B, t.C_golden);
        
        write_matrix_to_dram(tb, t.act_addr, A);
        write_matrix_to_dram(tb, t.wgt_addr, B);
        
        for (uint32_t j = 0; j < size_C; j += 4) tb.write_dram(t.out_addr + j, 0);
        
        uint32_t tcdm_act = 0x10000000;
        uint32_t tcdm_wgt = tcdm_act + ((size_A + 4095) & ~4095);
        uint32_t tcdm_out = tcdm_wgt + ((size_B + 4095) & ~4095);
        
        // Use cmd_base mapping logic
        uint32_t cmd_base = queue_addr + 16;
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, t.act_addr, tcdm_act, size_A);
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_READ, t.wgt_addr, tcdm_wgt, size_B);
        write_cmd(tb, cmd_base, cmd_idx++, 0x34, t.M, t.K, t.N); // OP_CFG_MATMUL
        write_cmd(tb, cmd_base, cmd_idx++, 0x35, tcdm_act, tcdm_wgt, tcdm_out); // OP_COMPUTE_MATMUL
        write_cmd(tb, cmd_base, cmd_idx++, OP_WAIT_COMPUTE, 0, 0, 0);
        write_cmd(tb, cmd_base, cmd_idx++, OP_DMA_WRITE, t.out_addr, tcdm_out, size_C);
    }
    
    uint32_t cmd_base = queue_addr + 16;
    write_cmd(tb, cmd_base, cmd_idx++, OP_FINISH, 0, 0, 0);
    
    setup_cmd_queue(tb, queue_addr, cmd_idx, cmd_idx + 10);
    
    tb.max_sim_time = 5000000;
    cout << "[TEST] Running Firmware... (Total Commands: " << cmd_idx << ")" << endl;
    if (!run_firmware(tb, "../../../sw/npu_runtime/npu_runtime.bin")) {
        cout << "[TEST] FAILED: Timeout!" << endl;
        return 1;
    }
    
    int failed = 0;
    for (size_t i = 0; i < tests.size(); i++) {
        Conv2DTest& t = tests[i];
        uint32_t n_pad = ((t.N + 3) / 4) * 4;
        uint32_t size_C = t.M * n_pad;
        
        for (uint32_t j = 0; j < size_C; j += 4) {
            uint32_t actual = tb.read_dram(t.out_addr + j);
            uint32_t expected = 0;
            for (int b = 0; b < 4 && (j + b) < size_C; b++) {
                expected |= ((uint32_t)(uint8_t)t.C_golden[j + b]) << (b * 8);
            }
            if (actual != expected) {
                cout << "[TEST] Mismatch Test " << i << " (IH=" << t.IH << ",IW=" << t.IW << ",K=" << t.K << ",OC=" << t.OC << ")" << endl;
                cout << "       Offset " << j << " | Expected: " << hex << expected << " Actual: " << actual << dec << endl;
                failed++;
                break;
            }
        }
    }
    
    cout << "========================================" << endl;
    cout << " CONV2D TEST STATISTICS                 " << endl;
    cout << "========================================" << endl;
    cout << " Total Tests Run : " << tests.size() << endl;
    cout << " Tests Passed    : " << (tests.size() - failed) << endl;
    cout << " Tests Failed    : " << failed << endl;
    cout << "========================================" << endl;
    
    if (failed > 0) return 1;
    cout << "[TEST] ALL TESTS PASSED!" << endl;
    return 0;
}
