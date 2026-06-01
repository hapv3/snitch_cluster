// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_mac_array import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  // Pipeline control
  input  logic valid_i,
  input  logic clear_acc_i, // Clear accumulator for new output tile
  output logic ready_o,

  // Operands from SSR
  input  logic signed [NpuActWidth-1:0] act_i [NpuMacsPerCore],
  input  logic signed [NpuWgtWidth-1:0] wgt_i [NpuMacsPerCore],

  // Accumulated output
  output logic valid_o,
  output logic signed [NpuAccWidth-1:0] acc_o
);

  // Intermediate product wires
  logic signed [15:0] prod [NpuMacsPerCore];
  
  // Pipeline registers for products
  logic signed [15:0] prod_q [NpuMacsPerCore];
  logic valid_q;
  logic clear_acc_q;

  // Multiplier Array (Stage 1)
  always_comb begin
    for (int i = 0; i < NpuMacsPerCore; i++) begin
      prod[i] = act_i[i] * wgt_i[i];
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q <= 1'b0;
      clear_acc_q <= 1'b0;
    end else begin
      valid_q <= valid_i;
      clear_acc_q <= clear_acc_i;
    end
  end

  // Pipeline registers for products (generate block to satisfy Verilator BLKLOOPINIT)
  for (genvar i = 0; i < NpuMacsPerCore; i++) begin : gen_prod_reg
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        prod_q[i] <= '0;
      end else if (valid_i) begin
        prod_q[i] <= prod[i];
      end
    end
  end

  // Adder Tree (Stage 2 - simplified as behavioral sum for synthesis tool to optimize)
  logic signed [NpuAccWidth-1:0] sum_tree;
  always_comb begin
    sum_tree = '0;
    for (int i = 0; i < NpuMacsPerCore; i++) begin
      sum_tree = sum_tree + NpuAccWidth'(prod_q[i]);
    end
  end

  // Accumulator (Stage 3)
  logic signed [NpuAccWidth-1:0] acc_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      acc_q <= '0;
      valid_o <= 1'b0;
    end else begin
      valid_o <= valid_q;
      if (valid_q) begin
        if (clear_acc_q) begin
          acc_q <= sum_tree;
        end else begin
          acc_q <= acc_q + sum_tree;
        end
      end
    end
  end

  assign acc_o = acc_q;
  assign ready_o = 1'b1; // Always ready in this simplified pipeline

endmodule
