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

  // AXI4-Lite Slave Interface from Host CPU
  input  logic [AxiAddrWidth-1:0]  s_axi_awaddr_i,
  input  logic                     s_axi_awvalid_i,
  output logic                     s_axi_awready_o,
  input  logic [DataWidth-1:0]     s_axi_wdata_i,
  input  logic [(DataWidth/8)-1:0] s_axi_wstrb_i,
  input  logic                     s_axi_wvalid_i,
  output logic                     s_axi_wready_o,
  output logic [1:0]               s_axi_bresp_o,
  output logic                     s_axi_bvalid_o,
  input  logic                     s_axi_bready_i,
  
  input  logic [AxiAddrWidth-1:0]  s_axi_araddr_i,
  input  logic                     s_axi_arvalid_i,
  output logic                     s_axi_arready_o,
  output logic [DataWidth-1:0]     s_axi_rdata_o,
  output logic [1:0]               s_axi_rresp_o,
  output logic                     s_axi_rvalid_o,
  input  logic                     s_axi_rready_i,

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

  // AXI4 Master Write Interface
  output logic                     axi_aw_valid_o,
  output logic [AxiAddrWidth-1:0]  axi_aw_addr_o,
  output logic [7:0]               axi_aw_len_o,
  output logic [2:0]               axi_aw_size_o,
  input  logic                     axi_aw_ready_i,
  output logic                     axi_w_valid_o,
  output logic [AxiDataWidth-1:0]  axi_w_data_o,
  output logic [(AxiDataWidth/8)-1:0] axi_w_strb_o,
  output logic                     axi_w_last_o,
  input  logic                     axi_w_ready_i,
  input  logic                     axi_b_valid_i,
  output logic                     axi_b_ready_o,

  // Interrupt to ARM Host
  output logic                 irq_to_host_o,

  // (Firmware Load Port is now multiplexed through AXI-Lite Slave)

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
  logic                 host_req_valid_i;
  logic                 host_req_write_i;
  logic [AddrWidth-1:0] host_req_addr_i;
  logic [DataWidth-1:0] host_req_data_i;
  logic                 host_req_ready_o;
  logic                 host_rsp_valid_o;
  logic [DataWidth-1:0] host_rsp_data_o;
  logic                 host_rsp_ready_i;

  logic                 fw_req_valid_i;
  logic                 fw_req_write_i;
  logic [AddrWidth-1:0] fw_req_addr_i;
  logic [DataWidth-1:0] fw_req_data_i;
  logic                 fw_req_ready_o;
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
    .fw_rsp_ready_i(1'b1)
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

  logic [NumCores-1:0] compute_irq;
  logic [NumCores-1:0] compute_irq_clear;
  
  assign compute_irq_clear = (ctrl_tcdm_req_valid && ctrl_tcdm_req_write && ctrl_tcdm_req_addr == 32'h6000_0F08 && mem_sel_core) ? ctrl_tcdm_req_data[NumCores-1:0] : '0;

  // Delayed response for core registers
  logic core_reg_rsp_valid_q;
  logic [31:0] core_reg_rsp_data_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      core_reg_rsp_valid_q <= 1'b0;
      core_reg_rsp_data_q <= '0;
    end else begin
      core_reg_rsp_valid_q <= 1'b0;
      if (mem_sel_core && ctrl_tcdm_req_valid) begin
        if (ctrl_tcdm_req_addr == 32'h6000_0F10) begin
          $display("[%0t] [CORE_REG_RSP] Processing valid reg request", $time);
          core_reg_rsp_valid_q <= 1'b1;
          core_reg_rsp_data_q <= cluster_core_mask_q;
        end else if (ctrl_tcdm_req_addr == 32'h6000_0F04) begin
          core_reg_rsp_valid_q <= 1'b1;
          core_reg_rsp_data_q <= { {(32-NumCores){1'b0}}, compute_irq };
        end else if (ctrl_tcdm_req_addr == 32'h6000_0F08) begin
          core_reg_rsp_valid_q <= 1'b1;
          core_reg_rsp_data_q <= 32'h0;
        end
      end
    end
  end

  // Mux Responses back to Control Core
  // Assume only 1 interface responds at a time
  always_comb begin
    ctrl_tcdm_req_ready = 1'b0;
    
    // Request routing
    if (mem_sel_tcdm) begin
      ctrl_tcdm_req_ready = tcdm_req_ready;
    end else if (mem_sel_dma) begin
      ctrl_tcdm_req_ready = dma_req_ready;
    end else if (mem_sel_core) begin
      ctrl_tcdm_req_ready = 1'b1;
    end

    // Response routing
    ctrl_tcdm_rsp_valid = 1'b0;
    ctrl_tcdm_rsp_data  = '0;
    
    if (tcdm_rsp_valid) begin
      ctrl_tcdm_rsp_valid = tcdm_rsp_valid;
      ctrl_tcdm_rsp_data  = tcdm_rsp_data;
    end else if (dma_rsp_valid) begin
      ctrl_tcdm_rsp_valid = dma_rsp_valid;
      ctrl_tcdm_rsp_data  = dma_rsp_data;
    end else if (core_reg_rsp_valid_q) begin
      ctrl_tcdm_rsp_valid = 1'b1;
      ctrl_tcdm_rsp_data  = core_reg_rsp_data_q;
    end else if (|core_mmio_rsp_valid) begin
      ctrl_tcdm_rsp_valid = 1'b1;
      for (int i=0; i<NumCores; i++) begin
        if (core_mmio_rsp_valid[i]) ctrl_tcdm_rsp_data = core_mmio_rsp_data[i];
      end
    end
  end

  // Debug Print
  always_ff @(posedge clk_i) begin
    if (rst_ni) begin
      if (ctrl_tcdm_req_valid) begin
        if (ctrl_tcdm_req_ready) begin
          if (ctrl_tcdm_req_write)
            $display("[%0t] [CTRL] WRITE Addr: %x Data: %x", $time, ctrl_tcdm_req_addr, ctrl_tcdm_req_data);
          else if (ctrl_tcdm_rsp_valid)
            $display("[%0t] [CTRL] READ Addr: %x Data: %x", $time, ctrl_tcdm_req_addr, ctrl_tcdm_rsp_data);
        end else begin
          $display("[%0t] [CTRL] STALLED Addr: %x (mem_sel_tcdm=%b, mem_sel_core=%b, tcdm_ready=%b)", $time, ctrl_tcdm_req_addr, mem_sel_tcdm, mem_sel_core, tcdm_req_ready);
        end
      end
      if (tcdm_req_valid || tcdm_rsp_valid || ctrl_tcdm_rsp_valid) begin
        // Removed LSU_DEBUG
      end
      if (dma_req_valid && dma_req_ready) begin
        $display("[%0t] [DMA] REQ Addr: %x", $time, ctrl_tcdm_req_addr); // actually dma req info is inside dma_ctrl_req_addr_i
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
    .axi_aw_valid_o(axi_aw_valid_o),
    .axi_aw_addr_o(axi_aw_addr_o),
    .axi_aw_len_o(axi_aw_len_o),
    .axi_aw_size_o(axi_aw_size_o),
    .axi_aw_ready_i(axi_aw_ready_i),
    .axi_w_valid_o(axi_w_valid_o),
    .axi_w_data_o(axi_w_data_o),
    .axi_w_strb_o(axi_w_strb_o),
    .axi_w_last_o(axi_w_last_o),
    .axi_w_ready_i(axi_w_ready_i),
    .axi_b_valid_i(axi_b_valid_i),
    .axi_b_ready_o(axi_b_ready_o),
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
      .mmio_req_valid_i(core_mmio_req_valid[i]),
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
      .tcdm_rsp_rdata_i(ext_rsp_rdata[i+1]),
      // Interrupts
      .irq_o           (compute_irq[i]),
      .irq_clear_i     (compute_irq_clear[i])
    );
  end
  // ---------------------------------------------------------
  // AXI-Lite to Internal SRAM/Mailbox Bridge
  // ---------------------------------------------------------
  typedef enum logic [1:0] { IDLE, WRITE_WAIT_READY, WRITE_WAIT_RESP, READ_WAIT_READY } axi_state_e;
  axi_state_e axi_state_q, axi_state_d;

  logic aw_ready, w_ready, b_valid, ar_ready, r_valid;
  logic [AddrWidth-1:0] waddr_q;
  logic [AddrWidth-1:0] raddr_q;

  assign s_axi_awready_o = aw_ready;
  assign s_axi_wready_o  = w_ready;
  assign s_axi_bvalid_o  = b_valid;
  assign s_axi_bresp_o   = 2'b00; // OKAY
  assign s_axi_arready_o = ar_ready;
  assign s_axi_rvalid_o  = r_valid;
  assign s_axi_rresp_o   = 2'b00; // OKAY

  // Address Decoding
  logic is_fw_write, is_fw_read;
  logic is_mb_write, is_mb_read;
  
  assign is_mb_write = (s_axi_awaddr_i >= 32'h4000_0000);
  assign is_fw_write = (s_axi_awaddr_i >= 32'h0000_1000 && s_axi_awaddr_i < 32'h0000_9000);
  
  assign is_mb_read  = (s_axi_araddr_i >= 32'h4000_0000);
  assign is_fw_read  = (s_axi_araddr_i >= 32'h0000_1000 && s_axi_araddr_i < 32'h0000_9000);

  always_comb begin
    axi_state_d = axi_state_q;
    aw_ready = 1'b0;
    w_ready  = 1'b0;
    b_valid  = 1'b0;
    ar_ready = 1'b0;
    r_valid  = 1'b0;
    
    host_req_valid_i = 1'b0;
    host_req_write_i = 1'b0;
    host_req_addr_i  = s_axi_awaddr_i;
    host_req_data_i  = s_axi_wdata_i;
    
    fw_req_valid_i   = 1'b0;
    fw_req_write_i   = 1'b0;
    fw_req_addr_i    = s_axi_awaddr_i;
    fw_req_data_i    = s_axi_wdata_i;

    host_rsp_ready_i = 1'b1;

    case (axi_state_q)
      IDLE: begin
        if (s_axi_awvalid_i && s_axi_wvalid_i) begin
          if (is_mb_write) begin
            host_req_valid_i = 1'b1;
            host_req_write_i = 1'b1;
            host_req_addr_i  = s_axi_awaddr_i;
            host_req_data_i  = s_axi_wdata_i;
            if (host_req_ready_o) begin
              aw_ready = 1'b1;
              w_ready  = 1'b1;
              axi_state_d = WRITE_WAIT_RESP;
            end else begin
              axi_state_d = WRITE_WAIT_READY;
            end
          end else if (is_fw_write) begin
            fw_req_valid_i = 1'b1;
            fw_req_write_i = 1'b1;
            fw_req_addr_i  = s_axi_awaddr_i;
            fw_req_data_i  = s_axi_wdata_i;
            if (fw_req_ready_o) begin
              aw_ready = 1'b1;
              w_ready  = 1'b1;
              axi_state_d = WRITE_WAIT_RESP;
            end else begin
              axi_state_d = WRITE_WAIT_READY;
            end
          end else begin
            // Error mapping
            aw_ready = 1'b1;
            w_ready  = 1'b1;
            axi_state_d = WRITE_WAIT_RESP;
          end
        end else if (s_axi_arvalid_i) begin
          if (is_mb_read) begin
            host_req_valid_i = 1'b1;
            host_req_write_i = 1'b0;
            host_req_addr_i  = s_axi_araddr_i;
            if (host_req_ready_o) begin
              ar_ready = 1'b1;
              axi_state_d = READ_WAIT_READY;
            end
          end else if (is_fw_read) begin
            fw_req_valid_i = 1'b1;
            fw_req_write_i = 1'b0;
            fw_req_addr_i  = s_axi_araddr_i;
            if (fw_req_ready_o) begin
              ar_ready = 1'b1;
              axi_state_d = READ_WAIT_READY;
            end
          end else begin
            ar_ready = 1'b1;
            axi_state_d = READ_WAIT_READY;
          end
        end
      end
      
      WRITE_WAIT_READY: begin
        if (waddr_q >= 32'h4000_0000) begin
          host_req_valid_i = 1'b1;
          host_req_write_i = 1'b1;
          host_req_addr_i  = waddr_q;
          host_req_data_i  = s_axi_wdata_i;
          if (host_req_ready_o) begin
            aw_ready = 1'b1;
            w_ready  = 1'b1;
            axi_state_d = WRITE_WAIT_RESP;
          end
        end else begin
          fw_req_valid_i = 1'b1;
          fw_req_write_i = 1'b1;
          fw_req_addr_i  = waddr_q;
          fw_req_data_i  = s_axi_wdata_i;
          if (fw_req_ready_o) begin
            aw_ready = 1'b1;
            w_ready  = 1'b1;
            axi_state_d = WRITE_WAIT_RESP;
          end
        end
      end
      
      WRITE_WAIT_RESP: begin
        b_valid = 1'b1;
        if (s_axi_bready_i) begin
          axi_state_d = IDLE;
        end
      end
      
      READ_WAIT_READY: begin
        if (raddr_q >= 32'h4000_0000) begin
          if (host_rsp_valid_o) begin
            r_valid = 1'b1;
            if (s_axi_rready_i) axi_state_d = IDLE;
          end
        end else begin
          r_valid = 1'b1;
          if (s_axi_rready_i) axi_state_d = IDLE;
        end
      end
    endcase
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      axi_state_q <= IDLE;
      waddr_q <= '0;
      raddr_q <= '0;
      s_axi_rdata_o <= '0;
    end else begin
      axi_state_q <= axi_state_d;
      
      if (axi_state_q == IDLE && s_axi_awvalid_i && s_axi_wvalid_i) begin
        waddr_q <= s_axi_awaddr_i;
      end
      if (axi_state_q == IDLE && s_axi_arvalid_i && !(s_axi_awvalid_i && s_axi_wvalid_i)) begin
        raddr_q <= s_axi_araddr_i;
      end
      
      if (host_rsp_valid_o && axi_state_q == READ_WAIT_READY && raddr_q >= 32'h4000_0000) begin
        s_axi_rdata_o <= host_rsp_data_o;
      end else if (axi_state_q == READ_WAIT_READY && raddr_q < 32'h4000_0000) begin
        s_axi_rdata_o <= 32'h0;
      end
    end
  end

endmodule
