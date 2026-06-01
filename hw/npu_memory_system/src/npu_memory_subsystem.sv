// Copyright 2026 NPU IP
// TCDM Memory Subsystem Wrapper

module npu_memory_subsystem #(
  parameter int NumMasters = 5,
  parameter int NumBanks = 32,
  parameter int DataWidth = 32,
  parameter int AddrWidth = 32,
  parameter int BankAddrWidth = 11, // 8 KB per bank
  parameter int AxiDataWidth = 128,
  parameter int AxiAddrWidth = 32
)(
  input  logic clk_i,
  input  logic rst_ni,

  // DMA Control (MMIO)
  input  logic                     dma_ctrl_req_valid_i,
  input  logic                     dma_ctrl_req_write_i,
  input  logic [AddrWidth-1:0]     dma_ctrl_req_addr_i,
  input  logic [31:0]              dma_ctrl_req_data_i,
  output logic                     dma_ctrl_req_ready_o,
  output logic                     dma_ctrl_rsp_valid_o,
  output logic [31:0]              dma_ctrl_rsp_data_o,

  // AXI4 Master Interface from DMA
  output logic                     axi_ar_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_ar_addr_o,
  output logic [7:0]               axi_ar_len_o,
  output logic [2:0]               axi_ar_size_o,
  input  logic                     axi_ar_ready_i,
  input  logic                     axi_r_valid_i,
  input  logic [AxiDataWidth-1:0]  axi_r_data_i,
  input  logic                     axi_r_last_i,
  output logic                     axi_r_ready_o,

  // Direct Requesters (SSR 0, SSR 1, SSR 2, RISC-V Core)
  // Master 0 is DMA internally. Master 1..4 are external.
  input  logic [NumMasters-2:0]                ext_req_valid_i,
  input  logic [NumMasters-2:0]                ext_req_write_i,
  input  logic [NumMasters-2:0][3:0]           ext_req_be_i,
  input  logic [NumMasters-2:0][AddrWidth-1:0] ext_req_addr_i,
  input  logic [NumMasters-2:0][DataWidth-1:0] ext_req_wdata_i,
  output logic [NumMasters-2:0]                ext_req_ready_o,
  output logic [NumMasters-2:0]                ext_rsp_valid_o,
  output logic [NumMasters-2:0][DataWidth-1:0] ext_rsp_rdata_o
);

  logic [NumMasters-1:0]                master_req_valid;
  logic [NumMasters-1:0]                master_req_write;
  logic [NumMasters-1:0][3:0]           master_req_be;
  logic [NumMasters-1:0][AddrWidth-1:0] master_req_addr;
  logic [NumMasters-1:0][DataWidth-1:0] master_req_wdata;
  logic [NumMasters-1:0]                master_req_ready;
  logic [NumMasters-1:0]                master_rsp_valid;
  logic [NumMasters-1:0][DataWidth-1:0] master_rsp_rdata;

  // Master 0: DMA Engine
  npu_dma_engine #(
    .AxiDataWidth(AxiDataWidth),
    .AxiAddrWidth(AxiAddrWidth),
    .TcdmDataWidth(DataWidth),
    .TcdmAddrWidth(AddrWidth)
  ) dma_inst (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    // Control
    .ctrl_req_valid_i (dma_ctrl_req_valid_i),
    .ctrl_req_write_i (dma_ctrl_req_write_i),
    .ctrl_req_addr_i  (dma_ctrl_req_addr_i),
    .ctrl_req_data_i  (dma_ctrl_req_data_i),
    .ctrl_req_ready_o (dma_ctrl_req_ready_o),
    .ctrl_rsp_valid_o (dma_ctrl_rsp_valid_o),
    .ctrl_rsp_data_o  (dma_ctrl_rsp_data_o),
    // TCDM
    .tcdm_req_valid_o (master_req_valid[0]),
    .tcdm_req_write_o (master_req_write[0]),
    .tcdm_req_be_o    (master_req_be[0]),
    .tcdm_req_addr_o  (master_req_addr[0]),
    .tcdm_req_wdata_o (master_req_wdata[0]),
    .tcdm_req_ready_i (master_req_ready[0]),
    .tcdm_rsp_valid_i (master_rsp_valid[0]),
    .tcdm_rsp_rdata_i (master_rsp_rdata[0]),
    // AXI
    .axi_ar_valid_o(axi_ar_valid_o),
    .axi_ar_addr_o(axi_ar_addr_o),
    .axi_ar_len_o(axi_ar_len_o),
    .axi_ar_size_o(axi_ar_size_o),
    .axi_ar_ready_i(axi_ar_ready_i),
    .axi_r_valid_i(axi_r_valid_i),
    .axi_r_data_i(axi_r_data_i),
    .axi_r_last_i(axi_r_last_i),
    .axi_r_ready_o(axi_r_ready_o),
    // Write channels ignored for now
    .axi_aw_ready_i(1'b0),
    .axi_w_ready_i(1'b0),
    .axi_b_valid_i(1'b0),
    .axi_aw_valid_o(), .axi_aw_addr_o(), .axi_aw_len_o(), .axi_aw_size_o(),
    .axi_w_valid_o(), .axi_w_data_o(), .axi_w_strb_o(), .axi_w_last_o(),
    .axi_b_ready_o()
  );

  // Master 1..4: External Requesters
  for (genvar i = 0; i < NumMasters-1; i++) begin : gen_ext_masters
    assign master_req_valid[i+1] = ext_req_valid_i[i];
    assign master_req_write[i+1] = ext_req_write_i[i];
    assign master_req_be[i+1]    = ext_req_be_i[i];
    assign master_req_addr[i+1]  = ext_req_addr_i[i];
    assign master_req_wdata[i+1] = ext_req_wdata_i[i];
    assign ext_req_ready_o[i]    = master_req_ready[i+1];
    assign ext_rsp_valid_o[i]    = master_rsp_valid[i+1];
    assign ext_rsp_rdata_o[i]    = master_rsp_rdata[i+1];
  end

  // Interconnect to Banks
  logic [NumBanks-1:0]                   bank_req;
  logic [NumBanks-1:0]                   bank_write;
  logic [NumBanks-1:0][3:0]              bank_be;
  logic [NumBanks-1:0][BankAddrWidth-1:0] bank_addr;
  logic [NumBanks-1:0][DataWidth-1:0]    bank_wdata;
  logic [NumBanks-1:0][DataWidth-1:0]    bank_rdata;

  npu_tcdm_interconnect #(
    .NumMasters(NumMasters),
    .NumBanks(NumBanks),
    .DataWidth(DataWidth),
    .AddrWidth(AddrWidth),
    .BankAddrWidth(BankAddrWidth)
  ) xbar_inst (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .master_req_valid_i(master_req_valid),
    .master_req_write_i(master_req_write),
    .master_req_be_i   (master_req_be),
    .master_req_addr_i (master_req_addr),
    .master_req_wdata_i(master_req_wdata),
    .master_req_ready_o(master_req_ready),
    .master_rsp_valid_o(master_rsp_valid),
    .master_rsp_rdata_o(master_rsp_rdata),
    .bank_req_o  (bank_req),
    .bank_write_o(bank_write),
    .bank_be_o   (bank_be),
    .bank_addr_o (bank_addr),
    .bank_wdata_o(bank_wdata),
    .bank_rdata_i(bank_rdata)
  );

  // Bank Instantiations
  for (genvar b = 0; b < NumBanks; b++) begin : gen_tcdm_banks
    npu_tcdm_bank #(
      .Depth(2048), // 8 KB
      .DataWidth(DataWidth),
      .AddrWidth(BankAddrWidth)
    ) bank_inst (
      .clk_i(clk_i),
      .req_i(bank_req[b]),
      .write_i(bank_write[b]),
      .be_i(bank_be[b]),
      .addr_i(bank_addr[b]),
      .wdata_i(bank_wdata[b]),
      .rdata_o(bank_rdata[b])
    );
  end

endmodule
