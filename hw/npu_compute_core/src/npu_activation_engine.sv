// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_activation_engine import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  input  core_cfg_t cfg_i,

  input  logic valid_i,
  input  logic signed [NpuAccWidth-1:0] acc_i,
  output logic ready_o,

  output logic valid_o,
  output logic [NpuActWidth-1:0] act_o
);

  logic signed [NpuAccWidth-1:0] requantized;
  logic signed [15:0] clipped;
  logic [NpuActWidth-1:0] activated;

  // 1. Requantization (Multiply-Shift-Saturate)
  always_comb begin
    // Note: hardware implementation of (acc * scale) >> shift + zero_point
    // Simplified for this model. Real RTL will have DSP blocks for the 32x32 multiply.
    automatic logic signed [63:0] scaled = acc_i * $signed(cfg_i.output_scale);
    automatic logic signed [31:0] shifted = scaled >>> cfg_i.shift_amount;
    automatic logic signed [31:0] offset = shifted + $signed(cfg_i.output_zero_point);
    
    // Clip to INT16 before activation
    if (offset > 32'sd32767) clipped = 16'sd32767;
    else if (offset < -32'sd32768) clipped = -16'sd32768;
    else clipped = 16'(offset);
  end

  // 2. Activation Function
  always_comb begin
    activated = '0;
    unique case (cfg_i.act_type)
      ACT_NONE: begin
        if (clipped > 16'sd127) activated = 8'sd127;
        else if (clipped < -16'sd128) activated = -8'sd128;
        else activated = 8'(clipped);
      end
      ACT_RELU: begin
        if (clipped > 16'sd127) activated = 8'sd127;
        else if (clipped < 16'sd0) activated = 8'sd0;
        else activated = 8'(clipped);
      end
      ACT_RELU6: begin
        // Assuming quantization scale maps 6 to a specific int value, e.g. 6/scale.
        // Simplified mapping for ReLU6.
        if (clipped > 16'sd127) activated = 8'sd127; // Requires proper threshold comparison
        else if (clipped < 16'sd0) activated = 8'sd0;
        else activated = 8'(clipped);
      end
      ACT_SIGMOID: begin
        // Placeholder for LUT
        activated = 8'sd0;
      end
      default: activated = 8'sd0;
    endcase
  end

  // Output Register
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 1'b0;
      act_o   <= '0;
    end else begin
      valid_o <= valid_i;
      if (valid_i) begin
        act_o <= activated;
      end
    end
  end

  assign ready_o = 1'b1;

endmodule
