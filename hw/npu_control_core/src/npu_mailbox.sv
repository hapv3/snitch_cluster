// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// Command Mailbox for the NPU Control Core.
/// Allows the ARM Host to send task descriptors (e.g., Conv2D configuration, tensor addresses)
/// to the NPU, and allows the NPU to send status updates or interrupts back.

module npu_mailbox #(
  parameter int unsigned AddrWidth = 32,
  parameter int unsigned DataWidth = 32
) (
  input  logic clk_i,
  input  logic rst_ni,

  // Interface from ARM Host (AXI4-Lite or generic Req/Rsp)
  input  logic                 host_req_valid_i,
  input  logic                 host_req_write_i,
  input  logic [AddrWidth-1:0] host_req_addr_i,
  input  logic [DataWidth-1:0] host_req_data_i,
  output logic                 host_req_ready_o,
  
  output logic                 host_rsp_valid_o,
  output logic [DataWidth-1:0] host_rsp_data_o,
  input  logic                 host_rsp_ready_i,

  // Interface to Snitch Control Core (Req/Rsp)
  input  logic                 core_req_valid_i,
  input  logic                 core_req_write_i,
  input  logic [AddrWidth-1:0] core_req_addr_i,
  input  logic [DataWidth-1:0] core_req_data_i,
  output logic                 core_req_ready_o,

  output logic                 core_rsp_valid_o,
  output logic [DataWidth-1:0] core_rsp_data_o,
  input  logic                 core_rsp_ready_i,

  // Interrupts
  output logic                 irq_to_core_o, // NPU task available
  output logic                 irq_to_host_o  // NPU task complete
);

  // Mailbox registers
  // 0x00: STATUS (Bit 0: Task pending, Bit 1: Task complete)
  // 0x04: CONTROL (Write 1 to clear task pending / complete)
  // 0x08-0x3C: TASK DESCRIPTOR (14 words)
  
  logic [DataWidth-1:0] registers [16];
  
  logic task_pending_q, task_complete_q;

  // Host access logic
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      host_rsp_valid_o <= 1'b0;
      host_rsp_data_o <= '0;
      task_pending_q <= 1'b0;
      for (int i = 0; i < 16; i++) registers[i] <= '0;
    end else begin
      // Default response
      if (host_rsp_valid_o && host_rsp_ready_i) begin
        host_rsp_valid_o <= 1'b0;
      end
      
      host_req_ready_o <= !host_rsp_valid_o;
      
      if (host_req_valid_i && host_req_ready_o) begin
        automatic logic [3:0] reg_idx = host_req_addr_i[5:2];
        host_rsp_valid_o <= 1'b1;
        
        if (host_req_write_i) begin
          if (reg_idx >= 2 && reg_idx < 16) begin
            registers[reg_idx] <= host_req_data_i;
          end else if (reg_idx == 1) begin // CONTROL
            if (host_req_data_i[0]) task_pending_q <= 1'b1; // Trigger task
          end
        end else begin
          if (reg_idx == 0) begin
            host_rsp_data_o <= {30'd0, task_complete_q, task_pending_q};
          end else begin
            host_rsp_data_o <= registers[reg_idx];
          end
        end
      end
      
      // Clear pending if core acknowledges
      if (core_req_valid_i && core_req_write_i && core_req_addr_i[5:2] == 1 && core_req_data_i[0]) begin
        task_pending_q <= 1'b0;
      end
    end
  end

  // Core access logic
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      core_rsp_valid_o <= 1'b0;
      core_rsp_data_o <= '0;
      task_complete_q <= 1'b0;
    end else begin
      if (core_rsp_valid_o && core_rsp_ready_i) begin
        core_rsp_valid_o <= 1'b0;
      end
      
      core_req_ready_o <= !core_rsp_valid_o;
      
      if (core_req_valid_i && core_req_ready_o) begin
        automatic logic [3:0] reg_idx = core_req_addr_i[5:2];
        core_rsp_valid_o <= 1'b1;
        
        if (core_req_write_i) begin
           if (reg_idx == 1) begin // CONTROL
             if (core_req_data_i[1]) task_complete_q <= 1'b1; // Signal task complete
           end
        end else begin
          if (reg_idx == 0) begin
            core_rsp_data_o <= {30'd0, task_complete_q, task_pending_q};
          end else begin
            core_rsp_data_o <= registers[reg_idx];
          end
        end
      end
      
      // Clear complete if host acknowledges
      if (host_req_valid_i && host_req_write_i && host_req_addr_i[5:2] == 1 && host_req_data_i[1]) begin
        task_complete_q <= 1'b0;
      end
    end
  end

  assign irq_to_core_o = task_pending_q;
  assign irq_to_host_o = task_complete_q;

endmodule
