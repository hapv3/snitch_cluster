`ifndef NPU_COMPUTE_CORE_PKG_SV
`define NPU_COMPUTE_CORE_PKG_SV

// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// Package containing definitions for the NPU compute core datapath.
package npu_compute_core_pkg;

  /// Precision parameters
  localparam NpuActWidth = 8;
  localparam NpuWgtWidth = 8;
  localparam NpuAccWidth = 32;

  /// Hardware parameters
  localparam NpuMacsPerCore = 128;

  /// TCDM Interface parameters
  localparam NpuTcdmDataWidth = 64;
  localparam NpuTcdmAddrWidth = 32;

  /// Activation function types
  typedef enum logic [2:0] {
    ACT_NONE       = 3'b000,
    ACT_RELU       = 3'b001,
    ACT_RELU6      = 3'b010,
    ACT_SIGMOID    = 3'b011, // Hardware LUT
    ACT_LEAKY_RELU = 3'b100, // alpha ≈ 0.125 (>>3)
    ACT_SILU       = 3'b101, // Hardware LUT
    ACT_MISH       = 3'b110  // Hardware LUT
  } act_type_e;

  /// Configuration structure for a compute core
  typedef struct packed {
    act_type_e act_type;
    logic [31:0] output_scale;
    logic [31:0] output_zero_point;
    logic [7:0]  shift_amount;
  } core_cfg_t;

endpackage

`endif
