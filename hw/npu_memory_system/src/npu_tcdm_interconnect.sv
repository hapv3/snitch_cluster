// Copyright 2026 NPU IP
// Pipelined Fully-Connected Crossbar for TCDM Interconnect

module npu_tcdm_interconnect #(
  parameter int NumMasters = 5,
  parameter int NumBanks = 32,
  parameter int DataWidth = 32,
  parameter int AddrWidth = 32,
  parameter int BankAddrWidth = 11, // 2048 words per bank
  parameter logic [AddrWidth-1:0] TcdmBaseAddr = 32'h1000_0000
)(
  input  logic clk_i,
  input  logic rst_ni,

  // Master Interfaces (from DMA, Core, SSRs)
  input  logic [NumMasters-1:0]                 master_req_valid_i,
  input  logic [NumMasters-1:0]                 master_req_write_i,
  input  logic [NumMasters-1:0][3:0]            master_req_be_i,
  input  logic [NumMasters-1:0][AddrWidth-1:0]  master_req_addr_i,
  input  logic [NumMasters-1:0][DataWidth-1:0]  master_req_wdata_i,
  output logic [NumMasters-1:0]                 master_req_ready_o,
  output logic [NumMasters-1:0]                 master_rsp_valid_o,
  output logic [NumMasters-1:0][DataWidth-1:0]  master_rsp_rdata_o,

  // Bank Interfaces (to SRAM banks)
  output logic [NumBanks-1:0]                   bank_req_o,
  output logic [NumBanks-1:0]                   bank_write_o,
  output logic [NumBanks-1:0][3:0]              bank_be_o,
  output logic [NumBanks-1:0][BankAddrWidth-1:0] bank_addr_o,
  output logic [NumBanks-1:0][DataWidth-1:0]    bank_wdata_o,
  input  logic [NumBanks-1:0][DataWidth-1:0]    bank_rdata_i
);

  localparam int MasterIdWidth = $clog2(NumMasters);
  localparam int BankSelWidth = $clog2(NumBanks);

  // Arbitration and Request Routing
  logic [NumBanks-1:0][NumMasters-1:0] req_matrix;
  
  always_comb begin
    // Default: no requests
    req_matrix = '0;
    
    // Decode master addresses to bank requests
    for (int m = 0; m < NumMasters; m++) begin
      if (master_req_valid_i[m]) begin
        automatic logic [BankSelWidth-1:0] bank_idx = master_req_addr_i[m][2 +: BankSelWidth];
        // Only map if address is within TCDM range (optional strict check, but we assume decoder upstream already checked)
        req_matrix[bank_idx][m] = 1'b1;
      end
    end
  end

  // Simple Fixed-Priority Arbiter per Bank (m=0 has highest priority)
  logic [NumBanks-1:0][MasterIdWidth-1:0] grant_idx;
  logic [NumBanks-1:0]                    bank_granted;

  always_comb begin
    for (int b = 0; b < NumBanks; b++) begin
      grant_idx[b] = '0;
      bank_granted[b] = 1'b0;
      // Fixed priority: lower index wins
      for (int m = NumMasters-1; m >= 0; m--) begin
        if (req_matrix[b][m]) begin
          grant_idx[b] = m[MasterIdWidth-1:0];
          bank_granted[b] = 1'b1;
        end
      end
    end
  end

  // Route granted master to bank
  always_comb begin
    for (int b = 0; b < NumBanks; b++) begin
      automatic int winner = grant_idx[b];
      bank_req_o[b]   = bank_granted[b];
      bank_write_o[b] = master_req_write_i[winner];
      bank_be_o[b]    = master_req_be_i[winner];
      // Address layout: [17:7] for BankAddrWidth=11, BankSelWidth=5, word aligned (2 bits)
      bank_addr_o[b]  = master_req_addr_i[winner][2 + BankSelWidth +: BankAddrWidth];
      bank_wdata_o[b] = master_req_wdata_i[winner];
    end
  end

  // Acknowledge winners
  always_comb begin
    master_req_ready_o = '0;
    for (int b = 0; b < NumBanks; b++) begin
      if (bank_granted[b]) begin
        master_req_ready_o[grant_idx[b]] = 1'b1;
      end
    end
  end

  // Pipeline registers for Response Routing
  logic [NumBanks-1:0][MasterIdWidth-1:0] rsp_target_q;
  logic [NumBanks-1:0]                    rsp_valid_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      rsp_valid_q <= '0;
      rsp_target_q <= '0;
    end else begin
      for (int b = 0; b < NumBanks; b++) begin
        // Only expect a response for READs
        if (bank_req_o[b] && !bank_write_o[b]) begin
          rsp_valid_q[b]  <= 1'b1;
          rsp_target_q[b] <= grant_idx[b];
        end else begin
          rsp_valid_q[b]  <= 1'b0;
        end
      end
    end
  end

  // Route bank responses back to masters
  always_comb begin
    master_rsp_valid_o = '0;
    master_rsp_rdata_o = '0;
    
    for (int b = 0; b < NumBanks; b++) begin
      if (rsp_valid_q[b]) begin
        automatic int target = rsp_target_q[b];
        master_rsp_valid_o[target] = 1'b1;
        master_rsp_rdata_o[target] = bank_rdata_i[b];
      end
    end
  end

endmodule
