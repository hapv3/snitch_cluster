// Copyright 2026 NPU IP
// Solderpad Hardware License, Version 0.51

/// UVM Package for NPU MAC Array Verification
package npu_mac_array_tb_pkg;

  import uvm_pkg::*;
  `include "uvm_macros.svh"

  import npu_compute_core_pkg::*;

  // ============================================================
  // Transaction
  // ============================================================
  class mac_transaction extends uvm_sequence_item;
    `uvm_object_utils(mac_transaction)

    rand logic signed [NpuActWidth-1:0] activations [NpuMacsPerCore];
    rand logic signed [NpuWgtWidth-1:0] weights     [NpuMacsPerCore];
    rand logic                          clear_acc;

    // Expected result (populated by scoreboard reference model)
    logic signed [NpuAccWidth-1:0] expected_acc;

    function new(string name = "mac_transaction");
      super.new(name);
    endfunction

    // Constrain to exercise full INT8 range including edge cases
    constraint c_data_range {
      foreach (activations[i]) {
        activations[i] inside {[-128:127]};
      }
      foreach (weights[i]) {
        weights[i] inside {[-128:127]};
      }
    }

    // Bias toward interesting edge cases 10% of the time
    constraint c_edge_cases {
      foreach (activations[i]) {
        activations[i] dist {
          -128      := 2,
          [-127:-1] := 43,
          0         := 5,
          [1:126]   := 43,
          127       := 2
        };
      }
      foreach (weights[i]) {
        weights[i] dist {
          -128      := 2,
          [-127:-1] := 43,
          0         := 5,
          [1:126]   := 43,
          127       := 2
        };
      }
    }

    constraint c_clear_acc_dist {
      clear_acc dist { 1'b1 := 30, 1'b0 := 70 };
    }

    function string convert2string();
      return $sformatf("clear_acc=%0b act[0]=%0d wgt[0]=%0d",
                       clear_acc, activations[0], weights[0]);
    endfunction

  endclass


  // ============================================================
  // Sequence: Constrained Random
  // ============================================================
  class mac_random_sequence extends uvm_sequence #(mac_transaction);
    `uvm_object_utils(mac_random_sequence)

    int num_transactions = 1000;

    function new(string name = "mac_random_sequence");
      super.new(name);
    endfunction

    task body();
      mac_transaction txn;
      for (int i = 0; i < num_transactions; i++) begin
        txn = mac_transaction::type_id::create($sformatf("txn_%0d", i));
        start_item(txn);
        if (!txn.randomize())
          `uvm_fatal("SEQ", "Randomization failed")
        finish_item(txn);
      end
    endtask

  endclass


  // ============================================================
  // Sequence: Directed Edge Cases
  // ============================================================
  class mac_directed_sequence extends uvm_sequence #(mac_transaction);
    `uvm_object_utils(mac_directed_sequence)

    function new(string name = "mac_directed_sequence");
      super.new(name);
    endfunction

    task body();
      mac_transaction txn;

      // Test 1: All zeros
      txn = mac_transaction::type_id::create("txn_zeros");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = 0;
      foreach (txn.weights[i])     txn.weights[i] = 0;
      txn.clear_acc = 1;
      finish_item(txn);

      // Test 2: All max positive
      txn = mac_transaction::type_id::create("txn_max_pos");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = 8'sd127;
      foreach (txn.weights[i])     txn.weights[i] = 8'sd127;
      txn.clear_acc = 1;
      finish_item(txn);

      // Test 3: All max negative
      txn = mac_transaction::type_id::create("txn_max_neg");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = -8'sd128;
      foreach (txn.weights[i])     txn.weights[i] = -8'sd128;
      txn.clear_acc = 1;
      finish_item(txn);

      // Test 4: Mixed extremes (pos * neg)
      txn = mac_transaction::type_id::create("txn_mixed");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = 8'sd127;
      foreach (txn.weights[i])     txn.weights[i] = -8'sd128;
      txn.clear_acc = 1;
      finish_item(txn);

      // Test 5: Accumulation test — two consecutive with clear_acc=0
      txn = mac_transaction::type_id::create("txn_acc_base");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = 8'sd1;
      foreach (txn.weights[i])     txn.weights[i] = 8'sd1;
      txn.clear_acc = 1;
      finish_item(txn);

      txn = mac_transaction::type_id::create("txn_acc_add");
      start_item(txn);
      foreach (txn.activations[i]) txn.activations[i] = 8'sd2;
      foreach (txn.weights[i])     txn.weights[i] = 8'sd3;
      txn.clear_acc = 0; // Should accumulate on top of previous
      finish_item(txn);

    endtask

  endclass


  // ============================================================
  // Driver
  // ============================================================
  class mac_driver extends uvm_driver #(mac_transaction);
    `uvm_component_utils(mac_driver)

    virtual npu_mac_array_if vif;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      if (!uvm_config_db#(virtual npu_mac_array_if)::get(this, "", "vif", vif))
        `uvm_fatal("DRV", "Could not get virtual interface")
    endfunction

    task run_phase(uvm_phase phase);
      mac_transaction txn;
      forever begin
        seq_item_port.get_next_item(txn);

        @(posedge vif.clk);
        vif.valid_i     <= 1'b1;
        vif.clear_acc_i <= txn.clear_acc;
        for (int i = 0; i < NpuMacsPerCore; i++) begin
          vif.act_i[i] <= txn.activations[i];
          vif.wgt_i[i] <= txn.weights[i];
        end

        @(posedge vif.clk);
        vif.valid_i <= 1'b0;

        seq_item_port.item_done();
      end
    endtask

  endclass


  // ============================================================
  // Monitor (Input Side)
  // ============================================================
  class mac_input_monitor extends uvm_monitor;
    `uvm_component_utils(mac_input_monitor)

    virtual npu_mac_array_if vif;
    uvm_analysis_port #(mac_transaction) ap;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      ap = new("ap", this);
      if (!uvm_config_db#(virtual npu_mac_array_if)::get(this, "", "vif", vif))
        `uvm_fatal("MON", "Could not get virtual interface")
    endfunction

    task run_phase(uvm_phase phase);
      mac_transaction txn;
      forever begin
        @(posedge vif.clk);
        if (vif.valid_i) begin
          txn = mac_transaction::type_id::create("mon_txn");
          txn.clear_acc = vif.clear_acc_i;
          for (int i = 0; i < NpuMacsPerCore; i++) begin
            txn.activations[i] = vif.act_i[i];
            txn.weights[i]     = vif.wgt_i[i];
          end
          ap.write(txn);
        end
      end
    endtask

  endclass


  // ============================================================
  // Monitor (Output Side)
  // ============================================================
  class mac_output_monitor extends uvm_monitor;
    `uvm_component_utils(mac_output_monitor)

    virtual npu_mac_array_if vif;
    uvm_analysis_port #(mac_transaction) ap;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      ap = new("ap", this);
      if (!uvm_config_db#(virtual npu_mac_array_if)::get(this, "", "vif", vif))
        `uvm_fatal("MON_OUT", "Could not get virtual interface")
    endfunction

    task run_phase(uvm_phase phase);
      mac_transaction txn;
      forever begin
        @(posedge vif.clk);
        if (vif.valid_o) begin
          txn = mac_transaction::type_id::create("out_txn");
          txn.expected_acc = vif.acc_o;
          ap.write(txn);
        end
      end
    endtask

  endclass


  // ============================================================
  // Scoreboard with DPI-C Reference Model
  // ============================================================

  // DPI-C import declaration
  import "DPI-C" function int npu_mac_reference_model(
    input byte activations[],
    input byte weights[],
    input int  num_macs,
    input int  clear_acc,
    input int  prev_acc
  );

  class mac_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(mac_scoreboard)

    uvm_analysis_imp #(mac_transaction, mac_scoreboard) input_imp;
    mac_transaction input_queue[$];

    int expected_acc;
    int pass_count;
    int fail_count;
    int total_count;

    function new(string name, uvm_component parent);
      super.new(name, parent);
      expected_acc = 0;
      pass_count = 0;
      fail_count = 0;
      total_count = 0;
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      input_imp = new("input_imp", this);
    endfunction

    function void write(mac_transaction txn);
      // Compute expected result using DPI-C reference model
      byte act_arr[];
      byte wgt_arr[];
      act_arr = new[NpuMacsPerCore];
      wgt_arr = new[NpuMacsPerCore];

      for (int i = 0; i < NpuMacsPerCore; i++) begin
        act_arr[i] = byte'(txn.activations[i]);
        wgt_arr[i] = byte'(txn.weights[i]);
      end

      expected_acc = npu_mac_reference_model(
        act_arr, wgt_arr,
        NpuMacsPerCore,
        int'(txn.clear_acc),
        expected_acc
      );

      input_queue.push_back(txn);
    endfunction

    // Called by the output monitor via a separate analysis port
    function void check_output(logic signed [NpuAccWidth-1:0] actual_acc);
      total_count++;
      if (actual_acc == expected_acc) begin
        pass_count++;
        `uvm_info("SCB", $sformatf("[PASS %0d] Expected=%0d, Got=%0d",
                  total_count, expected_acc, actual_acc), UVM_MEDIUM)
      end else begin
        fail_count++;
        `uvm_error("SCB", $sformatf("[FAIL %0d] Expected=%0d, Got=%0d",
                  total_count, expected_acc, actual_acc))
      end
    endfunction

    function void report_phase(uvm_phase phase);
      `uvm_info("SCB", $sformatf(
        "\n========================================\n  SCOREBOARD SUMMARY\n  Total:  %0d\n  Passed: %0d\n  Failed: %0d\n========================================",
        total_count, pass_count, fail_count), UVM_NONE)
      if (fail_count > 0) begin
        `uvm_error("SCB", "TEST FAILED: Mismatches detected!")
      end else begin
        `uvm_info("SCB", "TEST PASSED: All outputs matched reference model.", UVM_NONE)
      end
    endfunction

  endclass


  // ============================================================
  // Agent
  // ============================================================
  class mac_agent extends uvm_agent;
    `uvm_component_utils(mac_agent)

    mac_driver        drv;
    uvm_sequencer #(mac_transaction) seqr;
    mac_input_monitor in_mon;
    mac_output_monitor out_mon;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      drv     = mac_driver::type_id::create("drv", this);
      seqr    = uvm_sequencer#(mac_transaction)::type_id::create("seqr", this);
      in_mon  = mac_input_monitor::type_id::create("in_mon", this);
      out_mon = mac_output_monitor::type_id::create("out_mon", this);
    endfunction

    function void connect_phase(uvm_phase phase);
      super.connect_phase(phase);
      drv.seq_item_port.connect(seqr.seq_item_export);
    endfunction

  endclass


  // ============================================================
  // Environment
  // ============================================================
  class mac_env extends uvm_env;
    `uvm_component_utils(mac_env)

    mac_agent      agent;
    mac_scoreboard scb;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      agent = mac_agent::type_id::create("agent", this);
      scb   = mac_scoreboard::type_id::create("scb", this);
    endfunction

    function void connect_phase(uvm_phase phase);
      super.connect_phase(phase);
      agent.in_mon.ap.connect(scb.input_imp);
    endfunction

  endclass


  // ============================================================
  // Test: Random
  // ============================================================
  class mac_random_test extends uvm_test;
    `uvm_component_utils(mac_random_test)

    mac_env env;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      env = mac_env::type_id::create("env", this);
    endfunction

    task run_phase(uvm_phase phase);
      mac_random_sequence seq;
      phase.raise_objection(this);
      seq = mac_random_sequence::type_id::create("seq");
      seq.num_transactions = 2000;
      seq.start(env.agent.seqr);
      // Wait for pipeline to flush
      #100;
      phase.drop_objection(this);
    endtask

  endclass


  // ============================================================
  // Test: Directed
  // ============================================================
  class mac_directed_test extends uvm_test;
    `uvm_component_utils(mac_directed_test)

    mac_env env;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      env = mac_env::type_id::create("env", this);
    endfunction

    task run_phase(uvm_phase phase);
      mac_directed_sequence seq;
      phase.raise_objection(this);
      seq = mac_directed_sequence::type_id::create("seq");
      seq.start(env.agent.seqr);
      #200;
      phase.drop_objection(this);
    endtask

  endclass

endpackage
