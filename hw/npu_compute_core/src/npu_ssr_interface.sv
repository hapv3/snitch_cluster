// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_ssr_interface import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  // Stream Configuration (from Control Core / memory mapped)
  input  logic cfg_valid_i,
  input  logic [NpuTcdmAddrWidth-1:0] cfg_base_addr_i,
  input  logic [NpuTcdmAddrWidth-1:0] cfg_stride_1d_i,
  input  logic [NpuTcdmAddrWidth-1:0] cfg_stride_2d_i,
  input  logic [15:0] cfg_bound_1d_i,
  input  logic [15:0] cfg_bound_2d_i,

  // Loop control trigger (from FREP)
  input  logic loop_step_i,
  output logic stream_done_o,

  // TCDM Read Request Interface
  output logic tcdm_req_o,
  output logic [NpuTcdmAddrWidth-1:0] tcdm_addr_o,
  input  logic tcdm_gnt_i,

  // TCDM Read Response Interface
  input  logic tcdm_rvalid_i,
  input  logic [NpuTcdmDataWidth-1:0] tcdm_rdata_i,

  // Stream Output to Compute Datapath
  output logic stream_valid_o,
  output logic [NpuTcdmDataWidth-1:0] stream_data_o,
  input  logic stream_ready_i
);

  // Counters for 2D loop
  logic [15:0] cnt_1d_q, cnt_1d_n;
  logic [15:0] cnt_2d_q, cnt_2d_n;
  logic [NpuTcdmAddrWidth-1:0] current_addr_q, current_addr_n;

  // Next state logic for address generation
  always_comb begin
    cnt_1d_n = cnt_1d_q;
    cnt_2d_n = cnt_2d_q;
    current_addr_n = current_addr_q;
    stream_done_o = 1'b0;

    if (cfg_valid_i) begin
      cnt_1d_n = '0;
      cnt_2d_n = '0;
      current_addr_n = cfg_base_addr_i;
    end else if (loop_step_i && tcdm_gnt_i) begin
      if (cnt_1d_q == cfg_bound_1d_i - 1) begin
        cnt_1d_n = '0;
        if (cnt_2d_q == cfg_bound_2d_i - 1) begin
          // End of 2D stream
          stream_done_o = 1'b1;
          cnt_2d_n = '0;
          current_addr_n = cfg_base_addr_i; // Reset or halt
        end else begin
          cnt_2d_n = cnt_2d_q + 1;
          current_addr_n = current_addr_q + cfg_stride_2d_i;
        end
      end else begin
        cnt_1d_n = cnt_1d_q + 1;
        current_addr_n = current_addr_q + cfg_stride_1d_i;
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cnt_1d_q <= '0;
      cnt_2d_q <= '0;
      current_addr_q <= '0;
    end else begin
      cnt_1d_q <= cnt_1d_n;
      cnt_2d_q <= cnt_2d_n;
      current_addr_q <= current_addr_n;
    end
  end

  // Memory interface
  assign tcdm_req_o  = loop_step_i;
  assign tcdm_addr_o = current_addr_q;

  // Response buffering (simplification: 1 cycle latency assumed, no complex FIFO for now)
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      stream_valid_o <= 1'b0;
      stream_data_o  <= '0;
    end else begin
      stream_valid_o <= tcdm_rvalid_i;
      if (tcdm_rvalid_i) begin
        stream_data_o <= tcdm_rdata_i;
      end
    end
  end

endmodule
