#!/bin/bash
# Copyright 2026 NPU IP
#
# Script to compile and run the testbench for the NPU MAC Array.
# Supports VCS (UVM), Xcelium (UVM), and Verilator (C++ testbench).
#
# Usage:
#   ./run_mac_tb.sh [random|directed] [--sim vcs|xrun|verilator] [--num N] [--seed S]
#
# Arguments:
#   random     - Run random test  (default, 2000 constrained-random transactions)
#   directed   - Run directed test (edge case tests)
#   --sim SIM  - Force a specific simulator (vcs, xrun, verilator). Auto-detected if omitted.
#   --num N    - Number of random transactions (default 2000, Verilator only)
#   --seed S   - Random seed (Verilator only)

set -euo pipefail

TB_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${TB_DIR}/../src"
DPI_DIR="${TB_DIR}/dpi"
VERILATOR_DIR="${TB_DIR}/verilator"

# ============================================================
# Argument Parsing
# ============================================================
TEST_NAME="random"
FORCE_SIM=""
NUM_RANDOM=2000
SEED=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    random|directed) TEST_NAME="$1"; shift ;;
    --sim)           FORCE_SIM="$2"; shift 2 ;;
    --num)           NUM_RANDOM="$2"; shift 2 ;;
    --seed)          SEED="$2"; shift 2 ;;
    *)
      echo "Usage: $0 [random|directed] [--sim vcs|xrun|verilator] [--num N] [--seed S]"
      exit 1
      ;;
  esac
done

# Map test name to UVM test class
case "$TEST_NAME" in
  random)   UVM_TEST="mac_random_test" ;;
  directed) UVM_TEST="mac_directed_test" ;;
esac

# ============================================================
# Simulator Selection (auto-detect or forced)
# ============================================================
select_simulator() {
  if [[ -n "$FORCE_SIM" ]]; then
    echo "$FORCE_SIM"
    return
  fi
  if command -v vcs &> /dev/null; then
    echo "vcs"
  elif command -v xrun &> /dev/null; then
    echo "xrun"
  elif command -v verilator &> /dev/null; then
    echo "verilator"
  else
    echo "none"
  fi
}

SIM=$(select_simulator)

echo "============================================"
echo "  NPU MAC Array Testbench"
echo "  Test:      ${TEST_NAME}"
echo "  Simulator: ${SIM}"
echo "============================================"

# ============================================================
# VCS (UVM)
# ============================================================
run_vcs() {
  WORK_DIR="${TB_DIR}/work_vcs"
  mkdir -p "${WORK_DIR}"

  echo "[INFO] Compiling with VCS..."
  vcs -full64 -sverilog -ntb_opts uvm \
    -timescale=1ns/1ps \
    +incdir+"${SRC_DIR}" \
    "${SRC_DIR}/npu_compute_core_pkg.sv" \
    "${SRC_DIR}/npu_mac_array.sv" \
    "${TB_DIR}/npu_mac_array_if.sv" \
    "${TB_DIR}/npu_mac_array_tb_pkg.sv" \
    "${TB_DIR}/npu_mac_array_tb.sv" \
    "${DPI_DIR}/npu_mac_reference_model.c" \
    -o "${WORK_DIR}/simv" \
    -l "${WORK_DIR}/compile.log" \
    +define+UVM_NO_DEPRECATED \
    -cm line+cond+tgl+fsm+branch+assert

  echo "[INFO] Running simulation..."
  "${WORK_DIR}/simv" \
    +UVM_TESTNAME="${UVM_TEST}" \
    +UVM_VERBOSITY=UVM_MEDIUM \
    -l "${WORK_DIR}/sim.log" \
    -cm line+cond+tgl+fsm+branch+assert

  echo "[INFO] Coverage database: ${WORK_DIR}/simv.vdb"
  echo "[INFO] Simulation log:    ${WORK_DIR}/sim.log"
}

# ============================================================
# Xcelium (UVM)
# ============================================================
run_xrun() {
  WORK_DIR="${TB_DIR}/work_xrun"
  mkdir -p "${WORK_DIR}"

  echo "[INFO] Running with Xcelium..."
  xrun -sv -uvm -64bit \
    -timescale 1ns/1ps \
    -incdir "${SRC_DIR}" \
    "${SRC_DIR}/npu_compute_core_pkg.sv" \
    "${SRC_DIR}/npu_mac_array.sv" \
    "${TB_DIR}/npu_mac_array_if.sv" \
    "${TB_DIR}/npu_mac_array_tb_pkg.sv" \
    "${TB_DIR}/npu_mac_array_tb.sv" \
    "${DPI_DIR}/npu_mac_reference_model.c" \
    +UVM_TESTNAME="${UVM_TEST}" \
    +UVM_VERBOSITY=UVM_MEDIUM \
    -coverage all \
    -l "${WORK_DIR}/sim.log"

  echo "[INFO] Simulation log: ${WORK_DIR}/sim.log"
}

# ============================================================
# Verilator (C++ testbench — no UVM dependency)
# ============================================================
run_verilator() {
  WORK_DIR="${TB_DIR}/work_verilator"
  mkdir -p "${WORK_DIR}"

  echo "[INFO] Verilating RTL..."
  verilator --cc --exe --build \
    --trace \
    -Wall \
    -Wno-UNUSED \
    -Wno-UNDRIVEN \
    --top-module npu_mac_array \
    -I"${SRC_DIR}" \
    "${SRC_DIR}/npu_compute_core_pkg.sv" \
    "${SRC_DIR}/npu_mac_array.sv" \
    "${VERILATOR_DIR}/npu_mac_array_tb.cpp" \
    --Mdir "${WORK_DIR}/obj_dir" \
    -o "${WORK_DIR}/Vnpu_mac_array" \
    2>&1 | tee "${WORK_DIR}/compile.log"

  echo "[INFO] Running Verilator simulation..."
  SIM_ARGS="+num=${NUM_RANDOM}"
  if [[ -n "$SEED" ]]; then
    SIM_ARGS="${SIM_ARGS} +seed=${SEED}"
  fi

  "${WORK_DIR}/Vnpu_mac_array" ${SIM_ARGS} 2>&1 | tee "${WORK_DIR}/sim.log"

  echo "[INFO] Waveform: ${WORK_DIR}/mac_array_waves.vcd"
  echo "[INFO] Simulation log: ${WORK_DIR}/sim.log"
}

# ============================================================
# Dispatch
# ============================================================
case "$SIM" in
  vcs)       run_vcs ;;
  xrun)      run_xrun ;;
  verilator) run_verilator ;;
  *)
    echo "[ERROR] No supported simulator found."
    echo "[INFO]  Install one of: VCS, Xcelium, or Verilator."
    echo ""
    echo "  For Verilator (open-source):"
    echo "    sudo apt install verilator    # Debian/Ubuntu"
    echo "    brew install verilator        # macOS"
    echo ""
    exit 1
    ;;
esac

echo "============================================"
echo "  Simulation Complete"
echo "============================================"
