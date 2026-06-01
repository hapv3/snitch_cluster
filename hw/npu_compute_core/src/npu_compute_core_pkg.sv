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
  typedef enum logic [1:0] {
    ACT_NONE    = 2'b00,
    ACT_RELU    = 2'b01,
    ACT_RELU6   = 2'b10,
    ACT_SIGMOID = 2'b11 // LUT-based
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
