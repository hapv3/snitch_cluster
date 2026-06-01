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

  // Accumulated output (4 lanes of accumulators)
  output logic valid_o,
  output logic signed [NpuAccWidth-1:0] acc_o [4]
);

  localparam int Lanes = 4;
  localparam int MacsPerLane = 32;

  // We broadcast the first 32 activations to ALL 4 lanes!
  // The weights are grouped 16 per lane.
  
  // Intermediate product wires
  logic signed [15:0] prod [Lanes][MacsPerLane];
  
  // Pipeline registers for products
  logic signed [15:0] prod_q [Lanes][MacsPerLane];
  logic valid_q;
  logic clear_acc_q;

  // Multiplier Array (Stage 1)
  always_comb begin
    for (int l = 0; l < Lanes; l++) begin
      for (int i = 0; i < MacsPerLane; i++) begin
        // Broadcast activation [i], unique weight [l*MacsPerLane + i]
        prod[l][i] = act_i[i] * wgt_i[l * MacsPerLane + i];
      end
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

  // Pipeline registers for products
  for (genvar l = 0; l < Lanes; l++) begin : gen_prod_reg_lane
    for (genvar i = 0; i < MacsPerLane; i++) begin : gen_prod_reg
      always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
          prod_q[l][i] <= '0;
        end else if (valid_i) begin
          prod_q[l][i] <= prod[l][i];
        end
      end
    end
  end

  // Adder Tree & Accumulator
  logic signed [NpuAccWidth-1:0] sum_tree [Lanes];
  logic signed [NpuAccWidth-1:0] acc_q [Lanes];

  always_comb begin
    for (int l = 0; l < Lanes; l++) begin
      sum_tree[l] = '0;
      for (int i = 0; i < MacsPerLane; i++) begin
        sum_tree[l] = sum_tree[l] + NpuAccWidth'(prod_q[l][i]);
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int l = 0; l < Lanes; l++) acc_q[l] <= '0;
      valid_o <= 1'b0;
    end else begin
      valid_o <= valid_q;
      if (valid_q) begin
        if (clear_acc_q) begin
          for (int l = 0; l < Lanes; l++) acc_q[l] <= sum_tree[l];
        end else begin
          for (int l = 0; l < Lanes; l++) acc_q[l] <= acc_q[l] + sum_tree[l];
        end
      end
    end
  end

  assign acc_o = acc_q;
  assign ready_o = 1'b1;

endmodule
