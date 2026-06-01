// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

`include "npu_compute_core_pkg.sv"

module npu_frep_controller import npu_compute_core_pkg::*; (
  input  logic clk_i,
  input  logic rst_ni,

  // Loop Configuration (from Control Core)
  input  logic cfg_valid_i,
  input  logic [15:0] cfg_loop_iterations_i, // Number of times to execute the block
  input  logic [7:0]  cfg_block_size_i,      // Instructions/steps per loop iteration

  // Compute Core handshakes
  output logic loop_active_o,
  output logic step_trigger_o,
  input  logic step_ready_i,

  // Status
  output logic loop_done_o
);

  logic [15:0] iter_cnt_q, iter_cnt_n;
  logic [7:0]  block_cnt_q, block_cnt_n;
  logic active_q, active_n;

  assign loop_active_o = active_q;
  assign step_trigger_o = active_q && step_ready_i;

  always_comb begin
    iter_cnt_n = iter_cnt_q;
    block_cnt_n = block_cnt_q;
    active_n = active_q;
    loop_done_o = 1'b0;

    if (cfg_valid_i) begin
      active_n = 1'b1;
      iter_cnt_n = cfg_loop_iterations_i;
      block_cnt_n = cfg_block_size_i;
    end else if (active_q && step_ready_i) begin
      if (block_cnt_q == 8'd1) begin
        // Block completed
        if (iter_cnt_q == 16'd1) begin
          // All iterations completed
          active_n = 1'b0;
          loop_done_o = 1'b1;
        end else begin
          // Next iteration
          iter_cnt_n = iter_cnt_q - 1;
          block_cnt_n = cfg_block_size_i; // reload block size
        end
      end else begin
        block_cnt_n = block_cnt_q - 1;
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      iter_cnt_q  <= '0;
      block_cnt_q <= '0;
      active_q    <= 1'b0;
    end else begin
      iter_cnt_q  <= iter_cnt_n;
      block_cnt_q <= block_cnt_n;
      active_q    <= active_n;
    end
  end

endmodule
