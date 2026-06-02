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

  // 256-entry Activation LUTs (input INT16 clamped to [-128, 127], mapped to [0, 255])
  // Scale = 0.0625
  localparam logic signed [7:0] lut_sigmoid [256] = '{
    -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -126, -126, -126, -126, -126, -126, -126, -126, -126, -125, -125, -125, -125, -125, -124, -124, -124, -124, -123, -123, -123, -122, -122, -122, -121, -121, -121, -120, -120, -119, -118, -118, -117, -117, -116, -115, -114, -114, -113, -112, -111, -110, -109, -108, -106, -105, -104, -102, -101, -99, -98, -96, -94, -92, -90, -88, -86, -84, -81, -79, -77, -74, -71, -68, -66, -63, -59, -56, -53, -50, -46, -43, -39, -35, -32, -28, -24, -20, -16, -12, -8, -4, 0, 3, 7, 11, 15, 19, 23, 27, 31, 34, 38, 42, 45, 49, 52, 55, 58, 62, 65, 67, 70, 73, 76, 78, 80, 83, 85, 87, 89, 91, 93, 95, 97, 98, 100, 101, 103, 104, 105, 107, 108, 109, 110, 111, 112, 113, 113, 114, 115, 116, 116, 117, 117, 118, 119, 119, 120, 120, 120, 121, 121, 121, 122, 122, 122, 123, 123, 123, 123, 124, 124, 124, 124, 124, 125, 125, 125, 125, 125, 125, 125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127
  };
  
  localparam logic signed [7:0] lut_silu [256] = '{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -2, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127
  };
  
  localparam logic signed [7:0] lut_mish [256] = '{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -4, -4, -4, -4, -4, -3, -3, -2, -2, -2, -1, -1, 0, 1, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127
  };

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
          automatic logic [7:0] index;
          if (clipped[l] > 16'sd127) index = 8'd255;
          else if (clipped[l] < -16'sd128) index = 8'd0;
          else index = 8'(clipped[l] + 16'sd128);
          activated[l] = lut_sigmoid[index];
        end
        ACT_SILU: begin
          automatic logic [7:0] index;
          if (clipped[l] > 16'sd127) index = 8'd255;
          else if (clipped[l] < -16'sd128) index = 8'd0;
          else index = 8'(clipped[l] + 16'sd128);
          activated[l] = lut_silu[index];
        end
        ACT_MISH: begin
          automatic logic [7:0] index;
          if (clipped[l] > 16'sd127) index = 8'd255;
          else if (clipped[l] < -16'sd128) index = 8'd0;
          else index = 8'(clipped[l] + 16'sd128);
          activated[l] = lut_mish[index];
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
