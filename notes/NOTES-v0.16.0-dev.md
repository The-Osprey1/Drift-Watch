# DriftWatch v0.16.0-dev — clock and timer reliability

Unreleased development candidate, based on the v0.15.3 foundation. Hardware acceptance, runtime and accuracy measurements are still pending. This is a scoped start on the v0.16 roadmap, with settings-store crash consistency and boot-loop safe mode still to follow.

## Clock provenance

- Reports unset, manual, retained, synced or stale time, with the current source and sync age.
- A plausible system clock at startup is labelled retained with unknown source/age. A saved editor timestamp is never restored as current time. Complete power loss still requires setting or syncing.
- Trusted Wi-Fi/phone/USB updates establish a new age. Age uses accumulated monotonic uptime, survives millis rollover, and cannot become fresh because UTC moved backwards.
- A trusted clock becomes stale after 24 hours without a new sync. It remains usable; this label is not a measured accuracy guarantee.
- USB time is a distinct trusted source. Its whole-second timestamp retains the learned drift correction but resets the learning anchor.
- Manual/phone/USB setting cancels an older active sync before applying the new choice. A late lookup or NTP reply cannot overwrite it.
- An implausible NTP answer is a failure; an already usable clock is restored to its progressing prior value.

## Timer recovery

The separate tools/timer NVS key records Idle, Running or Due only on transitions, with checked writes. It does not continuously checkpoint ticks or store a fabricated restart deadline.

- Restart with Running: show Interrupted and wait for explicit acknowledgement/reset.
- Restart with Due: retain a missed-timer notice.
- Alarm timeout: keep the missed notice on the face and in Quick Drawer.
- Explicit tap/BOOT dismissal or reset: clear the notice only when persistence succeeds.
- A failed marker save cannot produce a successful timer-start/reset reply. The drawer and status report a storage error.
- Starting a new timer explicitly replaces the pending notice. Normal light sleep keeps the monotonic deadline; a restart never pretends that continuity was established.

Factory/rescue flashing erases this marker along with other NVS data. An application-only downgrade to older firmware does not add recovery support to that older version.

## Device status contract

INFO? keeps the existing Serial fields and appends:

| Field | Meaning |
|---|---|
| PROTO=1 | Status/capability schema version; this does not add request IDs |
| CAPS | Hex bitmask: 1=settings, 2=remote timer control, 4=telemetry, 8=time quality, 16=timer recovery |
| MV | Measured battery millivolts; -1 unavailable |
| TEMP | QMI8658 board/sensor degrees Celsius; na unavailable |
| DRIFT | Learned correction in parts per billion |
| TIMEQ / SOURCE | Current clock quality and source |
| SYNCAGE | Monotonic seconds since the current trusted sync; -1 unknown |
| TIMER | idle/running/alarming/interrupted/missed/storage_error |
| REMAIN | Sampled whole seconds remaining, rounded up |

Serial advertises CAPS=0x1F. BLE advertises CAPS=0x1D: it reports timer state but does not accept remote timer commands. BLE uses the equivalent lower-case semicolon fields. Optional zone/network text escapes wire delimiters and is omitted whole if it cannot fit.

The companion displays sampled timer state rather than inventing a countdown between readings. Measurement age is tracked per field. Its explicit Refresh button sends one query; no periodic polling extends Phone Sync in the background. USB sync confirms UTC only and states that the watch's existing time zone is unchanged.

## BLE delivery

Full status is readable as a bounded snapshot of at most 512 text bytes. Notifications emit complete known fields of at most 20 bytes, then the acknowledgement last. Long optional strings remain available through the readable snapshot. A raw notification is sent only if the entire raw value fits.

The main-loop queue holds at most eight snapshots and sends at most one notification per 20 ms without sleeping. Close/link changes clear pending snapshots. Oversize/overflow replies are dropped without a false acknowledgement. Existing MTU negotiation and command write fragments remain supported.

## Qualification

Local packaging and companion regressions pass; changed production and pure-test objects cross-compile under GNU C++11 with warnings treated as errors. Full pinned-core firmware builds, strict host execution, mutation/layout checks and real BLE behavior must be checked on the candidate's CI/hardware record.

The first physical tests are restart while running, restart after expiry, timeout/missed notice acknowledgement, marker-save failure handling where practical, USB-versus-NTP precedence, status refresh and BLE small-payload delivery. Battery and clock accuracy claims still require the roadmap's measured profiles.
