// Copyright 2026 NPU IP
// Tightly Coupled Data Memory (TCDM) SRAM Bank

module npu_tcdm_bank #(
  parameter int Depth = 2048, // 8 KB (2048 x 32-bit words)
  parameter int DataWidth = 32,
  parameter int AddrWidth = $clog2(Depth)
)(
  input  logic                 clk_i,
  
  // TCDM Bank Interface
  input  logic                 req_i,
  input  logic                 write_i,
  input  logic [3:0]           be_i, // Byte enables
  input  logic [AddrWidth-1:0] addr_i,
  input  logic [DataWidth-1:0] wdata_i,
  output logic [DataWidth-1:0] rdata_o
);

  // Define SRAM array
  logic [DataWidth-1:0] mem [Depth];

  // Synchronous Read/Write
  always_ff @(posedge clk_i) begin
    if (req_i) begin
      if (write_i) begin
        if (be_i[0]) mem[addr_i][7:0]   <= wdata_i[7:0];
        if (be_i[1]) mem[addr_i][15:8]  <= wdata_i[15:8];
        if (be_i[2]) mem[addr_i][23:16] <= wdata_i[23:16];
        if (be_i[3]) mem[addr_i][31:24] <= wdata_i[31:24];
      end
      // Single-port behavior: read returns new data if writing, or old data if reading
      rdata_o <= mem[addr_i];
      $display("[%0t] [BANK] READ addr=%x data=%x", $time, addr_i, mem[addr_i]); 
    end
  end

endmodule
