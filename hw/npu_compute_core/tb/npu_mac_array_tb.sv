// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// Top-level testbench for NPU MAC Array.
/// Instantiates DUT, interface, and launches UVM test.
module npu_mac_array_tb;

  import uvm_pkg::*;
  `include "uvm_macros.svh"

  import npu_compute_core_pkg::*;
  import npu_mac_array_tb_pkg::*;

  // ============================================================
  // Clock and Reset Generation
  // ============================================================
  logic clk;
  logic rst_n;

  initial begin
    clk = 1'b0;
    forever #0.5ns clk = ~clk; // 1 GHz clock (1ns period)
  end

  initial begin
    rst_n = 1'b0;
    #10ns;
    rst_n = 1'b1;
  end

  // ============================================================
  // Interface Instantiation
  // ============================================================
  npu_mac_array_if mac_if();

  assign mac_if.clk   = clk;
  assign mac_if.rst_n = rst_n;

  // ============================================================
  // DUT Instantiation
  // ============================================================
  npu_mac_array i_dut (
    .clk_i       ( clk              ),
    .rst_ni      ( rst_n            ),
    .valid_i     ( mac_if.valid_i     ),
    .clear_acc_i ( mac_if.clear_acc_i ),
    .ready_o     ( mac_if.ready_o     ),
    .act_i       ( mac_if.act_i       ),
    .wgt_i       ( mac_if.wgt_i       ),
    .valid_o     ( mac_if.valid_o     ),
    .acc_o       ( mac_if.acc_o       )
  );

  // ============================================================
  // Functional Coverage
  // ============================================================
  covergroup cg_mac_inputs @(posedge clk iff mac_if.valid_i);

    cp_act_0: coverpoint mac_if.act_i[0] {
      bins neg_max = {-128};
      bins neg     = {[-127:-1]};
      bins zero    = {0};
      bins pos     = {[1:126]};
      bins pos_max = {127};
    }

    cp_wgt_0: coverpoint mac_if.wgt_i[0] {
      bins neg_max = {-128};
      bins neg     = {[-127:-1]};
      bins zero    = {0};
      bins pos     = {[1:126]};
      bins pos_max = {127};
    }

    cp_clear_acc: coverpoint mac_if.clear_acc_i {
      bins clear = {1'b1};
      bins accum = {1'b0};
    }

    // Cross coverage
    cx_act_wgt: cross cp_act_0, cp_wgt_0;
    cx_act_clear: cross cp_act_0, cp_clear_acc;

  endgroup

  covergroup cg_mac_outputs @(posedge clk iff mac_if.valid_o);

    cp_acc_sign: coverpoint mac_if.acc_o[31] {
      bins positive = {1'b0};
      bins negative = {1'b1};
    }

    cp_acc_magnitude: coverpoint mac_if.acc_o {
      bins zero    = {0};
      bins small   = {[1:1000]};
      bins medium  = {[1001:100000]};
      bins large   = {[100001:$]};
      bins neg_sm  = {[-1000:-1]};
      bins neg_med = {[-100000:-1001]};
      bins neg_lg  = {[$:-100001]};
    }

  endgroup

  cg_mac_inputs  cg_in  = new();
  cg_mac_outputs cg_out = new();

  // ============================================================
  // UVM Configuration and Test Launch
  // ============================================================
  initial begin
    // Initialize inputs
    mac_if.valid_i     = 1'b0;
    mac_if.clear_acc_i = 1'b0;
    for (int i = 0; i < NpuMacsPerCore; i++) begin
      mac_if.act_i[i] = '0;
      mac_if.wgt_i[i] = '0;
    end

    // Register virtual interface
    uvm_config_db#(virtual npu_mac_array_if)::set(null, "*", "vif", mac_if);

    // Run test
    run_test();
  end

  // ============================================================
  // Coverage Reporting
  // ============================================================
  final begin
    $display("============================================");
    $display("  FUNCTIONAL COVERAGE REPORT");
    $display("  Input Coverage:  %.2f%%", cg_in.get_coverage());
    $display("  Output Coverage: %.2f%%", cg_out.get_coverage());
    $display("============================================");
  end

endmodule
