// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// NPU Control Core
/// Integrates the RISC-V Snitch Core, I-SPM (Boot ROM + SRAM), and Command Mailbox.
/// The Control Core acts as the dispatcher for the 10 Compute Cores in the cluster,
/// receiving tasks from the ARM host via the Mailbox, programming the DMA to move data,
/// and dispatching execution commands to the MAC arrays.

module npu_control_core #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32
  // parameter snitch_pkg::isa_cfg_t IsaCfg = '0 // Placeholder for actual IsaCfg
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
  input  logic                 fw_rsp_ready_i,

  // Core Data Port (Driven by Testbench ISS)
  input  logic                 core_req_valid_i,
  input  logic                 core_req_write_i,
  input  logic [AddrWidth-1:0] core_req_addr_i,
  input  logic [DataWidth-1:0] core_req_data_i,
  output logic                 core_req_ready_o,
  output logic                 core_rsp_valid_o,
  output logic [DataWidth-1:0] core_rsp_data_o
);

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
    .core_rsp_valid_o   (mbox_rsp_valid),
    .core_rsp_data_o    (mbox_rsp_data),
    .core_rsp_ready_i   (1'b1), // Core always ready for MMIO response
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

  // Map from testbench to internal core_data
  assign core_data_valid = core_req_valid_i;
  assign core_data_write = core_req_write_i;
  assign core_data_addr  = core_req_addr_i;
  assign core_data_wdata = core_req_data_i;
  assign core_req_ready_o = core_data_ready;
  assign core_rsp_valid_o = core_data_rsp_valid;
  assign core_rsp_data_o  = core_data_rdata;

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
    
    if (mbox_rsp_valid) begin
      core_data_rsp_valid = 1'b1;
      core_data_rdata = mbox_rsp_data;
    end else if (tcdm_rsp_valid_i) begin
      core_data_rsp_valid = 1'b1;
      core_data_rdata = tcdm_rsp_data_i;
    end
  end

  // Interrupt mapping
  logic [2:0] irq; // {mti, msi, mei}
  assign irq = {1'b0, 1'b0, irq_from_mbox}; // Map Mailbox interrupt to Machine External Interrupt

  // Snitch RISC-V Core Instantiation
  // (Simplified placeholder binding for the Snitch generic core)
  // The actual snitch_cc uses complex struct interfaces, here we abstract
  // the signals to flattened req/rsp for simplicity in the NPU wrapper.
  
  // To instantiate the true Snitch, we would map core_data_* to the dreq_t struct
  // and ispm_req_* to the hive_req_t struct.
  
  // ... Snitch wrapper logic goes here ...
  // For the sake of the smoke test, we simulate the core fetching instructions from I-SPM
  // and executing dummy operations or waiting for the mailbox.

endmodule
