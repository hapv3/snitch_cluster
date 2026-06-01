// Copyright 2026 NPU IP
// NPU Cluster Top Level
// Integrates 1 Control Core, 10 Compute Cores, and the Memory Subsystem.

module npu_cluster_top #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32,
  parameter int unsigned AxiDataWidth = 128,
  parameter int unsigned AxiAddrWidth = 32,
  parameter int unsigned NumCores = 10
) (
  input  logic clk_i,
  input  logic rst_ni,

  // Interface from ARM Host to Mailbox (AXI4-Lite proxy)
  input  logic                 host_req_valid_i,
  input  logic                 host_req_write_i,
  input  logic [AddrWidth-1:0] host_req_addr_i,
  input  logic [DataWidth-1:0] host_req_data_i,
  output logic                 host_req_ready_o,
  
  output logic                 host_rsp_valid_o,
  output logic [DataWidth-1:0] host_rsp_data_o,
  input  logic                 host_rsp_ready_i,

  // AXI4 Master Interface from DMA (To External DDR)
  output logic                     axi_ar_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_ar_addr_o,
  output logic [7:0]               axi_ar_len_o,
  output logic [2:0]               axi_ar_size_o,
  input  logic                     axi_ar_ready_i,
  input  logic                     axi_r_valid_i,
  input  logic [AxiDataWidth-1:0]  axi_r_data_i,
  input  logic                     axi_r_last_i,
  output logic                     axi_r_ready_o,

  // Interrupt to ARM Host
  output logic                 irq_to_host_o,

  // Firmware Load Port (from Testbench / Host DMA)
  input  logic                 fw_req_valid_i,
  input  logic                 fw_req_write_i,
  input  logic [AddrWidth-1:0] fw_req_addr_i,
  input  logic [DataWidth-1:0] fw_req_data_i,
  output logic                 fw_req_ready_o,

  // Core Data Port (Driven by Testbench ISS)
  input  logic                 core_req_valid_i,
  input  logic                 core_req_write_i,
  input  logic [AddrWidth-1:0] core_req_addr_i,
  input  logic [DataWidth-1:0] core_req_data_i,
  output logic                 core_req_ready_o,
  output logic                 core_rsp_valid_o,
  output logic [DataWidth-1:0] core_rsp_data_o
);

  // ---------------------------------------------------------
  // Control Core Signals
  // ---------------------------------------------------------
  logic                 ctrl_tcdm_req_valid;
  logic                 ctrl_tcdm_req_write;
  logic [AddrWidth-1:0] ctrl_tcdm_req_addr;
  logic [DataWidth-1:0] ctrl_tcdm_req_data;
  logic                 ctrl_tcdm_req_ready;
  
  logic                 ctrl_tcdm_rsp_valid;
  logic [DataWidth-1:0] ctrl_tcdm_rsp_data;

  npu_control_core #(
    .AddrWidth(AddrWidth),
    .DataWidth(DataWidth)
  ) i_control_core (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .hart_id_i(32'h0),
    .host_req_valid_i(host_req_valid_i),
    .host_req_write_i(host_req_write_i),
    .host_req_addr_i(host_req_addr_i),
    .host_req_data_i(host_req_data_i),
    .host_req_ready_o(host_req_ready_o),
    .host_rsp_valid_o(host_rsp_valid_o),
    .host_rsp_data_o(host_rsp_data_o),
    .host_rsp_ready_i(host_rsp_ready_i),
    .tcdm_req_valid_o(ctrl_tcdm_req_valid),
    .tcdm_req_write_o(ctrl_tcdm_req_write),
    .tcdm_req_addr_o(ctrl_tcdm_req_addr),
    .tcdm_req_data_o(ctrl_tcdm_req_data),
    .tcdm_req_ready_i(ctrl_tcdm_req_ready),
    .tcdm_rsp_valid_i(ctrl_tcdm_rsp_valid),
    .tcdm_rsp_data_i(ctrl_tcdm_rsp_data),
    .irq_to_host_o(irq_to_host_o),
    .fw_req_valid_i(fw_req_valid_i),
    .fw_req_write_i(fw_req_write_i),
    .fw_req_addr_i(fw_req_addr_i),
    .fw_req_data_i(fw_req_data_i),
    .fw_req_ready_o(fw_req_ready_o),
    .fw_rsp_valid_o(),
    .fw_rsp_data_o(),
    .fw_rsp_ready_i(1'b1),
    .core_req_valid_i(core_req_valid_i),
    .core_req_write_i(core_req_write_i),
    .core_req_addr_i(core_req_addr_i),
    .core_req_data_i(core_req_data_i),
    .core_req_ready_o(core_req_ready_o),
    .core_rsp_valid_o(core_rsp_valid_o),
    .core_rsp_data_o(core_rsp_data_o)
  );

  // ---------------------------------------------------------
  // Address Decoder for Control Core LSU
  // ---------------------------------------------------------
  // 0x1000_0000 - 0x1FFF_FFFF: TCDM Memory
  // 0x6000_0000 - 0x6000_009F: Compute Cores MMIO (Unicast)
  // 0x6000_0F00              : Compute Cores MMIO (Broadcast)
  // 0x6000_0F10              : Cluster Core Mask
  // 0x7000_0000 - 0x7000_00FF: DMA MMIO

  logic mem_sel_tcdm, mem_sel_core, mem_sel_dma;
  assign mem_sel_tcdm = (ctrl_tcdm_req_addr >= 32'h1000_0000) && (ctrl_tcdm_req_addr < 32'h2000_0000);
  assign mem_sel_core = (ctrl_tcdm_req_addr >= 32'h6000_0000) && (ctrl_tcdm_req_addr < 32'h6000_1000);
  assign mem_sel_dma  = (ctrl_tcdm_req_addr >= 32'h7000_0000) && (ctrl_tcdm_req_addr < 32'h7000_1000);

  // TCDM routing
  logic tcdm_req_valid, tcdm_req_ready;
  logic tcdm_rsp_valid;
  logic [31:0] tcdm_rsp_data;
  assign tcdm_req_valid = ctrl_tcdm_req_valid && mem_sel_tcdm;

  // DMA routing
  logic dma_req_valid, dma_req_ready;
  logic dma_rsp_valid;
  logic [31:0] dma_rsp_data;
  assign dma_req_valid = ctrl_tcdm_req_valid && mem_sel_dma;

  // Compute Core MMIO routing
  logic [NumCores-1:0] core_mmio_req_valid;
  logic [NumCores-1:0] core_mmio_req_ready;
  logic [NumCores-1:0] core_mmio_rsp_valid;
  logic [NumCores-1:0][31:0] core_mmio_rsp_data;
  
  logic [31:0] cluster_core_mask_q;
  
  // Demux to cores
  always_comb begin
    core_mmio_req_valid = '0;
    if (ctrl_tcdm_req_valid && mem_sel_core) begin
      if (ctrl_tcdm_req_addr == 32'h6000_0F00) begin
        // Broadcast
        core_mmio_req_valid = cluster_core_mask_q[NumCores-1:0];
      end else if (ctrl_tcdm_req_addr < 32'h6000_0F00) begin
        // Unicast (stride 0x20 per core)
        automatic int core_idx = (ctrl_tcdm_req_addr - 32'h6000_0000) / 32;
        if (core_idx < NumCores) begin
          core_mmio_req_valid[core_idx] = 1'b1;
        end
      end
    end
  end

  // Register for Cluster Core Mask
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cluster_core_mask_q <= '0;
    end else begin
      if (ctrl_tcdm_req_valid && ctrl_tcdm_req_write && ctrl_tcdm_req_addr == 32'h6000_0F10) begin
        cluster_core_mask_q <= ctrl_tcdm_req_data;
      end
    end
  end

  // Mux Responses back to Control Core
  // Assume only 1 interface responds at a time
  always_comb begin
    ctrl_tcdm_req_ready = 1'b0;
    ctrl_tcdm_rsp_valid = 1'b0;
    ctrl_tcdm_rsp_data = '0;
    
    if (mem_sel_tcdm) begin
      ctrl_tcdm_req_ready = tcdm_req_ready;
      ctrl_tcdm_rsp_valid = tcdm_rsp_valid;
      ctrl_tcdm_rsp_data = tcdm_rsp_data;
    end else if (mem_sel_dma) begin
      ctrl_tcdm_req_ready = dma_req_ready;
      ctrl_tcdm_rsp_valid = dma_rsp_valid;
      ctrl_tcdm_rsp_data = dma_rsp_data;
    end else if (mem_sel_core) begin
      if (ctrl_tcdm_req_addr == 32'h6000_0F10) begin
        ctrl_tcdm_req_ready = 1'b1;
        // Pseudo response for mask register write
        ctrl_tcdm_rsp_valid = 1'b1;
        ctrl_tcdm_rsp_data = 32'h0;
      end else begin
        // For broadcast or unicast, we aggregate ready/valid
        // For simplicity, wait until ALL targeted cores are ready
        ctrl_tcdm_req_ready = 1'b1;
        ctrl_tcdm_rsp_valid = 1'b1;
        for (int i=0; i<NumCores; i++) begin
          if (core_mmio_req_valid[i]) begin
            ctrl_tcdm_req_ready &= core_mmio_req_ready[i];
            ctrl_tcdm_rsp_valid &= core_mmio_rsp_valid[i];
            // Just return data from the lowest targeted core
            if (ctrl_tcdm_rsp_data == '0) begin
              ctrl_tcdm_rsp_data = core_mmio_rsp_data[i];
            end
          end
        end
      end
    end
  end

  // ---------------------------------------------------------
  // Memory Subsystem (TCDM + Interconnect + DMA)
  // ---------------------------------------------------------
  // NumMasters = 1 (Control Core) + 10 (Compute Cores) = 11 External Requesters
  // Plus 1 internal DMA = 12 total ports.
  localparam int TotalMasters = 12;
  
  logic [TotalMasters-2:0]                ext_req_valid;
  logic [TotalMasters-2:0]                ext_req_write;
  logic [TotalMasters-2:0][3:0]           ext_req_be;
  logic [TotalMasters-2:0][AddrWidth-1:0] ext_req_addr;
  logic [TotalMasters-2:0][DataWidth-1:0] ext_req_wdata;
  logic [TotalMasters-2:0]                ext_req_ready;
  logic [TotalMasters-2:0]                ext_rsp_valid;
  logic [TotalMasters-2:0][DataWidth-1:0] ext_rsp_rdata;

  // Port 0: Control Core
  assign ext_req_valid[0] = tcdm_req_valid;
  assign ext_req_write[0] = ctrl_tcdm_req_write;
  assign ext_req_be[0]    = 4'hF;
  assign ext_req_addr[0]  = ctrl_tcdm_req_addr;
  assign ext_req_wdata[0] = ctrl_tcdm_req_data;
  assign tcdm_req_ready   = ext_req_ready[0];
  assign tcdm_rsp_valid   = ext_rsp_valid[0];
  assign tcdm_rsp_data    = ext_rsp_rdata[0];

  npu_memory_subsystem #(
    .NumMasters(TotalMasters),
    .NumBanks(32)
  ) i_mem_subsys (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .dma_ctrl_req_valid_i(dma_req_valid),
    .dma_ctrl_req_write_i(ctrl_tcdm_req_write),
    .dma_ctrl_req_addr_i(ctrl_tcdm_req_addr),
    .dma_ctrl_req_data_i(ctrl_tcdm_req_data),
    .dma_ctrl_req_ready_o(dma_req_ready),
    .dma_ctrl_rsp_valid_o(dma_rsp_valid),
    .dma_ctrl_rsp_data_o(dma_rsp_data),
    .axi_ar_valid_o(axi_ar_valid_o),
    .axi_ar_addr_o(axi_ar_addr_o),
    .axi_ar_len_o(axi_ar_len_o),
    .axi_ar_size_o(axi_ar_size_o),
    .axi_ar_ready_i(axi_ar_ready_i),
    .axi_r_valid_i(axi_r_valid_i),
    .axi_r_data_i(axi_r_data_i),
    .axi_r_last_i(axi_r_last_i),
    .axi_r_ready_o(axi_r_ready_o),
    .ext_req_valid_i(ext_req_valid),
    .ext_req_write_i(ext_req_write),
    .ext_req_be_i(ext_req_be),
    .ext_req_addr_i(ext_req_addr),
    .ext_req_wdata_i(ext_req_wdata),
    .ext_req_ready_o(ext_req_ready),
    .ext_rsp_valid_o(ext_rsp_valid),
    .ext_rsp_rdata_o(ext_rsp_rdata)
  );

  // ---------------------------------------------------------
  // 10 Compute Cores Instantiation
  // ---------------------------------------------------------
  for (genvar i = 0; i < NumCores; i++) begin : gen_compute_cores
    npu_core_wrapper #(
      .CoreId(i)
    ) i_core_wrapper (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .mmio_req_valid_i(core_mmio_req_valid[i] | (ctrl_tcdm_req_valid & mem_sel_core_bcast)),
      .mmio_req_write_i(ctrl_tcdm_req_write),
      .mmio_req_addr_i({27'd0, ctrl_tcdm_req_addr[4:0]}),
      .mmio_req_data_i(ctrl_tcdm_req_data),
      .mmio_req_ready_o(core_mmio_req_ready[i]),
      .mmio_rsp_valid_o(core_mmio_rsp_valid[i]),
      .mmio_rsp_data_o(core_mmio_rsp_data[i]),
      // Connect to memory subsystem (Port i+1)
      .tcdm_req_valid_o(ext_req_valid[i+1]),
      .tcdm_req_write_o(ext_req_write[i+1]),
      .tcdm_req_be_o   (ext_req_be[i+1]),
      .tcdm_req_addr_o (ext_req_addr[i+1]),
      .tcdm_req_wdata_o(ext_req_wdata[i+1]),
      .tcdm_req_ready_i(ext_req_ready[i+1]),
      .tcdm_rsp_valid_i(ext_rsp_valid[i+1]),
      .tcdm_rsp_rdata_i(ext_rsp_rdata[i+1])
    );
  end

endmodule
