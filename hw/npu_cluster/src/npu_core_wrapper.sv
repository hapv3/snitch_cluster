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
  input  logic [TcdmDataWidth-1:0]tcdm_rsp_rdata_i
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

  // Core signals
  logic core_valid;
  logic core_clear_acc;
  logic core_ready;
  logic core_out_valid;
  logic [31:0] core_out_act;

  core_cfg_t core_cfg;
  assign core_cfg.act_type = ACT_RELU;
  assign core_cfg.output_scale = 1;
  assign core_cfg.output_zero_point = 0;
  assign core_cfg.shift_amount = 0;

  logic [7:0] slide_idx_q, slide_idx_d;
  logic signed [7:0] act_buf_shifted [128];
  
  always_comb begin
    for (int i = 0; i < 128; i++) begin
      automatic int offset = i + slide_idx_q * reg_stride_slide[7:0];
      if (offset < 128)
        act_buf_shifted[i] = act_buf[offset];
      else
        act_buf_shifted[i] = 0;
    end
  end

  npu_compute_core i_core (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .cfg_i(core_cfg),
    .valid_i(core_valid),
    .clear_acc_i(core_clear_acc),
    .act_i(act_buf_shifted),
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
  logic [7:0]  fetch_cnt_q, fetch_cnt_d; // up to 32 words (128 bytes)

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
            default: mmio_rsp_data_o <= 32'hDEADBEEF;
          endcase
          $display("[RTL Trace] Core %0d Read Addr: %x, Data: %x", CoreId, mmio_req_addr_i, mmio_rsp_data_o);
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
        if (fetch_cnt_q < 32) begin
          tcdm_req_valid_o = 1;
          tcdm_req_addr_o = act_ptr_q;
          if (tcdm_req_ready_i) begin
            act_ptr_d = act_ptr_q + 4;
            // For simplicity, we assume TCDM responds in the very next cycle.
            // We increment fetch_cnt when we ISSUE the request.
            fetch_cnt_d = fetch_cnt_q + 1;
          end
        end else begin
          state_d = FETCH_WGT;
          fetch_cnt_d = 0;
        end
      end

      FETCH_WGT: begin
        if (fetch_cnt_q < 32) begin
          tcdm_req_valid_o = 1;
          tcdm_req_addr_o = wgt_ptr_q;
          if (tcdm_req_ready_i) begin
            wgt_ptr_d = wgt_ptr_q + 4;
            fetch_cnt_d = fetch_cnt_q + 1;
          end
        end else begin
          // Both buffers filled (data will arrive on the next cycle for the last request)
          // Move to compute.
          state_d = COMPUTE;
        end
      end

      COMPUTE: begin
        // The last read response from FETCH_WGT arrives now or earlier.
        // Fire the MAC array for 1 cycle.
        core_valid = 1;
        core_clear_acc = reg_ctrl[1]; // bit 1 indicates clear accumulator
        if (core_ready) begin
          state_d = WAIT_OUT;
        end
      end

      WAIT_OUT: begin
        if (core_out_valid) begin
          if (reg_ctrl[2]) begin
            state_d = WRITE_OUT;
          end else begin
            state_d = IDLE;
          end
        end
      end

      WRITE_OUT: begin
        tcdm_req_valid_o = 1;
        tcdm_req_write_o = 1;
        tcdm_req_be_o = 4'hF;
        tcdm_req_addr_o = reg_out_ptr + slide_idx_q * 4;
        tcdm_req_wdata_o = core_out_act;
        if (tcdm_req_ready_i) begin
          automatic logic [7:0] max_slides = reg_stride_slide[15:8];
          if (max_slides == 0) max_slides = 1;
          
          if (slide_idx_q < max_slides - 1) begin
             slide_idx_d = slide_idx_q + 1;
             state_d = COMPUTE;
          end else begin
             state_d = IDLE;
          end
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
      // Unpack 32-bit word into four 8-bit elements
      if (state_q_d == FETCH_ACT) begin
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
    end else begin
      state_q <= state_d;
      act_ptr_q <= act_ptr_d;
      wgt_ptr_q <= wgt_ptr_d;
      fetch_cnt_q <= fetch_cnt_d;
      slide_idx_q <= slide_idx_d;
    end
  end

endmodule
