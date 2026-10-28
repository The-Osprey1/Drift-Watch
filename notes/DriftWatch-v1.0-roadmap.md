# DriftWatch: roadmap to a standalone daily driver

Prepared 26 October 2026. Reviewed source: [f8b87542159acc050f3d205ec157a457769b977f](https://github.com/The-Osprey1/DriftWatch/tree/f8b87542159acc050f3d205ec157a457769b977f).

**Product decision:** v1.0 is a dependable standalone watch on the existing Waveshare ESP32-S3-Touch-LCD-1.28 board. A phone or computer helps with setup and updates; ordinary use works without one.

This document is a proposed implementation and qualification plan. Targets are release requirements to validate, not measurements already achieved. Release numbers and effort ranges are planning estimates. No firmware was changed or flashed during this review.

Implementation began in merged [PR #28](https://github.com/The-Osprey1/DriftWatch/pull/28) with the unreleased [v0.15.3 foundation](NOTES-v0.15.3.md): update packaging, strict command parsing and truthful companion controls. [PR #29](https://github.com/The-Osprey1/DriftWatch/pull/29) adds the [v0.16.0 development candidate](NOTES-v0.16.0-dev.md): clock provenance/monotonic sync age, timer restart notices with checked storage, measured status and capability negotiation, and bounded BLE delivery. Settings-store crash consistency and boot-loop safe mode remain open within v0.16. Hardware acceptance and the measured power/clock gates also remain open. The baseline findings below refer to the original reviewed source snapshot.

## 1. What v1.0 promises

The subsequent [v0.16.0-dev.2 settings candidate](NOTES-v0.16.0-dev.2.md) implements verified complete settings records, legacy migration, staged command saves and visible storage status. Software fault tests and physical power-cut qualification are separate evidence; boot-loop safe mode remains the next foundation task.

You can charge the watch, put it on, read the correct local time, use its tools, and finish a normal day without tending to the firmware. Connectivity failures remain background events. Settings survive supported updates. An unsuccessful update has a documented USB rescue path.

The existing board defines several boundaries:

- The display is a backlit IPS LCD. Choose brightness, screen-on time, and rendering policies accordingly.
- There is no independent battery-backed clock. After complete power loss, show that time needs setting until manual entry or a successful sync establishes it.
- There is no buzzer or vibration motor. Reminders and timer alerts are visual and silent. A missed-alert indicator persists after the alert screen closes.
- Battery level and charging are inferred from voltage. Show estimates and uncertainty honestly.
- The QMI8658 temperature reading is board/sensor temperature, not ambient temperature or a health measurement.
- Wi-Fi and BLE are temporary tools. Persistent phone connections are outside the v1.0 power and support contract.

These boundaries follow the [repository hardware description][S1], [battery implementation][S2], and [timekeeping implementation][S3].

### Included feature set

| Area | v1.0 capability | Scope boundary |
|---|---|---|
| Clock | Local time/date, 12/24-hour format, manual setting, saved time-zone rules, DST, drift correction, last-sync status | Clearly distinguish set, synced, stale, and unset time |
| Faces | All 12 existing faces, favourites, dependable switching, low-power rendering, readable battery/time status | Improve existing faces before expanding the collection |
| Input | Touch, BOOT fallback, rotation/calibration, raise/twist options, night mode | A failed sensor cannot make the watch unusable |
| Countdown | One active custom timer, existing 1/3/5-minute presets, pause/resume/reset, persistent missed notice | Bounded duration up to 24 hours; no multi-timer complexity |
| Stopwatch | Start/pause/resume/reset, up to 10 laps, works with screen off | Defined maximum display duration and honest reset interruption |
| World time | Home plus two saved zones; date difference and DST | Curated offline zone rules with version/date information |
| Reminders | Up to three named one-time/daily/selected-weekday visual reminders, enable/disable, snooze, missed list | Explicitly silent; requires valid local time |
| Quick tools | Existing flashlight with timeout, brightness, quick access to running tools and missed notices | Flashlight restores prior brightness |
| Power | Measured runtime, automatic low-battery policy, night schedule, bounded radio sessions | Daily-use runtime comes before always-on display |
| Companion | USB/BLE setup and supported controls, confirmed state, real telemetry, local profiles, offline help | Virtual Watch is clearly labelled; no account required |
| Maintenance | Settings-preserving USB updates, factory/rescue image, reset/forget-network, diagnostic export | USB remains the baseline recovery route |
| Physical watch | Documented battery, case, strap, assembly, charging and button access | Existing board; no custom PCB dependency |

**Optional after the core passes:** step counting, additional complications, and more faces. Step counting ships only after an accuracy and energy trial; it is not a v1.0 blocker.

**After v1.0:** phone notifications/native apps, always-connected BLE, weather feeds, cloud sync, an app store, GPS/heart-rate features, a custom PCB, added RTC/haptics, and routine OTA updates. These each expand the support, hardware, or power contract.

## 2. Starting position and confirmed gaps

The project already has useful foundations: twelve faces, manual/NTP/USB/BLE time setting, drift correction, low-battery policy, watchdog recovery, I2C startup recovery, settings migration, strict host simulations, mutation checks, image validation, and CI builds. Preserve these investments. [Source overview][S1], [CI][S4].

| Current state | Work required before daily-driver status |
|---|---|
| Source reports v0.15.2; published release/images remain v0.15.1 | Qualify and publish one coherent candidate with matching links and provenance |
| BLE and Serial have separate command paths; several companion buttons send only through Serial/Virtual | Share semantics, negotiate capabilities, acknowledge changes, test both transports |
| Companion computes synthetic temperature/voltage during physical connections | Publish real measurements with validity/age; synthetic data stays in Virtual Watch |
| Numeric parsers use conversions without full-input checks | Reject malformed, trailing, overflow, and out-of-range input without changing state |
| USB companion sync sends time without equivalent zone support | Fulfil the time-and-zone promise through negotiated commands |
| Duration timers use monotonic deadlines but live in RAM | Preserve the timing model; explicitly report interruption after a restart |
| Time validity largely checks whether the epoch is plausible | Track time source, trust, last successful sync, and reset/power-loss state |
| Settings are saved as multiple unchecked writes, with schema version written first | Add write-result handling, migration policy, and crash-consistent commit semantics |
| Watchdog recovery exists; no boot-loop safe mode was found | Add a minimal recovery mode with basic time display, BOOT input, and USB access |
| Normal builds poll motion; wake-on-motion/touch standby are diagnostic defaults | Measure the energy and reliability tradeoff before promoting either |
| Only a merged factory-style image is emitted | Create a tested settings-preserving update and keep a separate rescue image |
| Companion uses external fonts and has no offline app shell | Bundle required assets and cache app/help with an explicit update strategy |
| Several hardware acceptance items remain open in release records | Replace scattered historical status with one current, commit-specific record |

Evidence: [companion controls and telemetry][S5], [Serial parser][S6], [phone protocol][S7], [settings][S8], [power policy][S9], [build script][S10], [hardware checklist][S11].

### Resolve the update issue early

The merged RAW image includes padding over the NVS region. Flashing that whole image erases sectors containing settings and saved Wi-Fi. Espressif documents this padding behavior. The reviewed v0.15.1 image contains erased-value padding across its NVS region. [Build script][S10], [RAW image behavior][S12].

Publish two distinct routes:

1. **Update:** preserve settings and Wi-Fi using an application-only image with a verified compatible layout and target slot.
2. **Factory/rescue:** deliberately install the complete image, with explicit erase/reset consequences.

For the current fixed factory layout, the application starts at 0x10000. An updater must verify layout/slot assumptions; adding OTA later makes blindly writing that address unsafe. Existing dual app partitions alone do not implement rollback. [Image validator][S13], [Espressif OTA design][S14].

## 3. Measurable v1.0 release contract

Freeze these proposed thresholds after the initial baseline and feasibility review. Any revision must be an explicit product decision recorded before beta. Do not quietly relax a failed gate during release qualification.

| Gate | Proposed minimum | Evidence required |
|---|---|---|
| G01 Runtime | At least 24 hours from verified full charge to the 5% warning under Profile A; 36 hours is a stretch target | Three measured runs on the final battery/enclosure and shipping firmware |
| G02 Clock | Within 2 seconds immediately after a qualified sync; no more than 10 seconds absolute error after 24 hours offline following calibration | Independent reference, 72-hour room-to-wrist characterization; report worst error |
| G03 Manual wake | 500/500 touch/BOOT trials succeed; readable display p95 at most 500 ms after the input | Real hardware, including low battery and night mode |
| G04 Gesture wake | At least 190/200 intentional raises succeed; readable display p95 at most 1 second; at most 2 unintended wakes/hour in each defined activity sample | Logged trials for desk use, walking, and ordinary arm movement |
| G05 Tools | No missed alarm in 100 distributed 1/3/5-minute trials; scheduling lateness at most 1 second; duration error at most the greater of 1 second or 5 seconds per elapsed hour against a reference | Screen-off/face/state matrix; custom boundaries/pause/24-hour duration; stopwatch laps/sleep; world-zone dates; reminder recurrence/snooze/reset and simultaneous alerts |
| G06 Clock changes | Countdown/stopwatch elapsed time is unchanged by UTC correction, zone changes, or DST | Automated tests plus selected hardware trials; oscillator accuracy reported separately |
| G07 Persistence | Supported upgrades preserve promised settings; a failed save leaves a valid old or new record; downgrade behavior is documented | Migration fixtures, write-boundary fault injection, physical upgrade/downgrade |
| G08 Stability | No unexplained reset, unrecoverable hang, lost setting, or missed timer in 14 consecutive days on the release candidate | Wear log plus 1,000 bench sleep/wake cycles; restart reasons retained |
| G09 Device control | 100 transactions per supported transport without false success; 20 disconnect/reconnect/switch cycles without stuck sessions | Contract tests and actual USB/BLE clients |
| G10 Setup/recovery | Fresh setup reaches correct local time within 10 minutes; published USB rescue completes within 10 minutes | Someone follows the instructions without a development environment |
| G11 Usability | All critical watch controls fit the safe circle in both text sizes; existing 68 px target/8 px spacing goals retained where applicable; companion setup works with keyboard and 200% zoom | Layout checks and hardware/browser inspection |
| G12 Release integrity | Exact tested commit/binaries, hashes, target/layout, compatibility, known limitations and rescue instructions are published together | Release audit and deployed-link check |
| G13 Privacy | No Wi-Fi password in logs, profiles, diagnostic export, or persistent browser storage; unencrypted BLE and an open setup AP cannot provision passwords | USB and protected temporary-AP setup; BLE rejection; reset/forget-network tests |
| G14 Wearability | Comfortable full-day wear; retained strap/battery; usable charging/BOOT/RESET access; stable charging in the final enclosure | Assembly record and daily wear log |

Percentiles describe the recorded trial set, not a guarantee for every user. A personal v1.0 can qualify on one recorded assembly; support for a broader collection of devices needs additional units and wearers.

### Reference usage profiles

**Profile A — published daily-use baseline:** final shipping defaults, Digital face, the selected normal brightness, Subtle motion, final validated wake setting, and an 8-hour night schedule. Script 100 wakes that each run to the shipping screen timeout (currently 15 seconds), plus five additional screen-on minutes for tools/settings, two countdowns, and four scheduled Wi-Fi sync attempts per 24 hours. No USB power. Start BLE only for the initial setup. Record the actual timeout and screen-on time, temperature range, radio-on time, failed joins and false wakes. Do not use an unstated early screen-off action to improve the result.

**Profile B — demanding use:** repeat with Radar and Phantom, Expressive motion, higher brightness, extra interaction and unavailable Wi-Fi. Publish the measured reduction rather than assigning Profile A's runtime to every mode.

**Profile C — reserve:** low-battery rendering, reduced brightness, radios stopped unless explicitly requested, reliable BOOT/touch access, running timer preserved. Measure remaining useful time; do not infer it from a guessed battery capacity.

Record the battery's model, rated capacity, age, charging completion criterion and exact firmware/settings. Compare energy at the battery terminals with USB disconnected. Diagnostic logging must not confound a feature A/B comparison.

Set the average-current budget from measured usable capacity: allowable average mA = usable mAh / 24 hours. Usable capacity means the measured range from the defined full-charge point to the reserve warning, not the number printed on the cell. Allocate that budget among the board floor, screen/backlight, motion checks and radio sessions.

### Feasibility decisions

- **Clock misses G02:** characterize calibration and temperature behavior, then evaluate supported internal RTC clock-source options and their current/toolchain cost. No external RTC is assumed. Espressif documents temperature-sensitive sleep clocks and internal alternatives. [System time][S15].
- **Runtime misses G01:** measure the board's fixed draw, screen/backlight duty, motion polling and radio sessions. Tune the largest measured contributors. A cosmetic face change cannot substitute for an energy budget.
- **Deep sleep:** consider it only if measured savings justify restart/state-restoration work and clock/tool gates still pass. Light sleep plus validated wake-on-motion is the initial route.
- **Still outside the agreed contract:** keep pre-1.0 status or explicitly revise the product contract. The version number cannot substitute for evidence.

## 4. Release sequence

Work in small vertical slices. Every milestone produces a flashable candidate, a short acceptance record and a known-good fallback.

| Milestone | Outcome | Main work | Exit condition | Initial engineering estimate |
|---|---|---|---|---|
| v0.15.x stabilization | Trustworthy current feature set | Fix false companion state, synthetic telemetry, numeric validation, broken downloads; introduce preserving update; baseline hardware/power/time | Current functions pass a recorded hardware smoke test; no false success; update preserves settings | 3–5 days |
| v0.16.0 foundation | Dependable clock and state | Time quality/reset handling, common command semantics, robust settings commits/migrations, tool interruption semantics, boot-loop safe mode | G02 feasibility established; command and save-failure tests pass; safe mode/rescue demonstrated | 5–8 days |
| v0.17.0 power and input | Predictable daily operation | Battery calibration and profiles, wake-on-motion trial, bounded radio use, low-battery policy, wake tuning, input lock option | G01/G03/G04 pass on candidate defaults; Profile B tradeoffs documented | 5–8 days |
| v0.18.0 offline tools | Complete standalone utility | Custom countdown/pause, stopwatch/laps, home plus two world zones, three silent reminders, missed notices | Tool/time-change/reset matrix passes; all features operable on watch | 5–8 days |
| v0.19.0 companion and privacy | Easy setup without dependence | Offline shell/help, explicit capability support, secure credential route, real status, local profile import/export, accessibility | G09/G10/G11/G13 pass on selected clients; no account or cloud required | 4–7 days |
| v0.20.0 wearable/release readiness | An installable, maintainable watch | Final case/strap/battery assembly, release manifest, preserving installer, recovery guide, supported matrix, dependency review | G07/G12/G14 pass; complete acceptance checklist ready | 3–5 days |
| 1.0.0-beta.1 onward | Measured daily wear | Feature freeze, bench soak, initial wear runs, issue triage, fixes and fresh qualification | Feature-complete candidate reaches all functional gates and is ready for final qualification | 3–5 workdays plus initial observation |
| 1.0.0-rc.1 onward | Frozen release candidate | Full release rehearsal, documentation/link audit, final binaries/hashes and 14-day wear qualification | Exact final candidate passes all gates for 14 consecutive days; no outstanding blockers | 1–2 workdays plus 14 elapsed days |
| 1.0.0 | Supported standalone release | Promote the exact qualified artifacts; publish evidence and limitations | Signed-off release record and functioning install/recovery paths | 1 day |

These ranges total roughly 30–49 focused eight-hour engineering days before contingency. Reserve another 25% for hardware surprises and integration: approximately 300–490 hours. At 10 hours/week, this is roughly 7–12 months of engineering, with observation/fabrication partly overlapping; dedicated time can compress it substantially. Re-estimate after stabilization using actual effort. These are capacity estimates, not dated commitments.

Version ordering deliberately moves from 0.20.0 to 1.0.0-beta/rc. Using 0.9 after 0.20 would move backwards under semantic version ordering.

### Critical dependencies

Preserving update + known-good rescue precede frequent test flashing. Clock quality + persistent state precede reminders and tool restoration. Measured power + validated wake policy precede freezing shipping defaults. Common device commands precede companion expansion. The final enclosure precedes the qualifying wear test. Feature freeze precedes beta/RC qualification.

Case fit and test-harness preparation can progress alongside firmware. Avoid a whole-firmware rewrite or a new UI framework; extract the smallest reusable services needed by each milestone.

## 5. Implementation backlog

Priority meanings: **P0** protects correctness/data/recovery; **P1** completes the daily-driver contract; **P2** is optional and cannot delay a passing core release.

| ID | Priority / milestone | Deliverable and acceptance |
|---|---|---|
| REL-01 | P0 / stabilization | Emit separate factory/rescue and preserving app artifacts; manifest specifies board, offsets/layout and compatibility; upgrade preserves a seeded configuration |
| APP-01 | P0 / stabilization | Separate draft preview from confirmed device state; unsupported controls disabled; each change ends in acknowledged success or visible failure |
| APP-02 | P0 / stabilization | Real telemetry/validity/age; no sine-wave temperature or computed voltage labelled as measurements during physical connections |
| PRO-01 | P0 / stabilization | Full numeric parsing with length/range/overflow checks; invalid input leaves state unchanged; legacy valid commands continue working |
| APP-03 | P0 / stabilization | Redact secrets before logging; remove invalid exports/downloads; physical and Virtual sessions cannot silently overlap |
| QA-01 | P0 / stabilization | One hardware acceptance record tied to source SHA, binary hash, board and battery; establish runtime and clock baselines |
| CLK-01 | P0 / 0.16 | Explicit unset/manual/synced/stale states, source and sync age; power-loss behavior, supported date range and boundary checks tested |
| CLK-02 | P0 / 0.16 | Outlier-resistant drift learning; no learning from manual edits or unreasonable jumps; characterize room/wrist temperature changes |
| CLK-03 | P1 / 0.16 | Equivalent time-and-zone setting on USB/BLE; manual named zone works without third-party IP lookup; retain safe cached rules |
| STO-01 | P0 / 0.16 | Check storage operation results; validated versioned record with a commit marker or equivalent old/new recovery; report save failures |
| STO-02 | P0 / 0.16 | Real migrations by schema version, future-version protection, explicit downgrade policy, unchanged-value write suppression |
| SYS-01 | P0 / 0.16 | Bounded boot-loop detection; minimal face/recovery mode without animations or optional sensors; USB rescue and BOOT remain usable |
| PRO-02 | P1 / 0.16 | Protocol version/capability response, request IDs, bounded framing, explicit errors, duplicate-request policy and compatibility fixtures |
| TOOL-01 | P0 / 0.16 | Preserve monotonic duration timing; rebooted active tools show Interrupted unless reliable elapsed reconstruction is proven; no invented continuation |
| PWR-01 | P1 / 0.17 | Measure fixed board/awake/sleep/radio draw; freeze Profiles A–C and selected shipping defaults from results |
| PWR-02 | P1 / 0.17 | Trial wake-on-motion/touch standby with matched nonlogging builds; retain polling for twist or any unreliable condition |
| PWR-03 | P1 / 0.17 | Reserve policy: rendering/brightness/radio limits, clear estimated battery status, tested charging/unplug transitions and hysteresis |
| INP-01 | P1 / 0.17 | Tune raise/wrist-down using wrist trials; explicit night mode and optional touch lock; BOOT always provides a documented escape |
| UI-01 | P1 / 0.17 | All faces, four rotations, large text, low battery and Motion Off remain readable; running tools/missed notices accessible from any face |
| TOOL-02 | P1 / 0.18 | Custom countdown up to 24 hours; pause/resume/reset and presets; timer indication survives navigation and sleep |
| TOOL-03 | P1 / 0.18 | Stopwatch and 10 laps; elapsed clock independent of UTC; defined maximum duration and reset interruption |
| TOOL-04 | P1 / 0.18 | Home plus two world zones, local dates/DST and cached rules; changing a world zone never changes the base UTC clock |
| TOOL-05 | P1 / 0.18 | Three visual reminders, recurrence, snooze, retained missed notice; DST, clock-jump, invalid-time, restart and simultaneous-alert policies tested |
| APP-04 | P1 / 0.19 | One queued protocol client with Serial/BLE/Virtual adapters; stale replies from a previous connection cannot change new-session state |
| APP-05 | P1 / 0.19 | Offline app/help/assets after first load; app updates never replace the shell halfway through a device operation |
| APP-06 | P1 / 0.19 | Local profile export/import with schema checks and preview; credentials excluded; unsupported settings clearly reported |
| SEC-01 | P1 / 0.19 | USB credential provisioning supported; setup AP uses a fresh session password shown on watch/QR and a short window; reject unencrypted BLE passwords; authenticated BLE optional |
| SEC-02 | P1 / 0.19 | Forget network/factory reset clear the correct Wi-Fi and app stores; exports contain no credentials or unwanted identifiers |
| UX-01 | P1 / 0.19 | On-watch first-use setup, offline/manual route, companion keyboard/zoom/reduced-motion support and clear errors |
| REL-02 | P1 / 0.20 | Guided preserving USB installer or equivalent simple workflow; verifies target/layout/version, offers restore and blocks mismatched images |
| REL-03 | P1 / 0.20 | Publish exact CI-qualified binaries, hashes, manifest, changes, limitations, fallback, dependency/license inventory and link checks |
| REL-04 | P1 / 0.20 | Pin remaining release dependencies; assess core/library support before beta; migrate toolchain only with before/after regression evidence |
| HW-01 | P1 / 0.20 | Final battery/case/strap BOM, drawings/assembly photos, retention, charging/button access and comfort record |
| QA-02 | P1 / beta | Hardware-in-loop smoke harness for serial, sleep/wake, clock and timers; bounded diagnostic export supports reproducible bug reports |
| QA-03 | P1 / beta–RC | Final 14-day wear run, regression matrix, fresh-install/update/recovery rehearsal; unresolved P0/P1 faults prevent release |
| FIT-01 | P2 / after gates | Step-count trial using existing IMU; decide against measured accuracy, false steps and runtime cost |
| FACE-01 | P2 / after gates | Extra faces/complications only within frozen rendering, readability and battery budgets |

For each implemented item, record the responsible person, linked change, tests, candidate version and evidence. One maintainer can own all items; a separate checker is useful for install instructions and final release review.

## 6. Technical design rules

### Small services with clear ownership

- **Time:** owns UTC, local zone rules, quality/source/age and drift learning.
- **Tools:** owns monotonic countdown/stopwatch state and wall-clock reminder scheduling.
- **Storage:** owns validated records, migration, batching and reset.
- **Device commands:** validate, authorize, dispatch and report results through the same behavior on USB and BLE.
- **Power:** owns sleep deadlines and energy policy; tools, sync and reminders supply deadlines.
- **UI:** presents state and sends intents; an animation never owns alarm timing or blocks an input.
- **Companion:** UI to confirmed device model to queued client to one transport adapter. Draft preview remains separate.

Reuse existing modules first. This is an incremental separation of responsibilities, not a requirement to replace the current implementation.

### Time and tool semantics

1. UTC is the base clock; local zones are views. Zone/DST changes cannot alter elapsed tools.
2. Save calibration and zone rules with their provenance/version. Display sync age and an honest stale/unset state.
3. Preserve valid system time across reset types where the platform supports it and tests prove it. Complete power loss always invalidates time without independent timekeeping.
4. An NVS saved timestamp seeds the manual editor; it cannot account for time spent without power.
5. Timer/stopwatch active state is checkpointed on meaningful transitions, with bounded write frequency. After an untrusted restart, show Interrupted and retain the last known state.
6. If time is unset, prevent scheduled reminders and explain how to set time. Countdown/stopwatch remain available.
7. For DST spring gaps, move a scheduled reminder to the next valid local minute. During a repeated fall hour, fire once per intended local-date occurrence.
8. A forward clock correction records a missed reminder instead of producing a burst of old alerts. A backward correction cannot duplicate an already delivered occurrence.
9. Explicit dismissal acknowledges the reminder. Alert timeout leaves an unacknowledged missed record. Snooze creates one new elapsed-duration occurrence and prevents duplicate delivery.
10. Document precision separately: deadline scheduling, displayed resolution, and real elapsed-time accuracy are different measurements.
11. Simultaneous timer/reminder/snooze events retain distinct occurrence IDs in a bounded pending queue. Deliver in deadline order, with timer priority for ties. Dismiss/snooze affects only the displayed occurrence; all others remain pending. A newly due event cannot restart the current alert's timeout or erase a missed record. Test timer-plus-reminder, two reminders, and snooze-plus-timer collisions.

### Persistence and reset

Use a compact validated record with schema version, generation and integrity check, plus a commit/recovery scheme that retains an older valid record. Choose the simplest scheme supported by the actual NVS guarantees; prove old/new recovery with injected interruptions.

Keep fast-changing runtime state separate from preferences. Save only changed values or bounded checkpoints. Do not continuously write timer ticks, telemetry or sensor samples to flash. Migrate only after reading/validating the previous record. A newer unsupported schema must not be silently rewritten by older firmware.

Provide distinct actions: restart, forget Wi-Fi, reset preferences, and full factory reset. Explain what each removes. Keep recovery accessible without working touch.

### Protocol and privacy

Use one command contract across transports while preserving documented legacy aliases. Include protocol/firmware versions, capabilities, request IDs, acknowledgements, structured errors, length limits and units. Status covers time validity/zone/source/age, face/settings, measured battery mV/estimated percent, board temperature when available, power state and tool state.

Retry reads and idempotent settings safely. A retried timer-start request must not restart the countdown; deduplicate by request ID/session. Serial/BLE fragments and notifications require bounded reassembly. Disconnect cancels pending work and invalidates old replies.

Keep BLE explicitly user-opened and time-limited. Make USB the default path for Wi-Fi passwords. Reject passwords on an unencrypted BLE link; authenticated encrypted provisioning is an optional implementation route only after client compatibility tests pass. Never retain credentials in browser profiles or logs.

Retain on-watch Wi-Fi setup through a protected temporary AP with a fresh per-session password shown locally and encoded in its QR, a bounded five-minute session, and explicit on-watch initiation. Do not leave the current open AP as a parallel credential route. Test client isolation/connection limits where the stack supports them, timeout, cancel and teardown. If that protected route is unfinished, disable AP credential entry in the stable build and offer USB instead. The current portal starts without an AP password. [Network implementation][S16].

Use browser/manual zone rules without requiring IP geolocation. If an IP-based lookup remains, make its network dependency visible and optional. The current firmware uses a third-party HTTP lookup; retain cached/manual operation when it fails. [Network implementation][S16].

### Updates and toolchain

Keep the v1.0 partition layout fixed where feasible. Verify board, layout, image size, version and checksum before a preserving USB write. Hashes detect corruption; they do not provide publisher authentication by themselves. Fetch release manifests/images from the documented HTTPS release source.

Define downgrade compatibility and use a backup/export before an intentionally incompatible reset. Keep a known-good rescue image downloadable and exercise BOOT+RESET recovery in the final enclosure. Waveshare documents this recovery sequence. [Board FAQ][S17].

Build the final firmware version once before final RC qualification. The release manifest can mark that exact binary as a candidate, then stable after qualification; channel metadata is separate from the firmware's embedded version. Promote the same binary and hash rather than rebuilding solely to change an RC suffix.

OTA is optional after v1.0. If added, require authenticated image delivery/verification, inactive-slot writes, boot self-test, rollback, interruption tests and a key-management plan. Do not assume partition presence supplies these mechanisms. [OTA][S14].

Inventory Arduino core, libraries, Python packages and CI actions. Existing version pinning is useful. Evaluate support/security and any necessary upgrade early; freeze the qualified toolchain before beta. Release the artifacts you actually tested rather than rebuilding a different image after approval.

## 7. Test strategy and evidence

| Layer | Coverage | When |
|---|---|---|
| Logic/host | Parsing, storage migration/failure, time quality, drift, zones/DST, tool transitions, rollover, reminders, state ownership | Every relevant change |
| Existing native simulations | Faces/layout, input/UI, real network state machine, power controller and mutation checks | Preserve existing CI gates |
| Browser/protocol | Mock USB/BLE, capabilities, errors/retries, acknowledgements, fragmentation, disconnect, transport switching, profiles, offline shell | CI |
| Firmware build | Normal/diagnostic images, warnings, target/layout/size/version, prohibited test strings | CI |
| Hardware smoke | Boot, calibration/rotation, USB asleep sync, BLE, Wi-Fi loss/retry, tools on animated faces, low-battery behavior | Every milestone candidate |
| Energy/clock | Battery-terminal current, runtime profiles, clock reference and observed temperature | Baseline, power changes, final assembly |
| Fault/recovery | Save interruption, reset while tool active, stuck I2C, sensor missing, interrupted update, mismatched image, boot loop | Before beta and after affected changes |
| Wear | Ordinary desk/walking/commuting use, night use, comfort, false wakes, runtime, clock, missed notices | Beta and RC |
| Release rehearsal | Fresh install, preserving update, downgrade policy, rescue, public links and manifest | RC |

Do not repeat every long experiment after a documentation-only correction. Repeat affected checks after changes. Any firmware change during final RC qualification creates a new binary and restarts the 14-day stability gate; repeat other affected gates as well. Earlier beta runs provide useful diagnosis but do not replace the final unchanged-binary run.

Use the companion [acceptance checklist](DriftWatch-v1.0-acceptance-checklist.md) as the executable test record.

### Supported clients

Start with one deliberately small, recorded matrix: Windows Chrome/Edge over USB and BLE, plus one actual Android Chrome BLE client if available. Add iPhone/Bluefy only after testing that exact route. Unsupported browsers should show a clear alternative rather than a broken Connect button.

An offline app shell cannot add a missing browser API. Avoid promising universal browser or phone support from a successful desktop demo. Basic watch use remains independent of the browser matrix.

### Beta process

Start with your own final assembly. If available, add one or two other wearers/devices to uncover fit, gesture and browser differences. Log enough evidence to fix an issue without collecting private activity histories.

Each daily record includes firmware hash, uptime/restarts, battery start/end, actual screen/radio time, sync source/age, reference clock error, missed/false wakes, timer/reminder results and comfort notes. Record anomalies when they occur.

P0 examples: wrong time presented as trusted, erased settings during a preserving update, boot loop, unrecoverable update, exposed credentials, missed active timer. P1 examples: repeatable wake failure, unusable supported setup route, runtime below contract, inaccessible core control. Fix these before release. Cosmetic defects with usable workarounds can be documented.

## 8. Physical readiness on the current board

Record the exact battery model/capacity/protection, connector/polarity, enclosure revision, strap, fasteners and assembly instructions. The board supports a 3.7 V battery through its stated connector; check the actual chosen pack and enclosure against the manufacturer's specifications. [Board FAQ][S17].

Verify battery retention and space, wire routing, exposed contacts, screen attachment, strap retention, USB access and strain, and BOOT/RESET access. Characterize charging and board temperatures in the final case under ordinary use and charging, against the component/pack ratings.

A development-board case has no automatic water-resistance rating. Define the intended dry-use conditions and test any broader claim separately. Do not add a commercial certification program to a personal v1.0; expand the qualification plan if distribution or sales become the goal.

## 9. First implementation batch

1. Record the candidate SHA and known-good image; open the acceptance record.
2. Implement REL-01: preserving update plus labelled factory/rescue artifacts.
3. Fix APP-01/APP-02/PRO-01/APP-03: truthful state/telemetry, malformed input, logs, links and connection ownership.
4. Add focused command/browser regressions to existing CI.
5. Flash through the preserving route and finish the outstanding current-feature hardware checks.
6. Run the first 24-hour battery profile and 72-hour clock study while starting storage/time-quality work.
7. Lock the quality thresholds, shipping assumptions and revised effort estimate from those results.

Do not begin another face pack before this batch is complete. Keep the first batch independently useful: a more trustworthy companion, safer updates, known hardware status and measured feasibility.

## 10. v1.0 sign-off

- Every required feature and G01–G14 gate has a linked result, hardware identity and firmware hash.
- No open P0/P1 issue remains in the qualified supported scope.
- Installation, settings-preserving update, reset and USB rescue have been rehearsed.
- Runtime and clock claims state the actual measured conditions.
- Silent reminders, inferred charging, complete-power-loss behavior and supported clients are stated plainly.
- The shipped binaries are the qualified artifacts from the recorded source commit.
- The final release includes source, images, SHA-256, manifest, install/update/recovery instructions, assembly information, known limitations and acceptance results.

After release, use 1.0.x for focused fixes. Preserve the known-good image, protocol compatibility tests and issue template. Establish one stable channel; add an experimental channel only when there is a real need.

## Source references

Links in this document point to the pinned repository snapshot, the existing hardware checklist, Espressif's flashing/time/OTA documentation, and Waveshare's board FAQ. Repository findings are tied to the reviewed commit; external documentation should be rechecked when implementation begins.

[S1]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/README.md
[S2]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/battery.h
[S3]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/timekeep.h
[S4]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/.github/workflows/ci.yml
[S5]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/companion/index.html
[S6]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/serial_cmd.cpp
[S7]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/phone.cpp
[S8]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/settings.cpp
[S9]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/power.h
[S10]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/build.sh
[S11]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/notes/NOTES-v0.15.1.md
[S12]: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#raw-output-format
[S13]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/tools/validate_image.py
[S14]: https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/ota.html
[S15]: https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/system_time.html
[S16]: https://github.com/The-Osprey1/DriftWatch/blob/f8b87542159acc050f3d205ec157a457769b977f/DriftWatch/net.cpp
[S17]: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/FAQ
