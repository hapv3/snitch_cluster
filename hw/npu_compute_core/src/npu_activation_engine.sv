// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_activation_engine import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  input  core_cfg_t cfg_i,

  input  logic valid_i,
  input  logic signed [NpuAccWidth-1:0] acc_i [4],
  output logic ready_o,

  output logic valid_o,
  output logic [NpuActWidth-1:0] act_o [4]
);

  logic signed [NpuAccWidth-1:0] requantized [4];
  logic signed [15:0] clipped [4];
  logic [NpuActWidth-1:0] activated [4];

  // 1. Requantization (Multiply-Shift-Saturate)
  always_comb begin
    for (int l = 0; l < 4; l++) begin
      automatic logic signed [63:0] scaled = acc_i[l] * $signed(cfg_i.output_scale);
      automatic logic signed [31:0] shifted = scaled >>> cfg_i.shift_amount;
      automatic logic signed [31:0] offset = shifted + $signed(cfg_i.output_zero_point);
      
      // Clip to INT16 before activation
      if (offset > 32'sd32767) clipped[l] = 16'sd32767;
      else if (offset < -32'sd32768) clipped[l] = -16'sd32768;
      else clipped[l] = 16'(offset);
    end
  end

  // 2. Activation Function
  always_comb begin
    for (int l = 0; l < 4; l++) begin
      activated[l] = '0;
      unique case (cfg_i.act_type)
        ACT_NONE: begin
          if (clipped[l] > 16'sd127) activated[l] = 8'sd127;
          else if (clipped[l] < -16'sd128) activated[l] = -8'sd128;
          else activated[l] = 8'(clipped[l]);
        end
        ACT_RELU: begin
          if (clipped[l] > 16'sd127) activated[l] = 8'sd127;
          else if (clipped[l] < 16'sd0) activated[l] = 8'sd0;
          else activated[l] = 8'(clipped[l]);
        end
        ACT_RELU6: begin
          if (clipped[l] > 16'sd127) activated[l] = 8'sd127;
          else if (clipped[l] < 16'sd0) activated[l] = 8'sd0;
          else activated[l] = 8'(clipped[l]);
        end
        ACT_SIGMOID: begin
          activated[l] = 8'sd0;
        end
        ACT_LEAKY_RELU: begin
          if (clipped[l] > 16'sd127) activated[l] = 8'sd127;
          else if (clipped[l] < 16'sd0) begin
            // Leaky ReLU: alpha ≈ 0.125 (arithmetic right shift by 3)
            automatic logic signed [15:0] leak = clipped[l] >>> 3;
            if (leak < -16'sd128) activated[l] = -8'sd128;
            else activated[l] = 8'(leak);
          end
          else activated[l] = 8'(clipped[l]);
        end
        default: activated[l] = 8'sd0;
      endcase
    end
  end

  // Output Register
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 1'b0;
      for (int l = 0; l < 4; l++) act_o[l] <= '0;
    end else begin
      valid_o <= valid_i;
      if (valid_i) begin
        for (int l = 0; l < 4; l++) act_o[l] <= activated[l];
      end
    end
  end

  assign ready_o = 1'b1;

endmodule
