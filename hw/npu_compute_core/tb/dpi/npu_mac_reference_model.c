// Copyright 2026 NPU IP
// DPI-C Reference Model for NPU MAC Array Verification
//
// This C function computes the golden reference output of the 128-MAC array
// for comparison against RTL results in the UVM scoreboard.

#include <stdint.h>

// npu_mac_reference_model:
//   Computes sum(activations[i] * weights[i]) for i in [0, num_macs).
//   If clear_acc is non-zero, the accumulator starts from zero.
//   Otherwise, it adds to prev_acc.
//
// Parameters:
//   activations - array of int8_t activation values
//   weights     - array of int8_t weight values
//   num_macs    - number of MAC operations (128)
//   clear_acc   - 1 to start fresh, 0 to accumulate
//   prev_acc    - previous accumulator value (used when clear_acc == 0)
//
// Returns:
//   int32_t accumulated result
int npu_mac_reference_model(
    const int8_t activations[],
    const int8_t weights[],
    int num_macs,
    int clear_acc,
    int prev_acc
) {
    int32_t acc = clear_acc ? 0 : (int32_t)prev_acc;

    for (int i = 0; i < num_macs; i++) {
        // Explicit widening to int32 before multiply to match hardware behavior
        int32_t a = (int32_t)activations[i];
        int32_t w = (int32_t)weights[i];
        acc += a * w;
    }

    return acc;
}
