// Copyright 2026 NPU IP
// Hardware MaxPool Engine

`include "npu_compute_core_pkg.sv"

module npu_pooling_engine import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,
  
  input  core_cfg_t cfg_i,
  input  logic valid_i,
  input  logic signed [7:0] act_i [128],
  
  output logic ready_o,
  output logic valid_o,
  output logic signed [7:0] max_o
);

  logic signed [7:0] max_val;
  
  always_comb begin
    logic signed [7:0] m1;
    logic signed [7:0] m2;
    logic signed [7:0] m3;
    logic signed [7:0] m12;
    
    max_val = -8'sd128;
    
    if (cfg_i.kernel_size == 1) begin
      // 1D Pool of 4 elements (Linear test_maxpool compat)
      m1 = (act_i[0] > act_i[1]) ? act_i[0] : act_i[1];
      m2 = (act_i[2] > act_i[3]) ? act_i[2] : act_i[3];
      max_val = (m1 > m2) ? m1 : m2;
      
    end else if (cfg_i.kernel_size == 2) begin
      // 2x2 Pool: act_buf[0..1] from row 0, act_buf[4..5] from row 1
      m1 = (act_i[0] > act_i[1]) ? act_i[0] : act_i[1];
      m2 = (act_i[4] > act_i[5]) ? act_i[4] : act_i[5];
      max_val = (m1 > m2) ? m1 : m2;
      
    end else if (cfg_i.kernel_size == 3) begin
      // 3x3 Pool: act_buf[0..2] row0, act_buf[4..6] row1, act_buf[8..10] row2
      m1 = (act_i[0] > act_i[1]) ? act_i[0] : act_i[1];
      m1 = (m1 > act_i[2]) ? m1 : act_i[2];
      
      m2 = (act_i[4] > act_i[5]) ? act_i[4] : act_i[5];
      m2 = (m2 > act_i[6]) ? m2 : act_i[6];
      
      m3 = (act_i[8] > act_i[9]) ? act_i[8] : act_i[9];
      m3 = (m3 > act_i[10]) ? m3 : act_i[10];
      
      m12 = (m1 > m2) ? m1 : m2;
      max_val = (m12 > m3) ? m12 : m3;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 0;
      max_o <= 0;
    end else begin
      valid_o <= valid_i;
      if (valid_i) max_o <= max_val;
    end
  end
  
  assign ready_o = 1'b1;

endmodule
