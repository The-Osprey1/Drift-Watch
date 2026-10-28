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
pmf-disabled|DriftWatch/net.cpp|  conf.sta.pmf_cfg.capable = true;|  conf.sta.pmf_cfg.capable = false;|network
wpa3-sae-h2e-disabled|DriftWatch/net.cpp|  conf.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;|  conf.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;|network
apostrophe-fallback-skipped|DriftWatch/net.cpp|  if (!hasCurly \&\& !hasStraight) return false;|  return false;|network
entrance-outlives-touch|DriftWatch/ui_input.cpp|  cancelEntrance(true);  // a new touch ends a running entrance, never queues behind it|  (void)0;|interface
entrance-on-wake|DriftWatch/ui_motion.cpp|  wristDrop.reset();\n  showLowNotice();|  wristDrop.reset();\n  startEntrance(currentFace());\n  showLowNotice();|interface
petal-crosses-time|DriftWatch/faces_themed.cpp|    x = x > CX ? PROTECT_X1 + m : PROTECT_X0 - m;|    (void)m;|faces
watchdog-not-fed|DriftWatch/ui.cpp|  hal::watchdogFeed();|  (void)0;|interface
auto-zone-not-restored|DriftWatch/net.cpp|  if (settings.tz.isEmpty() \&\& !settings.autoTz.isEmpty()) {|  if (false) {|network
zone-rules-unchecked|DriftWatch/net.cpp|offsetIn(rules, hal::now()) == a.offset)) return rules;|offsetIn(rules, hal::now()) != 99999)) return rules;|network
join-not-retried|DriftWatch/net.cpp|      } else if (elapsed > CONNECT_TIMEOUT_MS \&\& !joinRetried) {|      } else if (false) {|network
resync-no-backoff|DriftWatch/ui.cpp|min<uint32_t>(RESYNC_MS, RETRY_SYNC_MS << (failsInARow - 1))|RETRY_SYNC_MS|interface_network
low-battery-repeats|DriftWatch/battery.cpp|  if (!toldSoon_ \&\& level < SOON) {|  if (level < SOON) {|interface
alarm-under-face|DriftWatch/ui.cpp|    // The alarm owns the screen until it is dismissed or runs out; nothing else draws over it.\n    hal::idle(10);\n    return;|    (void)0;|interface
alarm-picture-left|DriftWatch/ui_input.cpp|  uiKey = 0;\n  lastActivity = hal::ms();\n  if (mode != Mode::Face \&\& mode != Mode::Boot \&\& tr.kind == Slide::None) refreshScreen(true);|  lastActivity = hal::ms();|interface
drawer-redraws-fast|DriftWatch/screens.cpp|      drawItem(c, it, ctl[i].visible);\n    }\n    return false;|      drawItem(c, it, ctl[i].visible);\n    }\n    return true;|interface
flashlight-never-off|DriftWatch/ui.cpp|  if (mode == Mode::Flashlight \&\& since(flashlightStart) > FLASHLIGHT_MS) closeFlashlight();|  (void)0;|interface
settings-hub-gap-lost|DriftWatch/screens.cpp|int settingsSlot(const View \&v, int item) { return !v.large \&\& item >= CLOCK_HUB_END ? item + 1 : item; }|int settingsSlot(const View \&v, int item) { return item; }|interface
serial-wake-not-armed|DriftWatch/power.cpp|  arm.serial = true;|  arm.serial = false;|interface
serial-hold-ignored|DriftWatch/power.cpp|  arm.lowPower = options.lightSleep \&\& !sleepBroken_ \&\& !in.serialHold;|  arm.lowPower = options.lightSleep \&\& !sleepBroken_;|power
settings-installed-before-save|DriftWatch/settings.cpp|  if (!persist(candidate)) return false;|  settings = candidate; if (!persist(candidate)) return false;|settings_store
settings-crc-ignored|DriftWatch/settings_record.h|get(b + record.size - CRC_BYTES, CRC_BYTES) != crc(b, record.size - CRC_BYTES)|false|settings_store
LIST
)

build() {  # test name -> binary in $WORK/bin
  local t=$1 d=$WORK/src
  local ui="$d/DriftWatch/ui.cpp $d/DriftWatch/ui_render.cpp $d/DriftWatch/ui_motion.cpp $d/DriftWatch/ui_input.cpp \
$d/DriftWatch/screens.cpp $d/DriftWatch/faces.cpp $d/DriftWatch/faces_themed.cpp $d/DriftWatch/gfx.cpp $d/DriftWatch/settings.cpp \
$d/DriftWatch/raise.cpp $d/DriftWatch/orient.cpp $d/DriftWatch/phone.cpp $d/DriftWatch/timekeep.cpp $d/DriftWatch/battery.cpp \
$d/DriftWatch/serial_cmd.cpp $d/DriftWatch/power.cpp $d/DriftWatch/timer_store.cpp $d/DriftWatch/qrcodegen.c"
  local cxx="g++ -std=c++17 -O1 -w -I $d/tools/host -I $d/DriftWatch -isystem $TFT"
  mkdir -p "$WORK/bin"
  case $t in
    network) $cxx -I $d/tools/host/net $d/tools/host/net_test.cpp $d/tools/host/net/netsim.cpp $d/DriftWatch/net.cpp \
               $d/DriftWatch/settings.cpp $d/DriftWatch/timekeep.cpp $d/DriftWatch/orient.cpp -o "$WORK/bin/$t" ;;
    power) $cxx $d/tools/host/power_test.cpp $d/DriftWatch/power.cpp $d/DriftWatch/qmi8658.cpp -o "$WORK/bin/$t" ;;
    settings_store) $cxx $d/tools/host/settings_store_test.cpp $d/DriftWatch/settings.cpp $d/DriftWatch/orient.cpp -o "$WORK/bin/$t" ;;
    faces) $cxx $d/tools/host/tft_shim.cpp $d/tools/host/render_faces.cpp $d/DriftWatch/faces.cpp \
             $d/DriftWatch/faces_themed.cpp $d/DriftWatch/gfx.cpp -o "$WORK/bin/$t" ;;
    interface_network) $cxx -I $d/tools/host/net $d/tools/host/tft_shim.cpp $d/tools/host/sim_stubs.cpp $d/tools/host/net/netsim.cpp \
                         $d/tools/host/simulate_network.cpp $d/DriftWatch/net.cpp $ui -o "$WORK/bin/$t" ;;
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
path, old, new = sys.argv[1], sys.argv[2].replace('\\n', '\n').replace('\\&', '&'), sys.argv[3].replace('\\n', '\n').replace('\\&', '&')
s = open(path).read()
if s.count(old) != 1:
    sys.exit(f"the text to change is not in {path} exactly once")
open(path, 'w').write(s.replace(old, new))
PY
  then echo "  MUTATION $name: could not be applied (the code changed; update this list)"; status=1; continue; fi
  build "$test"
  mkdir -p "$WORK/out"
  # A planted bug that sends a test into a loop counts as caught, after 5 minutes, rather than
  # holding up the whole run.
  if timeout 300 "$WORK/bin/$test" "$WORK/out" >"$WORK/log" 2>&1; then
    echo "  MUTATION $name: NOT caught by the $test test"; status=1
  else
    echo "  mutation $name: caught by the $test test ($(grep -c FAIL "$WORK/log") failed checks)"
  fi
done <<< "$MUTATIONS"
exit $status
