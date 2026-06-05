// Copyright 2026 NPU IP
// NPU Compute Core Wrapper
// Buffers 32-bit TCDM reads into 1024-bit vectors to feed the MAC array

`include "npu_compute_core_pkg.sv"

module npu_core_wrapper import npu_compute_core_pkg::*; #(
  parameter int CoreId = 0,
  parameter int TcdmAddrWidth = 32,
  parameter int TcdmDataWidth = 32
)(
  input  logic clk_i,
  input  logic rst_ni,

  // MMIO Control Interface
  input  logic                    mmio_req_valid_i,
  input  logic                    mmio_req_write_i,
  input  logic [31:0]             mmio_req_addr_i,
  input  logic [31:0]             mmio_req_data_i,
  output logic                    mmio_req_ready_o,
  output logic                    mmio_rsp_valid_o,
  output logic [31:0]             mmio_rsp_data_o,

  // TCDM Master Interface
  output logic                    tcdm_req_valid_o,
  output logic                    tcdm_req_write_o,
  output logic [3:0]              tcdm_req_be_o,
  output logic [TcdmAddrWidth-1:0]tcdm_req_addr_o,
  output logic [TcdmDataWidth-1:0]tcdm_req_wdata_o,
  input  logic                    tcdm_req_ready_i,
  input  logic                    tcdm_rsp_valid_i,
  input  logic [TcdmDataWidth-1:0]tcdm_rsp_rdata_i,
  
  // Interrupts
  output logic                    irq_o,
  input  logic                    irq_clear_i
);

  // MMIO Registers
  logic [31:0] reg_status;
  logic [31:0] reg_ctrl;
  logic [31:0] reg_act_ptr;
  logic [31:0] reg_wgt_ptr;
  logic [31:0] reg_out_ptr;
  logic [31:0] reg_stride_slide;

  // Local Buffers
  logic signed [7:0] act_buf [128];
  logic signed [7:0] wgt_buf [128];

  // Performance counters
  logic [31:0] mac_active_cycles;
  logic [31:0] total_cycles;

  // Core signals
  logic core_valid;
  logic core_clear_acc;
  logic core_ready;
  logic core_out_valid;
  logic [31:0] core_out_act;

  core_cfg_t core_cfg;
  assign core_cfg.act_type = act_type_e'(reg_ctrl[10:8]);
  assign core_cfg.output_scale = 1;
  assign core_cfg.output_zero_point = 0;
  assign core_cfg.shift_amount = 0;

  // act_buf_shifted no longer needed, directly pass act_buf to compute core.

  npu_compute_core i_core (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .cfg_i(core_cfg),
    .valid_i(core_valid),
    .clear_acc_i(core_clear_acc),
    .act_i(act_buf),
    .wgt_i(wgt_buf),
    .ready_o(core_ready),
    .valid_o(core_out_valid),
    .act_o(core_out_act),
    .ready_i(1'b1)
  );

  // FSM for fetching
  typedef enum logic [2:0] { IDLE, FETCH_ACT, FETCH_WGT, COMPUTE, WAIT_OUT, WRITE_OUT } state_e;
  state_e state_q, state_d;

  logic [31:0] act_ptr_q, act_ptr_d;
  logic [31:0] wgt_ptr_q, wgt_ptr_d;
  logic [7:0]  fetch_cnt_q, fetch_cnt_d; // up to 32 words
  logic [7:0]  slide_idx_q, slide_idx_d;
  logic [7:0]  out_cnt_q, out_cnt_d;
  logic [31:0] final_out_q, final_out_d;

  // MMIO Logic
  assign mmio_req_ready_o = 1'b1;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      mmio_rsp_valid_o <= 0;
      mmio_rsp_data_o <= 0;
      reg_ctrl <= 0;
      reg_act_ptr <= 0;
      reg_wgt_ptr <= 0;
      reg_out_ptr <= 0;
      reg_stride_slide <= 0;
    end else begin
      mmio_rsp_valid_o <= 0;
      if (mmio_req_valid_i && mmio_req_ready_o) begin
        mmio_rsp_valid_o <= 1;
        if (mmio_req_write_i) begin
          case (mmio_req_addr_i[7:0])
            8'h00: reg_ctrl <= mmio_req_data_i;
            8'h04: reg_act_ptr <= mmio_req_data_i;
            8'h08: reg_wgt_ptr <= mmio_req_data_i;
            8'h0C: reg_out_ptr <= mmio_req_data_i;
            8'h14: reg_stride_slide <= mmio_req_data_i;
          endcase
        end else begin
          case (mmio_req_addr_i[7:0])
            8'h00: mmio_rsp_data_o <= reg_ctrl;
            8'h04: mmio_rsp_data_o <= reg_act_ptr;
            8'h08: mmio_rsp_data_o <= reg_wgt_ptr;
            8'h0C: mmio_rsp_data_o <= reg_out_ptr;
            8'h10: mmio_rsp_data_o <= {31'd0, (state_q != IDLE)};
            8'h14: mmio_rsp_data_o <= reg_stride_slide;
            8'h18: mmio_rsp_data_o <= mac_active_cycles;
            8'h1C: mmio_rsp_data_o <= total_cycles;
            default: mmio_rsp_data_o <= 32'hDEADBEEF;
          endcase
        end
      end
      
      // Auto-clear trigger
      if (reg_ctrl[0] && state_q != IDLE) begin
        reg_ctrl[0] <= 0;
      end
    end
  end

  // FSM Logic
  always_comb begin
    state_d = state_q;
    act_ptr_d = act_ptr_q;
    wgt_ptr_d = wgt_ptr_q;
    fetch_cnt_d = fetch_cnt_q;
    slide_idx_d = slide_idx_q;
    
    tcdm_req_valid_o = 0;
    tcdm_req_write_o = 0;
    tcdm_req_be_o = 4'hF;
    tcdm_req_addr_o = 0;
    tcdm_req_wdata_o = 0;
    
    core_valid = 0;
    core_clear_acc = 0;

    case (state_q)
      IDLE: begin
        if (reg_ctrl[0]) begin
          state_d = FETCH_ACT;
          act_ptr_d = reg_act_ptr;
          wgt_ptr_d = reg_wgt_ptr;
          fetch_cnt_d = 0;
          slide_idx_d = 0;
        end
      end

      FETCH_ACT: begin
        if (fetch_cnt_q < 8) begin // 32 bytes
          tcdm_req_valid_o = 1;
          tcdm_req_addr_o = act_ptr_q;
          if (tcdm_req_ready_i) begin
            act_ptr_d = act_ptr_q + 4;
            fetch_cnt_d = fetch_cnt_q + 1;
          end
        end else begin
          state_d = FETCH_WGT;
          fetch_cnt_d = 0;
        end
      end

      FETCH_WGT: begin
        if (fetch_cnt_q < 32) begin // 128 bytes
          tcdm_req_valid_o = 1;
          tcdm_req_addr_o = wgt_ptr_q;
          if (tcdm_req_ready_i) begin
            wgt_ptr_d = wgt_ptr_q + 4;
            fetch_cnt_d = fetch_cnt_q + 1;
          end
        end else begin
          state_d = COMPUTE;
        end
      end

      COMPUTE: begin
        $display("[%0t] [COMPUTE] slide=%0d A[0]=%x A[1]=%x B[0]=%x B[1]=%x B[32]=%x B[64]=%x B[96]=%x", $time, slide_idx_q, act_buf[0], act_buf[1], wgt_buf[0], wgt_buf[1], wgt_buf[32], wgt_buf[64], wgt_buf[96]);
        core_valid = 1;
        core_clear_acc = (slide_idx_q == 0); // clear acc ONLY on first slide
        if (core_ready) begin
          automatic logic [7:0] max_slides = reg_stride_slide[7:0];
          if (max_slides == 0) max_slides = 1;
          
          if (slide_idx_q < max_slides - 1) begin
            slide_idx_d = slide_idx_q + 1;
            fetch_cnt_d = 0;
            state_d = FETCH_ACT; // go fetch next block
          end else begin
            state_d = WAIT_OUT;
          end
        end
      end

      WAIT_OUT: begin
        // We wait here until out_cnt_q == max_slides.
        // Handled in sequential logic below.
        automatic logic [7:0] max_slides = reg_stride_slide[7:0];
        if (max_slides == 0) max_slides = 1;
        if (out_cnt_q == max_slides) begin
          state_d = WRITE_OUT;
        end
      end

      WRITE_OUT: begin
        tcdm_req_valid_o = 1;
        tcdm_req_write_o = 1;
        tcdm_req_be_o = 4'hF;
        tcdm_req_addr_o = reg_out_ptr;
        tcdm_req_wdata_o = final_out_q;
        if (tcdm_req_ready_i) begin
          state_d = IDLE;
        end
      end

      default: state_d = IDLE;
    endcase
  end
  
  // Data reception logic (pipelined from TCDM)
  logic [2:0] state_q_d;
  logic [7:0] fetch_cnt_q_d;
  always_ff @(posedge clk_i) begin
    if (tcdm_req_valid_o && tcdm_req_ready_i) begin
      state_q_d <= state_q;
      fetch_cnt_q_d <= fetch_cnt_q;
    end
  end

  always_ff @(posedge clk_i) begin
    if (tcdm_rsp_valid_i) begin
      if (state_q_d == FETCH_ACT) begin
        $display("[%0t] [FETCH_ACT] cnt=%0d rdata=%x", $time, fetch_cnt_q_d, tcdm_rsp_rdata_i);
        act_buf[fetch_cnt_q_d*4 + 0] <= tcdm_rsp_rdata_i[7:0];
        act_buf[fetch_cnt_q_d*4 + 1] <= tcdm_rsp_rdata_i[15:8];
        act_buf[fetch_cnt_q_d*4 + 2] <= tcdm_rsp_rdata_i[23:16];
        act_buf[fetch_cnt_q_d*4 + 3] <= tcdm_rsp_rdata_i[31:24];
      end else if (state_q_d == FETCH_WGT) begin
        wgt_buf[fetch_cnt_q_d*4 + 0] <= tcdm_rsp_rdata_i[7:0];
        wgt_buf[fetch_cnt_q_d*4 + 1] <= tcdm_rsp_rdata_i[15:8];
        wgt_buf[fetch_cnt_q_d*4 + 2] <= tcdm_rsp_rdata_i[23:16];
        wgt_buf[fetch_cnt_q_d*4 + 3] <= tcdm_rsp_rdata_i[31:24];
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= IDLE;
      act_ptr_q <= 0;
      wgt_ptr_q <= 0;
      fetch_cnt_q <= 0;
      slide_idx_q <= 0;
      out_cnt_q <= 0;
      final_out_q <= 0;
    end else begin
      state_q <= state_d;
      act_ptr_q <= act_ptr_d;
      wgt_ptr_q <= wgt_ptr_d;
      fetch_cnt_q <= fetch_cnt_d;
      slide_idx_q <= slide_idx_d;
      
      if (state_q == IDLE && state_d == FETCH_ACT) begin
        out_cnt_q <= 0;
      end else if (core_out_valid) begin
        out_cnt_q <= out_cnt_q + 1;
        final_out_q <= core_out_act; // capture the latest valid output
      end
    end
  end

  // Performance counters
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      mac_active_cycles <= 0;
      total_cycles <= 0;
    end else begin
      total_cycles <= total_cycles + 1;
      if (core_valid && core_ready) begin
        mac_active_cycles <= mac_active_cycles + 1;
      end
    end
  end

  // Interrupt logic
  logic irq_q, irq_d;
  assign irq_o = irq_q;

  always_comb begin
    irq_d = irq_q;
    if (irq_clear_i) begin
      irq_d = 1'b0;
    end else if (state_q != IDLE && state_d == IDLE) begin
      irq_d = 1'b1;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) irq_q <= 1'b0;
    else irq_q <= irq_d;
  end

endmodule
