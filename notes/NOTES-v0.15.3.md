# Drift-Watch v0.15.3 — daily-driver foundation

**Unreleased candidate. Hardware acceptance is pending.** This starts the standalone [v1.0 roadmap](DriftWatch-v1.0-roadmap.md). It does not claim the v1.0 battery, clock, wake or wear-test targets have passed.

## Changes

- Settings-preserving application-update artifacts are separate from the existing complete factory/rescue image. Each build has a manifest describing the target, layout, offsets, image hashes and update preconditions. Factory/rescue flashing can erase settings; an application-only update preserves them only with the documented compatible layout and app slot.
- Serial and phone numeric commands reject incomplete, malformed, overflowing and out-of-range arguments before changing state. Valid command aliases remain supported. The phone's long FORMAT= alias now reads the correct value.
- The companion distinguishes Virtual Watch data from physical measurements, reports command failures, respects transport support, and redacts password-bearing commands from its console.
- Packaging, command parsing and companion regressions join the automated checks. The roadmap and acceptance record are versioned with the source.

## Qualification record

Record actual results here before publishing a release. A passing host simulation is not a physical-watch result.

| Area | Status |
|---|---|
| Image packaging and manifest tests | Pending final integration run |
| Companion regressions | Pending final integration run |
| Headless command parsing | Pending execution; target syntax cross-compile checked separately |
| Strict host and mutation suites | Pending CI |
| Normal and diagnostic firmware builds | Pending CI |
| Settings-preserving update on the watch | Not run |
| Factory/rescue recovery on the watch | Not run |
| Sleeping-watch USB/BLE connections | Not run for this candidate |
| Timer alarm / Settings repaint / flashlight timeout | Not run for this candidate |
| Runtime / clock accuracy characterization | Not run |

## First hardware pass

Use the [acceptance checklist](DriftWatch-v1.0-acceptance-checklist.md) and record board, battery, source commit and firmware hash.

1. Keep the previous known-good image and documented USB rescue route available.
2. Seed nondefault settings and saved Wi-Fi, then exercise the documented compatible application-only update. Confirm the promised values survive.
3. Confirm USB time sync while the screen is off, BLE settings readback and correct disconnect handling.
4. Run a one-minute timer on Radar and Phantom with the screen off; dismiss an alarm in Settings; confirm the two-minute flashlight timeout.
5. Begin the first measured battery profile and 72-hour clock study. Do not change shipping defaults until the measurements support them.

No watch is flashed automatically by this change. The candidate remains unreleased until software checks and the required hardware acceptance are recorded.
