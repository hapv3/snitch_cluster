// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// NPU Control Core
/// Integrates the RISC-V Snitch Core, I-SPM (Boot ROM + SRAM), and Command Mailbox.
/// The Control Core acts as the dispatcher for the 10 Compute Cores in the cluster,
/// receiving tasks from the ARM host via the Mailbox, programming the DMA to move data,
/// and dispatching execution commands to the MAC arrays.

`include "snitch/typedef.svh"
import snitch_pkg::*;

module npu_control_core #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32
) (
  input  logic clk_i,
  input  logic rst_ni,

  input  logic [31:0] hart_id_i,
  
  // Interface from ARM Host to Mailbox (AXI4-Lite proxy)
  input  logic                 host_req_valid_i,
  input  logic                 host_req_write_i,
  input  logic [AddrWidth-1:0] host_req_addr_i,
  input  logic [DataWidth-1:0] host_req_data_i,
  output logic                 host_req_ready_o,
  
  output logic                 host_rsp_valid_o,
  output logic [DataWidth-1:0] host_rsp_data_o,
  input  logic                 host_rsp_ready_i,

  // Interface to Cluster Data/DMA Interconnect
  output logic                 tcdm_req_valid_o,
  output logic                 tcdm_req_write_o,
  output logic [AddrWidth-1:0] tcdm_req_addr_o,
  output logic [DataWidth-1:0] tcdm_req_data_o,
  input  logic                 tcdm_req_ready_i,
  
  input  logic                 tcdm_rsp_valid_i,
  input  logic [DataWidth-1:0] tcdm_rsp_data_i,
  
  // Interrupt to ARM Host
  output logic                 irq_to_host_o,

  // Firmware Load Port (from Testbench / Host DMA)
  input  logic                 fw_req_valid_i,
  input  logic                 fw_req_write_i,
  input  logic [AddrWidth-1:0] fw_req_addr_i,
  input  logic [DataWidth-1:0] fw_req_data_i,
  output logic                 fw_req_ready_o,
  output logic                 fw_rsp_valid_o,
  output logic [DataWidth-1:0] fw_rsp_data_o,
  input  logic                 fw_rsp_ready_i
);

  typedef `SNITCH_INSTR_REQ_STRUCT(AddrWidth) ireq_t;
  typedef `SNITCH_INSTR_RSP_STRUCT irsp_t;
  typedef `SNITCH_DATA_REQ_STRUCT(DataWidth, AddrWidth) dreq_t;
  typedef `SNITCH_DATA_RSP_STRUCT(DataWidth) drsp_t;


  // Define instruction memory interface (I-SPM)
  logic                 ispm_req_valid;
  logic [AddrWidth-1:0] ispm_req_addr;
  logic                 ispm_req_ready;
  logic                 ispm_rsp_valid;
  logic [DataWidth-1:0] ispm_rsp_data;

  // Define mailbox interface for Core
  logic                 mbox_req_valid;
  logic                 mbox_req_write;
  logic [AddrWidth-1:0] mbox_req_addr;
  logic [DataWidth-1:0] mbox_req_data;
  logic                 mbox_req_ready;
  logic                 mbox_rsp_valid;
  logic [DataWidth-1:0] mbox_rsp_data;
  
  logic irq_from_mbox;

  // I-SPM and Boot ROM Instantiation
  npu_ispm #(
    .AddrWidth (AddrWidth),
    .DataWidth (DataWidth)
  ) i_ispm (
    .clk_i              (clk_i),
    .rst_ni             (rst_ni),
    .fetch_req_valid_i  (ispm_req_valid),
    .fetch_req_addr_i   (ispm_req_addr),
    .fetch_req_ready_o  (ispm_req_ready),
    .fetch_rsp_valid_o  (ispm_rsp_valid),
    .fetch_rsp_data_o   (ispm_rsp_data),
    // DMA firmware load port (from Testbench / Host DMA)
    .load_req_valid_i   (fw_req_valid_i),
    .load_req_write_i   (fw_req_write_i),
    .load_req_addr_i    (fw_req_addr_i),
    .load_req_data_i    (fw_req_data_i),
    .load_req_ready_o   (fw_req_ready_o),
    .load_rsp_valid_o   (fw_rsp_valid_o),
    .load_rsp_data_o    (fw_rsp_data_o),
    .load_rsp_ready_i   (fw_rsp_ready_i)
  );

  // Command Mailbox Instantiation
  npu_mailbox #(
    .AddrWidth (AddrWidth),
    .DataWidth (DataWidth)
  ) i_mailbox (
    .clk_i              (clk_i),
    .rst_ni             (rst_ni),
    // Host Interface
    .host_req_valid_i   (host_req_valid_i),
    .host_req_write_i   (host_req_write_i),
    .host_req_addr_i    (host_req_addr_i),
    .host_req_data_i    (host_req_data_i),
    .host_req_ready_o   (host_req_ready_o),
    .host_rsp_valid_o   (host_rsp_valid_o),
    .host_rsp_data_o    (host_rsp_data_o),
    .host_rsp_ready_i   (host_rsp_ready_i),
    // Core Interface
    .core_req_valid_i   (mbox_req_valid),
    .core_req_write_i   (mbox_req_write),
    .core_req_addr_i    (mbox_req_addr),
    .core_req_data_i    (mbox_req_data),
    .core_req_ready_o   (mbox_req_ready),
    .core_rsp_valid_o (mbox_rsp_valid),
    .core_rsp_data_o  (mbox_rsp_data),
    .core_rsp_ready_i (1'b1), // Core always ready for MMIO response
    // IRQs
    .irq_to_core_o      (irq_from_mbox),
    .irq_to_host_o      (irq_to_host_o)
  );

  // Address Demux for Core Data Interface
  logic                 core_data_valid;
  logic                 core_data_write;
  logic [AddrWidth-1:0] core_data_addr;
  logic [DataWidth-1:0] core_data_wdata;
  logic                 core_data_ready;
  logic                 core_data_rsp_valid;
  logic [DataWidth-1:0] core_data_rdata;

  ireq_t inst_req;
  irsp_t inst_rsp;
  dreq_t data_req;
  drsp_t data_rsp;

  assign ispm_req_valid = inst_req.q_valid;
  assign ispm_req_addr  = inst_req.addr;
  assign inst_rsp.q_ready = ispm_req_ready;
  
  assign inst_rsp.data  = ispm_rsp_data;
  assign inst_rsp.error = 1'b0;

  assign core_data_valid = data_req.q_valid;
  assign core_data_write = data_req.q.write;
  assign core_data_addr  = data_req.q.addr;
  assign core_data_wdata = data_req.q.data;
  
  
  assign data_rsp.p_valid = core_data_rsp_valid;
  assign data_rsp.p.data  = core_data_rdata;
  assign data_rsp.p.error = 1'b0;
  assign data_rsp.q_ready = core_data_ready;


  always_comb begin
    // Default assignments
    mbox_req_valid = 1'b0;
    tcdm_req_valid_o = 1'b0;
    core_data_ready = 1'b0;
    core_data_rsp_valid = 1'b0;
    core_data_rdata = '0;

    mbox_req_write = core_data_write;
    mbox_req_addr  = core_data_addr;
    mbox_req_data  = core_data_wdata;
    
    tcdm_req_write_o = core_data_write;
    tcdm_req_addr_o  = core_data_addr;
    tcdm_req_data_o  = core_data_wdata;

    // Address Map: 0x4000_0000 is Mailbox, 0x1000_0000 is TCDM
    if (core_data_valid) begin
      if (core_data_addr[31:28] == 4'h4) begin // Mailbox
        mbox_req_valid = 1'b1;
        core_data_ready = mbox_req_ready;
      end else begin // TCDM
        tcdm_req_valid_o = 1'b1;
        core_data_ready = tcdm_req_ready_i;
      end
    end
    
    // Handled by response buffer
  end

  // Raw responses from MBOX or TCDM
  logic raw_rsp_valid;
  logic [DataWidth-1:0] raw_rsp_data;

  always_comb begin
    raw_rsp_valid = 1'b0;
    raw_rsp_data = '0;
    if (mbox_rsp_valid) begin
      raw_rsp_valid = 1'b1;
      raw_rsp_data = mbox_rsp_data;
    end else if (tcdm_rsp_valid_i) begin
      raw_rsp_valid = 1'b1;
      raw_rsp_data = tcdm_rsp_data_i;
    end
  end

  // Buffer response to handle core's p_ready backpressure
  logic buffered_rsp_valid_q;
  logic [DataWidth-1:0] buffered_rsp_data_q;
  
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      buffered_rsp_valid_q <= 1'b0;
      buffered_rsp_data_q <= '0;
    end else begin
      if (raw_rsp_valid && !data_req.p_ready) begin
        // Buffer the response
        buffered_rsp_valid_q <= 1'b1;
        buffered_rsp_data_q <= raw_rsp_data;
      end else if (buffered_rsp_valid_q && data_req.p_ready) begin
        // Response accepted by core
        buffered_rsp_valid_q <= 1'b0;
      end
    end
  end

  assign core_data_rsp_valid = buffered_rsp_valid_q ? 1'b1 : raw_rsp_valid;
  assign core_data_rdata     = buffered_rsp_valid_q ? buffered_rsp_data_q : raw_rsp_data;
  
  // Dummy block to continue logic

  // Interrupt mapping
  snitch_pkg::interrupts_t irq;
  always_comb begin
    irq = '0;
    irq.meip = irq_from_mbox; // Map Mailbox interrupt to Machine External Interrupt
  end
  snitch #(
    .BootAddr (32'h0000_1000),
    .AddrWidth (AddrWidth),
    .DataWidth (DataWidth),
    .NumIntOutstandingMem (1),
    .NumIntOutstandingLoads (1),
    .VMSupport (0),
    .EnableXif (0)
  ) i_core (
    .clk_i,
    .rst_i           (~rst_ni),
    .hart_id_i,
    .irq_i           (irq),
    .flush_i_valid_o (),
    .flush_i_ready_i (1'b1),
    .inst_req_o      (inst_req),
    .inst_rsp_i      (inst_rsp),
    .acc_req_o       (),
    .acc_rsp_i       ('0),
    .x_issue_req_o      (),
    .x_issue_resp_i     ('0),
    .x_issue_valid_o    (),
    .x_issue_ready_i    (1'b0),
    .x_register_o       (),
    .x_register_valid_o (),
    .x_register_ready_i (1'b0),
    .x_commit_o         (),
    .x_commit_valid_o   (),
    .x_result_i         ('0),
    .x_result_valid_i   (1'b0),
    .x_result_ready_o   (),
    .data_req_o      (data_req),
    .data_rsp_i      (data_rsp),
    .ptw_req_o       (),
    .ptw_rsp_i       ('0),
    .fpu_fmt_mode_o  (),
    .fpu_rnd_mode_o  (),
    .caq_pvalid_i    (1'b0),
    .core_events_o   (),
    .en_copift_o     (),
    .barrier_o       (),
    .barrier_i       (1'b0)
  );

  // Fetch Print
  always_ff @(posedge clk_i) begin
    if (rst_ni) begin
      // $display("[%0t] [PC_TRACE] PC: %x", $time, i_core.pc_q);
      
      if (ispm_req_valid) begin
        // $display("[%0t] [FETCH] Addr: %x", $time, ispm_req_addr); 
      end
      if (core_data_valid && core_data_ready) begin
        $display("[%0t] [LSU_REQ] write=%b addr=%x wdata=%x", $time, core_data_write, core_data_addr, core_data_wdata);
      end else if (core_data_valid && !core_data_ready) begin
        $display("[%0t] [LSU_STALL] addr=%x mbox_req_ready=%b", $time, core_data_addr, mbox_req_ready);
      end
      if (core_data_rsp_valid) begin
        $display("[%0t] [LSU_RSP] rdata=%x", $time, core_data_rdata);
      end
    end
  end

endmodule
