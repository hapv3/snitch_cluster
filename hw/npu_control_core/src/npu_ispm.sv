// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// Instruction Scratchpad Memory (I-SPM) and Boot ROM for the NPU Control Core.
/// Includes a small Boot ROM that the Snitch core executes upon reset,
/// waiting for the ARM host to load the firmware into the I-SPM SRAM.

module npu_ispm #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32,
  parameter int unsigned IspmSize  = 32768, // 32KB
  parameter int unsigned RomSize   = 1024   // 1KB
) (
  input  logic clk_i,
  input  logic rst_ni,

  // Req/Rsp Interface for Instruction Fetch (from Snitch)
  input  logic                 fetch_req_valid_i,
  input  logic [AddrWidth-1:0] fetch_req_addr_i,
  output logic                 fetch_req_ready_o,
  
  output logic                 fetch_rsp_valid_o,
  output logic [DataWidth-1:0] fetch_rsp_data_o,

  // Req/Rsp Interface for Firmware Load (from ARM Host / DMA)
  input  logic                 load_req_valid_i,
  input  logic                 load_req_write_i,
  input  logic [AddrWidth-1:0] load_req_addr_i,
  input  logic [DataWidth-1:0] load_req_data_i,
  output logic                 load_req_ready_o,
  
  output logic                 load_rsp_valid_o,
  output logic [DataWidth-1:0] load_rsp_data_o,
  input  logic                 load_rsp_ready_i
);

  // Address mapping:
  // 0x0000_0000 to 0x0000_03FF: Boot ROM
  // 0x0000_1000 to 0x0000_8FFF: I-SPM SRAM

  localparam int IspmWords = IspmSize / (DataWidth/8);
  localparam int RomWords  = RomSize / (DataWidth/8);

  logic [DataWidth-1:0] ispm_ram [IspmWords];
  logic [DataWidth-1:0] boot_rom [RomWords];

  // Initialize Boot ROM with a simple jump loop waiting for an interrupt
  // (e.g., wfi or simple branch-to-self)
  initial begin
    for (int i = 0; i < RomWords; i++) boot_rom[i] = 32'h00000013; // nop
    boot_rom[0] = 32'h10500073; // wfi
    boot_rom[1] = 32'hbfdff06f; // j -4 (loop back to wfi)
  end

  // Instruction Fetch Port (Read-Only)
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      fetch_rsp_valid_o <= 1'b0;
      fetch_rsp_data_o <= '0;
    end else begin
      fetch_req_ready_o <= 1'b1; // Always ready
      if (fetch_req_valid_i && fetch_req_ready_o) begin
        fetch_rsp_valid_o <= 1'b1;
        if (fetch_req_addr_i < RomSize) begin
          fetch_rsp_data_o <= boot_rom[fetch_req_addr_i[$clog2(RomWords)+1:2]];
        end else if (fetch_req_addr_i >= 32'h1000 && fetch_req_addr_i < 32'h1000 + IspmSize) begin
          fetch_rsp_data_o <= ispm_ram[(fetch_req_addr_i - 32'h1000) >> 2];
        end else begin
          fetch_rsp_data_o <= 32'h00000000; // Error / trap instruction
        end
      end else begin
        fetch_rsp_valid_o <= 1'b0;
      end
    end
  end

  // Firmware Load Port (Read/Write)
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      load_rsp_valid_o <= 1'b0;
      load_rsp_data_o <= '0;
    end else begin
      if (load_rsp_valid_o && load_rsp_ready_i) begin
        load_rsp_valid_o <= 1'b0;
      end
      
      load_req_ready_o <= !load_rsp_valid_o;
      
      if (load_req_valid_i && load_req_ready_o) begin
        load_rsp_valid_o <= 1'b1;
        if (load_req_addr_i >= 32'h1000 && load_req_addr_i < 32'h1000 + IspmSize) begin
          automatic logic [$clog2(IspmWords)-1:0] idx = (load_req_addr_i - 32'h1000) >> 2;
          if (load_req_write_i) begin
            ispm_ram[idx] <= load_req_data_i;
            load_rsp_data_o <= '0;
          end else begin
            load_rsp_data_o <= ispm_ram[idx];
          end
        end else begin
          load_rsp_data_o <= '0;
        end
      end
    end
  end

endmodule
