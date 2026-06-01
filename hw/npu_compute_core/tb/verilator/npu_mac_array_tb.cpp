// Copyright 2026 NPU IP
//
// Verilator C++ testbench for NPU MAC Array.
// Provides constrained-random and directed stimulus without UVM dependency.

#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vnpu_mac_array.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cassert>
#include <cstring>

// ============================================================
// Reference Model (same logic as DPI-C version)
// ============================================================
static int32_t mac_reference_model(
    const int8_t activations[],
    const int8_t weights[],
    int num_macs,
    bool clear_acc,
    int32_t prev_acc
) {
    int32_t acc = clear_acc ? 0 : prev_acc;
    for (int i = 0; i < num_macs; i++) {
        acc += (int32_t)activations[i] * (int32_t)weights[i];
    }
    return acc;
}

// ============================================================
// Testbench class
// ============================================================
class MacArrayTB {
public:
    Vnpu_mac_array* dut;
    VerilatedVcdC*  trace;
    uint64_t        sim_time;
    int32_t         ref_acc;
    int             pass_count;
    int             fail_count;
    int             total_count;

    static constexpr int NUM_MACS = 128;

    MacArrayTB() : sim_time(0), ref_acc(0), pass_count(0), fail_count(0), total_count(0) {
        dut = new Vnpu_mac_array;
        trace = new VerilatedVcdC;
        dut->trace(trace, 99);
        trace->open("mac_array_waves.vcd");
    }

    ~MacArrayTB() {
        trace->close();
        delete trace;
        delete dut;
    }

    void tick() {
        dut->clk_i = 0;
        dut->eval();
        trace->dump(sim_time++);

        dut->clk_i = 1;
        dut->eval();
        trace->dump(sim_time++);
    }

    void reset() {
        dut->rst_ni = 0;
        dut->valid_i = 0;
        dut->clear_acc_i = 0;
        for (int i = 0; i < NUM_MACS; i++) {
            dut->act_i[i] = 0;
            dut->wgt_i[i] = 0;
        }
        for (int i = 0; i < 10; i++) tick();
        dut->rst_ni = 1;
        tick();
    }

    void drive(const int8_t act[], const int8_t wgt[], bool clear_acc) {
        dut->valid_i = 1;
        dut->clear_acc_i = clear_acc ? 1 : 0;
        for (int i = 0; i < NUM_MACS; i++) {
            dut->act_i[i] = act[i];
            dut->wgt_i[i] = wgt[i];
        }
        tick();

        // De-assert valid
        dut->valid_i = 0;
        dut->clear_acc_i = 0;

        // Compute reference
        ref_acc = mac_reference_model(act, wgt, NUM_MACS, clear_acc, ref_acc);
    }

    // Wait for valid_o and check result
    void check_output() {
        // Pipeline latency: wait up to 10 cycles for valid_o
        for (int i = 0; i < 10; i++) {
            tick();
            if (dut->valid_o) {
                total_count++;
                int32_t actual = (int32_t)dut->acc_o;
                if (actual == ref_acc) {
                    pass_count++;
                } else {
                    fail_count++;
                    printf("[FAIL #%d] Expected=%d, Got=%d\n",
                           total_count, ref_acc, actual);
                }
                return;
            }
        }
        fail_count++;
        total_count++;
        printf("[FAIL #%d] Timeout: valid_o never asserted\n", total_count);
    }

    // ============================================================
    // Directed Tests
    // ============================================================
    void test_all_zeros() {
        printf("  [Directed] All zeros...\n");
        int8_t act[NUM_MACS] = {0};
        int8_t wgt[NUM_MACS] = {0};
        drive(act, wgt, true);
        check_output();
    }

    void test_all_max_positive() {
        printf("  [Directed] All max positive (127 * 127)...\n");
        int8_t act[NUM_MACS], wgt[NUM_MACS];
        memset(act, 127, NUM_MACS);
        memset(wgt, 127, NUM_MACS);
        // memset sets bytes, but 127 = 0x7F so this works for signed int8
        for (int i = 0; i < NUM_MACS; i++) { act[i] = 127; wgt[i] = 127; }
        drive(act, wgt, true);
        check_output();
    }

    void test_all_max_negative() {
        printf("  [Directed] All max negative (-128 * -128)...\n");
        int8_t act[NUM_MACS], wgt[NUM_MACS];
        for (int i = 0; i < NUM_MACS; i++) { act[i] = -128; wgt[i] = -128; }
        drive(act, wgt, true);
        check_output();
    }

    void test_mixed_extremes() {
        printf("  [Directed] Mixed extremes (127 * -128)...\n");
        int8_t act[NUM_MACS], wgt[NUM_MACS];
        for (int i = 0; i < NUM_MACS; i++) { act[i] = 127; wgt[i] = -128; }
        drive(act, wgt, true);
        check_output();
    }

    void test_accumulation() {
        printf("  [Directed] Accumulation (clear + accumulate)...\n");
        int8_t act[NUM_MACS], wgt[NUM_MACS];

        // First: clear accumulator, all 1*1
        for (int i = 0; i < NUM_MACS; i++) { act[i] = 1; wgt[i] = 1; }
        drive(act, wgt, true);
        check_output();

        // Second: accumulate on top, 2*3
        for (int i = 0; i < NUM_MACS; i++) { act[i] = 2; wgt[i] = 3; }
        drive(act, wgt, false);
        check_output();
    }

    void run_directed_tests() {
        printf("\n--- Directed Tests ---\n");
        test_all_zeros();
        test_all_max_positive();
        test_all_max_negative();
        test_mixed_extremes();
        test_accumulation();
    }

    // ============================================================
    // Constrained Random Tests
    // ============================================================
    void run_random_tests(int num_transactions) {
        printf("\n--- Random Tests (%d transactions) ---\n", num_transactions);
        for (int t = 0; t < num_transactions; t++) {
            int8_t act[NUM_MACS], wgt[NUM_MACS];
            for (int i = 0; i < NUM_MACS; i++) {
                act[i] = (int8_t)(rand() % 256 - 128);
                wgt[i] = (int8_t)(rand() % 256 - 128);
            }
            // 30% chance of clear_acc
            bool clear = (rand() % 100) < 30;
            drive(act, wgt, clear);
            check_output();

            if ((t + 1) % 500 == 0) {
                printf("  ... %d/%d transactions completed\n", t + 1, num_transactions);
            }
        }
    }

    // ============================================================
    // Report
    // ============================================================
    void report() {
        printf("\n============================================\n");
        printf("  VERILATOR TESTBENCH REPORT\n");
        printf("  Total:  %d\n", total_count);
        printf("  Passed: %d\n", pass_count);
        printf("  Failed: %d\n", fail_count);
        printf("============================================\n");
        if (fail_count > 0) {
            printf("  *** TEST FAILED ***\n");
        } else {
            printf("  *** TEST PASSED ***\n");
        }
        printf("============================================\n");
    }
};

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    unsigned int seed = (unsigned int)time(NULL);
    // Allow seed override via +seed=N
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "+seed=", 6) == 0) {
            seed = (unsigned int)atoi(argv[i] + 6);
        }
    }
    srand(seed);
    printf("[INFO] Random seed: %u\n", seed);

    int num_random = 2000;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "+num=", 5) == 0) {
            num_random = atoi(argv[i] + 5);
        }
    }

    MacArrayTB tb;
    tb.reset();
    tb.run_directed_tests();
    tb.run_random_tests(num_random);
    tb.report();

    return tb.fail_count > 0 ? 1 : 0;
}
