#!/usr/bin/env bash
# Builds and runs every computer test: the face check, the interface simulation and the
# network code test. Needs g++ and the TFT_eSPI 2.5.43 source (for its fonts).
# Usage: tools/host/run_tests.sh path/to/TFT_eSPI [output folder, default tools/host/out]
set -euo pipefail
cd "$(dirname "$0")/../.."
TFT=${1:?give the path to the TFT_eSPI source}
OUT=${2:-tools/host/out}
BIN=$OUT/bin
mkdir -p "$BIN"
CXX="g++ -std=c++17 -O1 -Wall"

$CXX -I tools/host -I DriftWatch -I "$TFT" tools/host/tft_shim.cpp tools/host/render_faces.cpp \
  DriftWatch/faces.cpp DriftWatch/gfx.cpp -o "$BIN/render_faces"
$CXX -I tools/host -I DriftWatch -I "$TFT" tools/host/tft_shim.cpp tools/host/sim_stubs.cpp tools/host/simulate.cpp \
  DriftWatch/ui.cpp DriftWatch/screens.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp DriftWatch/settings.cpp \
  DriftWatch/raise.cpp DriftWatch/orient.cpp DriftWatch/phone.cpp DriftWatch/timekeep.cpp DriftWatch/battery.cpp \
  DriftWatch/serial_cmd.cpp -o "$BIN/simulate"
# The real net.cpp, against the Wi-Fi, web and time-server stand-ins in tools/host/net.
$CXX -I tools/host/net -I tools/host -I DriftWatch tools/host/net_test.cpp tools/host/net/netsim.cpp \
  DriftWatch/net.cpp DriftWatch/settings.cpp DriftWatch/timekeep.cpp DriftWatch/orient.cpp -o "$BIN/net_test"

status=0
echo "== Faces";      "$BIN/render_faces" "$OUT"      || status=1
echo "== Interface";  "$BIN/simulate" "$OUT" 2>/dev/null | grep -E "FAIL|checks passed" || status=1
echo "== Network";    "$BIN/net_test"                 || status=1
exit $status
