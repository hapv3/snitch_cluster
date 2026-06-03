// Copyright 2026 NPU IP
// 2D Direct Memory Access (DMA) Engine for TCDM ↔ AXI4 Transfers

module npu_dma_engine #(
  parameter int AxiDataWidth = 128,
  parameter int AxiAddrWidth = 32,
  parameter int TcdmDataWidth = 32,
  parameter int TcdmAddrWidth = 32
)(
  input  logic clk_i,
  input  logic rst_ni,

  // Control Interface (MMIO from RISC-V)
  input  logic                     ctrl_req_valid_i,
  input  logic                     ctrl_req_write_i,
  input  logic [TcdmAddrWidth-1:0] ctrl_req_addr_i,
  input  logic [31:0]              ctrl_req_data_i,
  output logic                     ctrl_req_ready_o,
  output logic                     ctrl_rsp_valid_o,
  output logic [31:0]              ctrl_rsp_data_o,

  // TCDM Master Interface (To Interconnect)
  output logic                     tcdm_req_valid_o,
  output logic                     tcdm_req_write_o,
  output logic [3:0]               tcdm_req_be_o,
  output logic [TcdmAddrWidth-1:0] tcdm_req_addr_o,
  output logic [TcdmDataWidth-1:0] tcdm_req_wdata_o,
  input  logic                     tcdm_req_ready_i,
  input  logic                     tcdm_rsp_valid_i,
  input  logic [TcdmDataWidth-1:0] tcdm_rsp_rdata_i,

  // AXI4 Master Interface (To Main Memory)
  // Read Address Channel
  output logic                     axi_ar_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_ar_addr_o,
  output logic [7:0]               axi_ar_len_o,
  output logic [2:0]               axi_ar_size_o,
  input  logic                     axi_ar_ready_i,
  // Read Data Channel
  input  logic                     axi_r_valid_i,
  input  logic [AxiDataWidth-1:0]  axi_r_data_i,
  input  logic                     axi_r_last_i,
  output logic                     axi_r_ready_o,
  // Write Address Channel
  output logic                     axi_aw_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_aw_addr_o,
  output logic [7:0]               axi_aw_len_o,
  output logic [2:0]               axi_aw_size_o,
  input  logic                     axi_aw_ready_i,
  // Write Data Channel
  output logic                     axi_w_valid_o,
  output logic [AxiDataWidth-1:0]  axi_w_data_o,
  output logic [(AxiDataWidth/8)-1:0] axi_w_strb_o,
  output logic                     axi_w_last_o,
  input  logic                     axi_w_ready_i,
  // Write Response Channel
  input  logic                     axi_b_valid_i,
  output logic                     axi_b_ready_o
);

  // ----------------------------------------------------------------------
  // Memory Mapped Registers
  // ----------------------------------------------------------------------
  // 0x00: SRC_ADDR
  // 0x04: DST_ADDR
  // 0x08: DIM_X (bytes per line)
  // 0x0C: DIM_Y (lines)
  // 0x10: STRIDE_SRC (byte offset between lines)
  // 0x14: STRIDE_DST (byte offset between lines)
  // 0x18: TRIGGER (Write 1 to start)
  // 0x1C: STATUS (Bit 0: busy)
  
  logic [31:0] reg_src_addr, reg_dst_addr;
  logic [31:0] reg_dim_x, reg_dim_y;
  logic [31:0] reg_stride_src, reg_stride_dst;
  logic        reg_trigger;
  logic        busy_q;

  // Performance counters
  logic [31:0] dma_read_count;
  logic [31:0] dma_write_count;

  assign ctrl_req_ready_o = !ctrl_rsp_valid_o;
  
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      ctrl_rsp_valid_o <= 1'b0;
      ctrl_rsp_data_o  <= '0;
      reg_src_addr <= '0;
      reg_dst_addr <= '0;
      reg_dim_x <= '0;
      reg_dim_y <= '0;
      reg_stride_src <= '0;
      reg_stride_dst <= '0;
      reg_trigger <= 1'b0;
    end else begin
      if (state_q == TCDM_W || state_d == TCDM_W) $display("[%0t] [DMA_DEBUG] state=%d fifo=%b vld_o=%b rdy_i=%b addr=%x", $time, state_q, fifo_valid, tcdm_req_valid_o, tcdm_req_ready_i, tcdm_req_addr_o);
      reg_trigger <= 1'b0; // Auto-clear trigger
      
      if (ctrl_rsp_valid_o) begin
        ctrl_rsp_valid_o <= 1'b0;
      end
      
      if (ctrl_req_valid_i && ctrl_req_ready_o) begin
        ctrl_rsp_valid_o <= 1'b1;
        if (ctrl_req_write_i) begin
          case (ctrl_req_addr_i[5:2])
            4'h0: reg_src_addr <= ctrl_req_data_i;
            4'h1: reg_dst_addr <= ctrl_req_data_i;
            4'h2: reg_dim_x <= ctrl_req_data_i;
            4'h3: reg_dim_y <= ctrl_req_data_i;
            4'h4: reg_stride_src <= ctrl_req_data_i;
            4'h5: reg_stride_dst <= ctrl_req_data_i;
            4'h6: begin 
                begin reg_trigger <= ctrl_req_data_i[0]; $display("[%0t] [DMA_TRIGGER] src=%x dst=%x dim_x=%d", $time, reg_src_addr, reg_dst_addr, reg_dim_x); end
                $display("[%0t] [DMA_REG] TRIGGER Written! data=%b", $time, ctrl_req_data_i[0]);
            end
          endcase
        end else begin
      if (state_q == TCDM_W || state_d == TCDM_W) $display("[%0t] [DMA_DEBUG] state=%d fifo=%b vld_o=%b rdy_i=%b addr=%x", $time, state_q, fifo_valid, tcdm_req_valid_o, tcdm_req_ready_i, tcdm_req_addr_o);
          case (ctrl_req_addr_i[5:2])
            4'h0: ctrl_rsp_data_o <= reg_src_addr;
            4'h1: ctrl_rsp_data_o <= reg_dst_addr;
            4'h2: ctrl_rsp_data_o <= reg_dim_x;
            4'h3: ctrl_rsp_data_o <= reg_dim_y;
            4'h4: ctrl_rsp_data_o <= reg_stride_src;
            4'h5: ctrl_rsp_data_o <= reg_stride_dst;
            4'h7: begin
                ctrl_rsp_data_o <= {31'd0, busy_q};
                $display("[%0t] [DMA_REG] Read STATUS: busy_q=%b state_q=%d", $time, busy_q, state_q);
            end
            4'h8: ctrl_rsp_data_o <= dma_read_count;
            4'h9: ctrl_rsp_data_o <= dma_write_count;
            default: ctrl_rsp_data_o <= '0;
          endcase
        end
      end
    end
  end

  // ----------------------------------------------------------------------
  // DMA State Machine (Simplified block copy for TCDM <-> AXI)
  // Assuming L2 to TCDM transfers for now (Ext -> Int). 
  // Bi-directional would check if SRC/DST is in TCDM memory range.
  // ----------------------------------------------------------------------
  typedef enum logic [2:0] {
    IDLE,
    AXI_AR,
    AXI_R,
    TCDM_W,
    NEXT_LINE,
    DONE
  } dma_state_e;
  
  dma_state_e state_q, state_d;
  
  logic [31:0] curr_src_q, curr_src_d;
  logic [31:0] curr_dst_q, curr_dst_d;
  logic [31:0] x_rem_q, x_rem_d;
  logic [31:0] y_rem_q, y_rem_d;
  
  // Data FIFO to decouple AXI Read and TCDM Write
  logic [AxiDataWidth-1:0] fifo_data;
  logic fifo_valid, fifo_ready;
  // A simple 1-entry buffer for demonstration (a real DMA needs a deeper async/sync FIFO)
  
  assign busy_q = (state_q != IDLE);
  
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= IDLE;
      curr_src_q <= '0;
      curr_dst_q <= '0;
      x_rem_q <= '0;
      y_rem_q <= '0;
      fifo_valid <= 1'b0;
      fifo_data <= '0;
      dma_read_count <= '0;
      dma_write_count <= '0;
    end else begin
      if (state_q == TCDM_W || state_d == TCDM_W) $display("[%0t] [DMA_DEBUG] state=%d fifo=%b vld_o=%b rdy_i=%b addr=%x", $time, state_q, fifo_valid, tcdm_req_valid_o, tcdm_req_ready_i, tcdm_req_addr_o);
      state_q <= state_d;
      curr_src_q <= curr_src_d;
      curr_dst_q <= curr_dst_d;
      x_rem_q <= x_rem_d;
      y_rem_q <= y_rem_d;
      
      // Simple FIFO control
      if (axi_r_valid_i && axi_r_ready_o) begin
        fifo_valid <= 1'b1;
        fifo_data <= axi_r_data_i;
        dma_read_count <= dma_read_count + 1;
      end else if (tcdm_req_valid_o && tcdm_req_ready_i) begin
        fifo_valid <= 1'b0;
        if (tcdm_req_write_o)
          dma_write_count <= dma_write_count + 1;
      end
    end
  end

  always_comb begin
    state_d = state_q;
    curr_src_d = curr_src_q;
    curr_dst_d = curr_dst_q;
    x_rem_d = x_rem_q;
    y_rem_d = y_rem_q;
    
    // AXI Defaults
    axi_ar_valid_o = 1'b0;
    axi_ar_addr_o = curr_src_q;
    axi_ar_len_o = 8'd0; // Single beat for simplicity right now
    axi_ar_size_o = 3'b100; // 16 bytes = 128 bits
    axi_r_ready_o = 1'b0;
    
    axi_aw_valid_o = 1'b0;
    axi_aw_addr_o = '0;
    axi_aw_len_o = '0;
    axi_aw_size_o = '0;
    axi_w_valid_o = 1'b0;
    axi_w_data_o = '0;
    axi_w_strb_o = '0;
    axi_w_last_o = 1'b0;
    axi_b_ready_o = 1'b1;
    
    // TCDM Defaults
    tcdm_req_valid_o = 1'b0;
    tcdm_req_write_o = 1'b0;
    tcdm_req_addr_o = curr_dst_q;
    tcdm_req_wdata_o = fifo_data[31:0]; // Cast down for simplicity
    tcdm_req_be_o = 4'hF;
    
    case (state_q)
      IDLE: begin
        if (reg_trigger) begin
          curr_src_d = reg_src_addr;
          curr_dst_d = reg_dst_addr;
          x_rem_d = reg_dim_x;
          y_rem_d = reg_dim_y;
          state_d = AXI_AR;
        end
      end
      
      AXI_AR: begin
        if (x_rem_q > 0) begin
          axi_ar_valid_o = 1'b1;
          if (axi_ar_ready_i) begin
            state_d = AXI_R;
          end
        end else begin
      if (state_q == TCDM_W || state_d == TCDM_W) $display("[%0t] [DMA_DEBUG] state=%d fifo=%b vld_o=%b rdy_i=%b addr=%x", $time, state_q, fifo_valid, tcdm_req_valid_o, tcdm_req_ready_i, tcdm_req_addr_o);
          state_d = NEXT_LINE;
        end
      end
      
      AXI_R: begin
        if (!fifo_valid) begin
          axi_r_ready_o = 1'b1;
          if (axi_r_valid_i) begin
            state_d = TCDM_W;
          end
        end
      end
      
      TCDM_W: begin
        if (fifo_valid) begin
          tcdm_req_valid_o = 1'b1;
          tcdm_req_write_o = 1'b1;
          if (tcdm_req_ready_i) begin
            // Advance pointers
            // Note: simple implementation moves 4 bytes (32 bits) at a time
            curr_src_d = curr_src_q + 4; 
            curr_dst_d = curr_dst_q + 4;
            x_rem_d = x_rem_q - 4;
            state_d = AXI_AR;
          end
        end
      end
      
      NEXT_LINE: begin
        if (y_rem_q > 1) begin
          y_rem_d = y_rem_q - 1;
          x_rem_d = reg_dim_x;
          // Apply strides (subtracting what was already added during the line)
          curr_src_d = curr_src_q - reg_dim_x + reg_stride_src;
          curr_dst_d = curr_dst_q - reg_dim_x + reg_stride_dst;
          state_d = AXI_AR;
        end else begin
      if (state_q == TCDM_W || state_d == TCDM_W) $display("[%0t] [DMA_DEBUG] state=%d fifo=%b vld_o=%b rdy_i=%b addr=%x", $time, state_q, fifo_valid, tcdm_req_valid_o, tcdm_req_ready_i, tcdm_req_addr_o);
          state_d = DONE;
        end
      end
      
      DONE: begin
        state_d = IDLE;
      end
      
      default: state_d = IDLE;
    endcase
  end

  // ====================================================================
  // SystemVerilog Assertions (SVA) for AXI4 Master compliance
  // ====================================================================
  `ifndef VERILATOR
  // Property: AXI ARVALID must not be dropped until ARREADY is asserted
  property p_axi_ar_hold;
    @(posedge clk_i) disable iff (!rst_ni)
    (axi_ar_valid_o && !axi_ar_ready_i) |=> (axi_ar_valid_o && $stable(axi_ar_addr_o));
  endproperty
  assert property (p_axi_ar_hold) else $error("AXI AR dropped before ready");

  // Property: AXI RREADY should be high when able to accept data
  property p_axi_r_handshake;
    @(posedge clk_i) disable iff (!rst_ni)
    (axi_r_valid_i && axi_r_ready_o) |=> !fifo_valid; // Basic check for our simple fifo logic
  endproperty
  assert property (p_axi_r_handshake) else $error("AXI R handshake dropped");
  `endif

endmodule
