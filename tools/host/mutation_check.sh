#!/usr/bin/env bash
# Puts known bugs back, one at a time, into a copy of the code and checks that the tests catch
# each one. A test suite that still passes with a bug in place is not testing that bug.
# Usage: tools/host/mutation_check.sh path/to/TFT_eSPI
set -euo pipefail
cd "$(dirname "$0")/../.."
TFT=${1:?give the path to the TFT_eSPI source}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# name | file | text to find | what to put instead | test that must fail
MUTATIONS=$(cat <<'LIST'
auto-zone-saved|DriftWatch/net.cpp|  strlcpy(posixTz, settings.tz.isEmpty() ? DEFAULT_POSIX_TZ : settings.tz.c_str(), sizeof posixTz);|  if (settings.tz.isEmpty()) { settings.tz = DEFAULT_POSIX_TZ; saveSettings(); }  strlcpy(posixTz, settings.tz.c_str(), sizeof posixTz);|network
motion-lights-screen|DriftWatch/power.cpp|  motionLine(t1);\n  return Wake::None;|  motionLine(t1);\n  return Wake::Gesture;|power
motion-lights-screen-ui|DriftWatch/power.cpp|  motionLine(t1);\n  return Wake::None;|  motionLine(t1);\n  return Wake::Gesture;|interface
late-zone-answer-used|DriftWatch/net.cpp|bool answered = zoneEpoch && zoneReady.load(std::memory_order_acquire) == zoneEpoch;|bool answered = zoneReady.load(std::memory_order_acquire) != 0;|network
time-client-left-running|DriftWatch/net.cpp|  if (sntp_enabled()) sntp_stop();|  (void)0;|network
truncated-reply-used|DriftWatch/net.cpp|  return *p == '{' \&\& end - p >= 2 \&\& end[-1] == '}';|  return *p == '{';|network
owed-nudge-ignored|DriftWatch/timekeep.cpp|((double)errorMs + owedMs_)|((double)errorMs)|network
LIST
)

build() {  # test name -> binary in $WORK/bin
  local t=$1 d=$WORK/src
  local ui="$d/DriftWatch/ui.cpp $d/DriftWatch/screens.cpp $d/DriftWatch/faces.cpp $d/DriftWatch/gfx.cpp $d/DriftWatch/settings.cpp \
$d/DriftWatch/raise.cpp $d/DriftWatch/orient.cpp $d/DriftWatch/phone.cpp $d/DriftWatch/timekeep.cpp $d/DriftWatch/battery.cpp \
$d/DriftWatch/serial_cmd.cpp $d/DriftWatch/power.cpp"
  local cxx="g++ -std=c++17 -O1 -w -I $d/tools/host -I $d/DriftWatch -isystem $TFT"
  mkdir -p "$WORK/bin"
  case $t in
    network) $cxx -I $d/tools/host/net $d/tools/host/net_test.cpp $d/tools/host/net/netsim.cpp $d/DriftWatch/net.cpp \
               $d/DriftWatch/settings.cpp $d/DriftWatch/timekeep.cpp $d/DriftWatch/orient.cpp -o "$WORK/bin/$t" ;;
    power) $cxx $d/tools/host/power_test.cpp $d/DriftWatch/power.cpp $d/DriftWatch/qmi8658.cpp -o "$WORK/bin/$t" ;;
    interface) $cxx $d/tools/host/tft_shim.cpp $d/tools/host/sim_stubs.cpp $d/tools/host/sim_net_fake.cpp \
                 $d/tools/host/simulate.cpp $ui -o "$WORK/bin/$t" ;;
  esac
}

status=0
while IFS='|' read -r name file find replace test; do
  [ -z "$name" ] && continue
  rm -rf "$WORK/src" && mkdir -p "$WORK/src" && cp -r DriftWatch tools "$WORK/src/"
  if ! python3 - "$WORK/src/$file" "$find" "$replace" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2].replace('\\n', '\n').replace('\\&', '&'), sys.argv[3].replace('\\n', '\n')
s = open(path).read()
if s.count(old) != 1:
    sys.exit(f"the text to change is not in {path} exactly once")
open(path, 'w').write(s.replace(old, new))
PY
  then echo "  MUTATION $name: could not be applied (the code changed; update this list)"; status=1; continue; fi
  build "$test"
  mkdir -p "$WORK/out"
  if "$WORK/bin/$test" "$WORK/out" >"$WORK/log" 2>&1; then
    echo "  MUTATION $name: NOT caught by the $test test"; status=1
  else
    echo "  mutation $name: caught by the $test test ($(grep -c FAIL "$WORK/log") failed checks)"
  fi
done <<< "$MUTATIONS"
exit $status
