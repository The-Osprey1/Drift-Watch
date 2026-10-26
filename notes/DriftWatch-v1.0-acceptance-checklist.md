# DriftWatch v1.0 acceptance checklist and test record

Companion to [the v1.0 roadmap](DriftWatch-v1.0-roadmap.md). Prepared 26 October 2026.

**Initial status: NOT RUN.** Every checkbox below is an unperformed acceptance item. Attach actual evidence before marking a result. Use a separate copy for each candidate.

## Candidate identity

| Field | Record |
|---|---|
| Candidate version / date | |
| Full source commit | |
| Firmware SHA-256 / artifact URL | |
| Normal or diagnostic / build flags | |
| Toolchain / library / manifest version | |
| Board model / revision / device label | |
| Battery model / rated capacity / age | |
| Case / strap / assembly revision | |
| Shipping settings / face / brightness / motion / wake mode | |
| Tester / client OS / browser / transport | |
| Accepted roadmap target revision | |

## A. Baseline and feasibility

- [ ] Known-good firmware and rescue instructions are available locally before flashing.
- [ ] Record which current functions are implemented, partially implemented or absent.
- [ ] Record battery full-charge criterion, normal voltage behavior and warning thresholds.
- [ ] Measure screen-off/still, screen-off/walking, awake/static, awake/animated, Wi-Fi join/retry, timer alert and flashlight current.
- [ ] Compare power features with otherwise matching builds and USB disconnected; record diagnostic logging differences.
- [ ] Run the first Profile A discharge trial and 72-hour reference clock study.
- [ ] Agree G01–G14 thresholds and shipping assumptions before beta; record any deliberate adjustment.
- [ ] Record the selected support matrix and supported source settings schemas.

Result / evidence / next action:

## B. Preserving update, install and recovery — G07, G10, G12

- [ ] Factory/rescue and preserving-update artifacts have distinct names and instructions.
- [ ] Preserving updater verifies board, partition layout, target slot, image size and integrity before writing.
- [ ] Seed nondefault faces, brightness, motion, time format, zone, night mode, touch calibration and saved Wi-Fi.
- [ ] Upgrade from the supported previous release; each promised value remains correct.
- [ ] Repeat upgrade with nondefault new tool/reminder settings after those exist.
- [ ] Test the stated downgrade path; incompatible schema behavior matches the documentation.
- [ ] Interrupt an update at a controlled point; follow the published USB rescue instructions successfully.
- [ ] BOOT+RESET is physically accessible in the final enclosure.
- [ ] A mismatched/corrupt image is rejected by the normal update workflow.
- [ ] Fresh installation reaches correct local time within 10 minutes.
- [ ] Rescue reaches a usable watch within 10 minutes without a development environment.
- [ ] Factory reset clearly identifies what will be erased.
- [ ] Record any unavoidable reset or power-loss interruption of active tools.

| Trial | Source → target | Preserved values checked | Duration | Result / evidence |
|---|---|---|---|---|
| Fresh install | | | | |
| Preserving update | | | | |
| Downgrade | | | | |
| Interrupted update + rescue | | | | |
| Wrong/corrupt image | | | | |

## C. Clock and zones — G02, G06

- [ ] Fresh/full-power-loss start shows time unset until it is set; saved timestamps do not masquerade as current time.
- [ ] Manual entry works with no network and records manual time quality.
- [ ] NTP sync sets UTC, reports source/age and preserves selected zone policy.
- [ ] USB and BLE sync set both time and zone when advertised as supported.
- [ ] Unsupported firmware receives an honest capability/upgrade message.
- [ ] Missing network, failed lookup and missing NTP cannot trap the UI or continuously run radios.
- [ ] Saved/manual zones work offline without the third-party IP lookup.
- [ ] Test UTC, positive/negative offsets, half/quarter-hour zones, date boundaries and leap day.
- [ ] Test home/world-zone changes and both DST transitions.
- [ ] Test software reset, watchdog reset and power-on separately; time quality matches what actually survives.
- [ ] Learn correction only from qualifying reference samples; manual changes/outliers do not corrupt it.
- [ ] Record worst absolute error, not just the average.

Reference procedure: synchronize to an independent reference, record initial error, then prevent automatic correction during the offline test. Log error at 6/24/48/72 hours, sleep/awake mix and observed board temperatures. Repeat after calibration. Use ordinary room-to-wrist conditions and record their range. Verify return to automatic sync afterward.

| Sample | Reference UTC | Watch UTC | Error seconds | Temperature / use | Calibration state |
|---|---|---|---|---|---|
| Start | | | | | |
| 6 h | | | | | |
| 24 h | | | | | |
| 48 h | | | | | |
| 72 h | | | | | |

G02 candidate target: within 2 seconds after qualified sync and no more than 10 seconds error after 24 hours offline following calibration.

## D. Runtime, battery and charging — G01

- [ ] Use the final battery/enclosure, release firmware and documented shipping defaults.
- [ ] Record Profiles A/B/C exactly; no USB power during discharge measurements.
- [ ] Record actual screen-on/radio-on time, not just scripted counts.
- [ ] Complete three Profile A runs to the 5% warning.
- [ ] Each Profile A run meets the accepted 24-hour target.
- [ ] Characterize Radar/Phantom, Expressive motion, high brightness and unavailable Wi-Fi.
- [ ] Low-battery warnings arrive before ordinary-use brownout in every discharge run.
- [ ] Low battery preserves the face, BOOT/touch, running timer and saved settings.
- [ ] Voltage/percent/charging/time-left labels distinguish measurement from inference.
- [ ] Time remaining stays unavailable while evidence is insufficient.
- [ ] Charge/unplug transitions do not produce repeated warnings or false certainty.
- [ ] Charging and ordinary-use temperatures in the final case remain within the chosen component/pack ratings.

| Run | Profile / settings | Full → 5% duration | Screen/radio time | Restarts | Warning reserve / evidence |
|---|---|---|---|---|---|
| A1 | | | | | |
| A2 | | | | | |
| A3 | | | | | |
| B animated | | | | | |
| B no Wi-Fi | | | | | |
| C reserve | | | | | |

## E. Input, faces and usability — G03, G04, G11

- [ ] Complete 500 mixed touch/BOOT wake trials without failure.
- [ ] Record p95 time to readable display; target at most 500 ms for touch/BOOT.
- [ ] Complete 200 intentional raises; target at least 190 successful, p95 at most 1 second.
- [ ] Log at least one hour each of desk use, walking and normal arm movement; target at most two unintended wakes/hour per sample.
- [ ] Test Raise-only, Twist-only, combined and Off behavior; advertise only qualified modes.
- [ ] Night mode suppresses gesture wake but BOOT/touch still work.
- [ ] Test touch wake guard, long press, aborted drag, rapid gestures and input during iris transitions.
- [ ] All four rotations/calibration paths work; a broken touch map can be repaired with BOOT.
- [ ] All 12 faces remain readable with low battery, Motion Off and the selected text options.
- [ ] Core controls/text fit the circular safe region; existing target/spacing goals remain satisfied.
- [ ] Touch lock has an obvious BOOT escape and does not hide active timer alerts.
- [ ] On-watch time/tool operation works without a companion.

| Test | Trials | Successes / false wakes | p95 readable latency | Settings / evidence |
|---|---|---|---|---|
| Touch/BOOT | 500 | | | |
| Raise | 200 | | | |
| Desk activity | ≥1 h | | | |
| Walking | ≥1 h | | | |
| Arm movement | ≥1 h | | | |

## F. Timers, stopwatch, world time and reminders — G05, G06

- [ ] Existing presets and custom minimum/maximum/invalid durations behave correctly.
- [ ] Timer pause/resume/reset works and is reflected on every screen.
- [ ] Timer expires with the screen asleep on all 12 faces, including Radar/Phantom and Expressive modes.
- [ ] Timer owns the alert screen during wake/sleep animation, Settings, flashlight and radio activity.
- [ ] Dismissal restores the correct screen and brightness; timeout retains a missed notice.
- [ ] Distribute 100 short-timer trials across the above states; none is missed.
- [ ] Alert starts within one second of the device deadline.
- [ ] Against an independent reference, countdown/stopwatch duration error is no greater than the greater of 1 second or 5 seconds per elapsed hour; include a 24-hour duration trial.
- [ ] Countdown and stopwatch are unaffected by UTC steps, drift correction, zone/DST changes and counter rollover.
- [ ] Stopwatch runs with screen off; pause/resume and 10 laps work; maximum duration is defined.
- [ ] Home plus two world zones show the correct local date/DST offline.
- [ ] Three reminders support one-time/daily/selected-weekday configuration, enable/disable, snooze and dismissal.
- [ ] Invalid time suppresses scheduled reminders with a clear explanation.
- [ ] DST spring gap moves to next valid local minute; repeated fall hour fires once per intended occurrence.
- [ ] Forward clock corrections record missed events without an alert burst; backward corrections cannot duplicate one.
- [ ] Timer-plus-reminder, two reminders, and snooze-plus-timer collisions preserve all occurrence IDs; dismiss/snooze affects only the displayed alert and never restarts another alert's timeout.
- [ ] Restart/power-loss behavior shows Interrupted for duration tools when continuity cannot be established.
- [ ] Reminder configuration survives supported updates; missed/reminder occurrence state follows the accepted persistence policy.
- [ ] Timer/reminder UI explicitly identifies alerts as silent.

| Case group | Count | Missed / duplicate events | Maximum deadline lateness | Evidence |
|---|---|---|---|---|
| 1/3/5-minute timers | 100 total | | | |
| Representative 30/60-minute and 24-hour duration trials | | | | |
| UTC/zone/DST/rollover | | | | |
| Reboot/power loss | | | | |
| Reminder recurrence/snooze/missed | | | | |
| Simultaneous timer/reminder/snooze events | | | | |

## G. Persistence and fault recovery — G07, G08

- [ ] Interrupted saves at each logical commit boundary recover a valid old or new configuration.
- [ ] Full-storage/write failures are reported and leave a usable watch.
- [ ] Corrupt records and unsupported newer schemas follow the documented recovery policy.
- [ ] Legacy migration fixtures pass for every supported schema.
- [ ] Tick/redraw/sensor/unchanged-command activity causes no unnecessary flash writes.
- [ ] Three early unexpected restarts enter the chosen safe-mode policy; healthy uptime clears the streak.
- [ ] Safe mode preserves settings and provides a readable screen, BOOT input and USB diagnosis.
- [ ] Stuck/missing IMU or touch, I2C recovery and low-memory rendering fail in a bounded usable way.
- [ ] No Wi-Fi/NTP or malformed traffic can create an endless radio/restart loop.
- [ ] Perform 1,000 sleep/wake cycles and record heap trend/restarts/failures.
- [ ] Recovery/export logs contain useful reasons and no passwords.

Fault-injection result / evidence:

## H. Companion, protocol and privacy — G09, G11, G13

- [ ] Confirmed watch state is separate from editable preview state.
- [ ] No operation reports success before watch acknowledgement.
- [ ] Unsupported capabilities are disabled or explained; Virtual Watch is always labelled.
- [ ] Physical telemetry has real values/units/validity/age; disconnect marks it stale.
- [ ] 100 command transactions pass on each supported transport, including new tool commands.
- [ ] Malformed/overflow/trailing/fragmented/unknown commands fail without unintended state changes.
- [ ] Duplicate timer-start requests cannot restart a timer.
- [ ] 20 unplug/reconnect/switch cycles leave one active session and no stale reply updates.
- [ ] Profile apply reads back every supported promised field.
- [ ] Offline shell/help/preview work after initial cache; supported local connectivity still works.
- [ ] Page updates do not interrupt an active device operation; old firmware compatibility is handled.
- [ ] USB credential setup works; unencrypted BLE rejects password provisioning.
- [ ] Temporary setup AP uses a fresh password shown on the watch/QR, closes after cancel/timeout and has no open credential-entry fallback.
- [ ] Passwords never appear in console/export/profile/browser persistent storage.
- [ ] Forget-network and factory-reset clear the intended device stores.
- [ ] Keyboard, labelled controls, announced errors, 200% zoom and reduced motion work for setup/status.
- [ ] Test all public downloads at the deployed /DriftWatch/ path.

| OS / browser / version | Transport | 100 transactions | 20 session cycles | Offline / access / privacy | Evidence |
|---|---|---|---|---|---|
| | USB | | | | |
| | BLE | | | | |

## I. Wearability and field use — G08, G14

- [ ] Record final battery/case/strap BOM, assembly photographs and revision.
- [ ] Strap and battery are retained; no sharp edges, pinched wires or stressed connector.
- [ ] USB, BOOT and RESET remain accessible.
- [ ] Full-day wear and charging are comfortable and practical.
- [ ] Intended dry-use conditions and any separately tested environmental claims are documented.
- [ ] Feature freeze begins before final wear qualification.
- [ ] Complete 14 consecutive days on the final meaningful candidate.
- [ ] Investigate every reset/hang/missed event; any final-RC firmware change restarts the 14-day stability run and repeats other affected gates.
- [ ] If available, another wearer completes setup/use without developer assistance.

| Day / date | Firmware / uptime | Battery start → end | Clock error / sync age | Wake/tool/reset issues | Comfort / evidence |
|---|---|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |
| 4 | | | | | |
| 5 | | | | | |
| 6 | | | | | |
| 7 | | | | | |
| 8 | | | | | |
| 9 | | | | | |
| 10 | | | | | |
| 11 | | | | | |
| 12 | | | | | |
| 13 | | | | | |
| 14 | | | | | |

## J. Final release sign-off — G01–G14

- [ ] All automated suites/builds/image validation pass for the recorded commit.
- [ ] All required features and accepted gates have evidence.
- [ ] No unresolved P0/P1 fault remains.
- [ ] Exact qualified binaries are promoted; candidate/stable channel metadata does not require rebuilding the embedded firmware version.
- [ ] Release contains SHA-256, board/layout, source SHA, toolchain, known limitations and support matrix.
- [ ] Install, preserving update, reset, downgrade policy and rescue instructions match the tested route.
- [ ] Public runtime/accuracy claims match measured conditions.
- [ ] Silent alerts, inferred battery/charging and power-loss clock behavior are explained.
- [ ] Final release/download/help links work and all displayed versions agree.
- [ ] A known-good fallback and issue-report template remain available.

| Gate | Pass / fail / not run | Evidence link | Exceptions / decision |
|---|---|---|---|
| G01 Runtime | Not run | | |
| G02 Clock | Not run | | |
| G03 Manual wake | Not run | | |
| G04 Gesture wake | Not run | | |
| G05 Tools | Not run | | |
| G06 Clock changes | Not run | | |
| G07 Persistence | Not run | | |
| G08 Stability | Not run | | |
| G09 Device control | Not run | | |
| G10 Setup/recovery | Not run | | |
| G11 Usability | Not run | | |
| G12 Release integrity | Not run | | |
| G13 Privacy | Not run | | |
| G14 Wearability | Not run | | |

Release decision / date / responsible maintainer:
