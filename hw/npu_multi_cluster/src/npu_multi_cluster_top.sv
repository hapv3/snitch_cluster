// Copyright 2026 NPU IP
// NPU Multi-Cluster Top Level
// Integrates 4 NPU Clusters and multiplexes their AXI interfaces.

module npu_multi_cluster_top #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32,
  parameter int unsigned AxiDataWidth = 128,
  parameter int unsigned AxiAddrWidth = 32,
  parameter int unsigned NumCores = 10,
  parameter int unsigned NumClusters = 4
) (
  input  logic clk_i,
  input  logic rst_ni,

  // Interface from ARM Host to Mailbox (AXI4-Lite proxy)
  input  logic [NumClusters-1:0]                 host_req_valid_i,
  input  logic [NumClusters-1:0]                 host_req_write_i,
  input  logic [NumClusters-1:0][AddrWidth-1:0]  host_req_addr_i,
  input  logic [NumClusters-1:0][DataWidth-1:0]  host_req_data_i,
  output logic [NumClusters-1:0]                 host_req_ready_o,
  
  output logic [NumClusters-1:0]                 host_rsp_valid_o,
  output logic [NumClusters-1:0][DataWidth-1:0]  host_rsp_data_o,
  input  logic [NumClusters-1:0]                 host_rsp_ready_i,

  // Shared AXI4 Master Interface from DMA to External DDR (NoC Stub)
  output logic                     axi_ar_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_ar_addr_o,
  output logic [7:0]               axi_ar_len_o,
  output logic [2:0]               axi_ar_size_o,
  input  logic                     axi_ar_ready_i,
  input  logic                     axi_r_valid_i,
  input  logic [AxiDataWidth-1:0]  axi_r_data_i,
  input  logic                     axi_r_last_i,
  output logic                     axi_r_ready_o,

  // Interrupts to ARM Host
  output logic [NumClusters-1:0]   irq_to_host_o,

  // Firmware Load Ports
  input  logic [NumClusters-1:0]                 fw_req_valid_i,
  input  logic [NumClusters-1:0]                 fw_req_write_i,
  input  logic [NumClusters-1:0][AddrWidth-1:0]  fw_req_addr_i,
  input  logic [NumClusters-1:0][DataWidth-1:0]  fw_req_data_i,
  output logic [NumClusters-1:0]                 fw_req_ready_o,

  // Core Data Ports
  input  logic [NumClusters-1:0]                 core_req_valid_i,
  input  logic [NumClusters-1:0]                 core_req_write_i,
  input  logic [NumClusters-1:0][AddrWidth-1:0]  core_req_addr_i,
  input  logic [NumClusters-1:0][DataWidth-1:0]  core_req_data_i,
  output logic [NumClusters-1:0]                 core_req_ready_o,
  
  output logic [NumClusters-1:0]                 core_rsp_valid_o,
  output logic [NumClusters-1:0][DataWidth-1:0]  core_rsp_data_o
);

  // AXI Multiplexer / Interconnect Stub
  // For simplicity in testing, a round-robin arbiter for AXI read requests
  
  logic [NumClusters-1:0] cl_axi_ar_valid;
  logic [NumClusters-1:0][AxiAddrWidth-1:0] cl_axi_ar_addr;
  logic [NumClusters-1:0][7:0] cl_axi_ar_len;
  logic [NumClusters-1:0][2:0] cl_axi_ar_size;
  logic [NumClusters-1:0] cl_axi_ar_ready;
  logic [NumClusters-1:0] cl_axi_r_valid;
  logic [NumClusters-1:0] cl_axi_r_ready;
  
  // State for Arbiter
  logic [1:0] arb_grant_q, arb_grant_d;
  logic arb_active_q, arb_active_d;
  
  always_comb begin
    arb_grant_d = arb_grant_q;
    arb_active_d = arb_active_q;
    
    cl_axi_ar_ready = '0;
    cl_axi_r_valid = '0;
    
    axi_ar_valid_o = 1'b0;
    axi_ar_addr_o  = '0;
    axi_ar_len_o   = '0;
    axi_ar_size_o  = '0;
    axi_r_ready_o  = 1'b0;
    
    if (!arb_active_q) begin
      for (int i = 0; i < NumClusters; i++) begin
        if (cl_axi_ar_valid[i]) begin
          arb_grant_d = i[1:0];
          arb_active_d = 1'b1;
          break;
        end
      end
    end
    
    if (arb_active_q) begin
      axi_ar_valid_o = cl_axi_ar_valid[arb_grant_q];
      axi_ar_addr_o  = cl_axi_ar_addr[arb_grant_q];
      axi_ar_len_o   = cl_axi_ar_len[arb_grant_q];
      axi_ar_size_o  = cl_axi_ar_size[arb_grant_q];
      cl_axi_ar_ready[arb_grant_q] = axi_ar_ready_i;
      
      cl_axi_r_valid[arb_grant_q] = axi_r_valid_i;
      axi_r_ready_o = cl_axi_r_ready[arb_grant_q];
      
      if (axi_r_valid_i && axi_r_last_i && axi_r_ready_o) begin
        arb_active_d = 1'b0;
      end
    end
  end
  
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      arb_grant_q <= '0;
      arb_active_q <= 1'b0;
    end else begin
      arb_grant_q <= arb_grant_d;
      arb_active_q <= arb_active_d;
    end
  end

  // Instantiate 4 Clusters
  for (genvar i = 0; i < NumClusters; i++) begin : gen_clusters
    npu_cluster_top #(
      .AddrWidth(AddrWidth),
      .DataWidth(DataWidth),
      .AxiDataWidth(AxiDataWidth),
      .AxiAddrWidth(AxiAddrWidth),
      .NumCores(NumCores)
    ) i_cluster (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .host_req_valid_i(host_req_valid_i[i]),
      .host_req_write_i(host_req_write_i[i]),
      .host_req_addr_i(host_req_addr_i[i]),
      .host_req_data_i(host_req_data_i[i]),
      .host_req_ready_o(host_req_ready_o[i]),
      .host_rsp_valid_o(host_rsp_valid_o[i]),
      .host_rsp_data_o(host_rsp_data_o[i]),
      .host_rsp_ready_i(host_rsp_ready_i[i]),
      .axi_ar_valid_o(cl_axi_ar_valid[i]),
      .axi_ar_addr_o(cl_axi_ar_addr[i]),
      .axi_ar_len_o(cl_axi_ar_len[i]),
      .axi_ar_size_o(cl_axi_ar_size[i]),
      .axi_ar_ready_i(cl_axi_ar_ready[i]),
      .axi_r_valid_i(cl_axi_r_valid[i]),
      .axi_r_data_i(axi_r_data_i),
      .axi_r_last_i(axi_r_last_i),
      .axi_r_ready_o(cl_axi_r_ready[i]),
      .irq_to_host_o(irq_to_host_o[i]),
      .fw_req_valid_i(fw_req_valid_i[i]),
      .fw_req_write_i(fw_req_write_i[i]),
      .fw_req_addr_i(fw_req_addr_i[i]),
      .fw_req_data_i(fw_req_data_i[i]),
      .fw_req_ready_o(fw_req_ready_o[i]),
      .core_req_valid_i(core_req_valid_i[i]),
      .core_req_write_i(core_req_write_i[i]),
      .core_req_addr_i(core_req_addr_i[i]),
      .core_req_data_i(core_req_data_i[i]),
      .core_req_ready_o(core_req_ready_o[i]),
      .core_rsp_valid_o(core_rsp_valid_o[i]),
      .core_rsp_data_o(core_rsp_data_o[i])
    );
  end

endmodule
