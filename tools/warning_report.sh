#!/usr/bin/env bash
# Sorts the compiler warnings in a firmware build log by where they come from, so our own code
# can be held to none while warnings in the ESP32 core and the libraries are recorded, not hidden.
# Usage: tools/warning_report.sh build.log > report.md
# Exit code 1 if any warning is in DriftWatch/ (our code).
set -euo pipefail
LOG=${1:?give the build log}
W=$(grep -E ': warning: ' "$LOG" | sort -u || true)
ours=$(printf '%s\n' "$W" | grep -E '(^|/)DriftWatch/' | grep -c . || true)
core=$(printf '%s\n' "$W" | grep -E '/(hardware|packages)/esp32/' | grep -c . || true)
libs=$(printf '%s\n' "$W" | grep -E '/libraries/' | grep -vE '(^|/)DriftWatch/' | grep -c . || true)
total=$(printf '%s\n' "$W" | grep -c . || true)
echo "# Firmware compiler warnings"
echo
echo "| Where | Warnings |"
echo "|---|---|"
echo "| Drift-Watch code (DriftWatch/) | $ours |"
echo "| ESP32 Arduino core | $core |"
echo "| Libraries (TFT_eSPI, WiFiManager and the core's own) | $libs |"
echo "| All, without duplicates | $total |"
echo
if [ "$total" -gt 0 ]; then
  echo '```'
  printf '%s\n' "$W" | sed -E 's#^.*/(DriftWatch|libraries|esp32)/#\1/#' | head -200
  echo '```'
fi
[ "$ours" -eq 0 ]
