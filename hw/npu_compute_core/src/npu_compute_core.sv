// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_compute_core import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  input  core_cfg_t cfg_i,

  // Stream inputs from TCDM (via SSR)
  input  logic valid_i,
  input  logic clear_acc_i,
  input  logic signed [NpuActWidth-1:0] act_i [NpuMacsPerCore],
  input  logic signed [NpuWgtWidth-1:0] wgt_i [NpuMacsPerCore],
  output logic ready_o,

  // Stream output to TCDM (via SSR)
  output logic valid_o,
  output logic [31:0] act_o,
  input  logic ready_i
);

  // Interconnect signals
  logic mac_valid;
  logic signed [NpuAccWidth-1:0] mac_acc [4];
  logic mac_ready;
  
  logic act_valid;
  logic [NpuActWidth-1:0] act_data [4];
  logic act_ready;

  // 1. MAC Array Instantiation
  npu_mac_array i_mac_array (
    .clk_i       ( clk_i       ),
    .rst_ni      ( rst_ni      ),
    .valid_i     ( valid_i     ),
    .clear_acc_i ( clear_acc_i ),
    .ready_o     ( mac_ready   ),
    .act_i       ( act_i       ),
    .wgt_i       ( wgt_i       ),
    .valid_o     ( mac_valid   ),
    .acc_o       ( mac_acc     )
  );

  // 2. Activation Engine Instantiation
  npu_activation_engine i_act_engine (
    .clk_i       ( clk_i       ),
    .rst_ni      ( rst_ni      ),
    .cfg_i       ( cfg_i       ),
    .valid_i     ( mac_valid   ),
    .acc_i       ( mac_acc     ),
    .ready_o     ( act_ready   ),
    .valid_o     ( act_valid   ),
    .act_o       ( act_data    )
  );

  // Output routing
  assign valid_o = act_valid;
  assign act_o   = {act_data[3], act_data[2], act_data[1], act_data[0]};
  assign ready_o = mac_ready && act_ready; // Backpressure propagation

endmodule
