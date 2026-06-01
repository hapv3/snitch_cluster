// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// Virtual interface for connecting UVM components to the MAC array DUT.
interface npu_mac_array_if;

  import npu_compute_core_pkg::*;

  logic clk;
  logic rst_n;

  // Inputs
  logic valid_i;
  logic clear_acc_i;
  logic signed [NpuActWidth-1:0] act_i [NpuMacsPerCore];
  logic signed [NpuWgtWidth-1:0] wgt_i [NpuMacsPerCore];

  // Outputs
  logic ready_o;
  logic valid_o;
  logic signed [NpuAccWidth-1:0] acc_o;

endinterface
