#!/usr/bin/env bash
# Builds and runs every computer test: the face check, the interface simulation (with the
# network scripted), the interface and network code together, the network code on its own,
# the motion sensor and screen-off power checks, and headless command parsing. Needs g++ and the TFT_eSPI 2.5.43 source
# (for its fonts).
# Usage: tools/host/run_tests.sh path/to/TFT_eSPI [output folder, default tools/host/out]
# HOST_STRICT=1 treats warnings in our own code as errors and adds the address and undefined
# behaviour sanitizers, as the CI does. TFT_eSPI is included as a system folder, so its own
# warnings are not ours to fix.
set -euo pipefail
cd "$(dirname "$0")/../.."
TFT=${1:?give the path to the TFT_eSPI source}
OUT=${2:-tools/host/out}
BIN=$OUT/bin
mkdir -p "$BIN"
FLAGS="-std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
if [ "${HOST_STRICT:-0}" = "1" ]; then
  FLAGS="$FLAGS -Werror -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
  export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1}
  export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
fi
CXX="g++ $FLAGS"
UI_SRC="DriftWatch/ui.cpp DriftWatch/ui_render.cpp DriftWatch/ui_motion.cpp DriftWatch/ui_input.cpp \
DriftWatch/screens.cpp DriftWatch/faces.cpp DriftWatch/faces_themed.cpp DriftWatch/gfx.cpp DriftWatch/settings.cpp \
DriftWatch/raise.cpp DriftWatch/orient.cpp DriftWatch/phone.cpp DriftWatch/timekeep.cpp DriftWatch/battery.cpp \
DriftWatch/serial_cmd.cpp DriftWatch/power.cpp DriftWatch/timer_store.cpp DriftWatch/qrcodegen.c"

$CXX -I tools/host -I DriftWatch -isystem "$TFT" tools/host/tft_shim.cpp tools/host/render_faces.cpp \
  DriftWatch/faces.cpp DriftWatch/faces_themed.cpp DriftWatch/gfx.cpp -o "$BIN/render_faces"
# The interface with the network replaced by a script (sim_net_fake.cpp).
$CXX -I tools/host -I DriftWatch -isystem "$TFT" tools/host/tft_shim.cpp tools/host/sim_stubs.cpp \
  tools/host/sim_net_fake.cpp tools/host/simulate.cpp $UI_SRC -o "$BIN/simulate"
# The interface and the real net.cpp together, against the stand-ins in tools/host/net.
$CXX -I tools/host/net -I tools/host -I DriftWatch -isystem "$TFT" tools/host/tft_shim.cpp tools/host/sim_stubs.cpp \
  tools/host/net/netsim.cpp tools/host/simulate_network.cpp DriftWatch/net.cpp $UI_SRC -o "$BIN/simulate_network"
# The real net.cpp on its own, with the real settings.cpp and timekeep.cpp.
$CXX -I tools/host/net -I tools/host -I DriftWatch tools/host/net_test.cpp tools/host/net/netsim.cpp \
  DriftWatch/net.cpp DriftWatch/settings.cpp DriftWatch/timekeep.cpp DriftWatch/orient.cpp -o "$BIN/net_test"
# The motion sensor's register sequences and the screen-off power controller.
$CXX -I tools/host -I DriftWatch tools/host/power_test.cpp DriftWatch/power.cpp DriftWatch/qmi8658.cpp -o "$BIN/power_test"
# Real command parsers, with only time/settings side effects replaced by test doubles.
$CXX -I tools/host -I DriftWatch tools/host/command_test.cpp DriftWatch/serial_cmd.cpp DriftWatch/phone.cpp -o "$BIN/command_test"
$CXX -I tools/host -I DriftWatch tools/host/timer_store_test.cpp DriftWatch/timer_store.cpp -o "$BIN/timer_store_test"
$CXX -I DriftWatch tools/host/clock_status_test.cpp -o "$BIN/clock_status_test"
$CXX -I DriftWatch tools/host/status_fields_test.cpp -o "$BIN/status_fields_test"
$CXX -I DriftWatch tools/host/ble_status_test.cpp -o "$BIN/ble_status_test"

status=0
run() {  # name, command...: runs one test, keeps its whole output in the output folder
  local name=$1; shift
  echo "== $name"
  if "$@" >"$OUT/$name.log" 2>"$OUT/$name.stderr.log"; then :; else status=1; fi
  grep -E "FAIL|LAYOUT|EDGE|STATE|PHANTOM|ENTRANCE|CHARGE|checks passed|problems|ERROR|runtime error" "$OUT/$name.log" "$OUT/$name.stderr.log" \
    | sed -E 's/^[^:]*:/  /' || true
}
run faces            "$BIN/render_faces" "$OUT"
run interface        "$BIN/simulate" "$OUT"
run interface_network "$BIN/simulate_network" "$OUT"
run network          "$BIN/net_test"
run power            "$BIN/power_test"
run commands         "$BIN/command_test"
run timer_store      "$BIN/timer_store_test"
run clock_status     "$BIN/clock_status_test"
run status_fields    "$BIN/status_fields_test"
run ble_status       "$BIN/ble_status_test"
exit $status
