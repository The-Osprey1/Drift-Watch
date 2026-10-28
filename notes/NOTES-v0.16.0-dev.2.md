# DriftWatch v0.16.0-dev.2 — verified settings

Unreleased development candidate. This slice adds settings persistence to the clock/timer/status foundation; it does not implement boot-loop safe mode or complete v0.16/v1.0 qualification. No physical watch was flashed.

## Storage contract

Schema 7 uses complete records in watchcfg/a and watchcfg/b. Each 215-byte record has an explicit little-endian envelope, generation, bounded strings/fields and CRC32. The persisted representation contains no String pointers or compiler padding. CRC detects accidental corruption, not tampering.

On a changed save, the invalid/older slot is the target; the newest valid snapshot stays available. Success requires the expected write count, a fresh read after reopening storage, byte equality, valid decoding and the expected generation. An unchanged verified payload needs no flash write. First use/import creates one record; later changed saves establish the alternating history.

A failed or unconfirmed save reports failure. If bytes reached storage before verification failed, a restart may recover the complete old or complete new snapshot; the command never claims success for that uncertain result. Local settings can remain usable in RAM, visibly unsaved, with retries no sooner than 30 seconds or after a new explicit edit. Remote commands stage their candidate and install it in RAM only after verification.

Loading selects the newest valid supported snapshot. A CRC-corrupt peer produces a visible recovered status. Integrity-valid future schemas/envelopes, oversized records, conflicting equal generations and generation exhaustion block writes. If no supported record is usable, an intact legacy snapshot may be shown read-only with a warning; it can be older than the last new-format settings. Without a legacy snapshot, safe RAM defaults are read-only. The store is never automatically erased.

The error-aware existence probe distinguishes a missing key from a read failure. Preferences reads and writes are checked; the pinned [Preferences implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/Preferences/src/Preferences.cpp) commits its blob write before returning the byte count. Physical power-cut behavior still requires qualification against the actual board and [NVS behavior](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/storage/nvs_flash.html).

## Migration and downgrade boundary

Legacy schemas 1–6 are loaded with their historical defaults, normalized, and imported through one verified record. Original watch/* keys are retained without mirroring future changes. Existing timer markers and Wi-Fi credentials use separate stores. lastKnownTime remains an editor hint and never restores the live clock.

Older firmware can read the legacy snapshot left at upgrade; it does not receive the latest schema-7 changes. A new-format watch with no old keys uses the old firmware's defaults. Edits made by old firmware do not override an existing valid schema-7 record on re-upgrade. Latest-settings downgrade synchronization is outside this candidate's contract. Factory/rescue flashing resets both settings formats.

## Device and companion behavior

INFO? appends STORE=saved/pending/recovered/error/readonly. BLE uses the equivalent lower-case field, delivered whole before the acknowledgement. Capability bit 32 means checked settings persistence: Serial advertises CAPS=0x3F and BLE CAPS=0x3D. Schema remains PROTO=1.

For that capability, the matching companion requires fresh STORE=saved plus fresh matching setting fields before confirming settings, face saves or profiles. Earlier firmware keeps the existing confirmation contract. A save refusal is visible without a false success. Read-only settings disable persistent controls; Refresh, supported USB timers and USB UTC sync remain usable. BLE time-and-zone sync is blocked while settings are read-only. USB sync leaves the selected zone unchanged.

The face and Details report save uncertainty, read-only protection or recovery. A failed setup-portal zone save gets an explicit Zone not saved result; Wi-Fi credentials are a separate store. Neither a runtime RAM value nor an earlier cached STORE=saved can certify a new save.

## Qualification

Tests cover codec bit corruption/truncation, every target-record interrupted-write boundary, migration fixtures, read/open/write/readback faults, future protection, generation conflicts/exhaustion, no redundant writes, staged command refusals, bounded local retries and companion durability/freshness. Run the full strict host/mutation suites, pinned normal/diagnostic firmware builds, image validation and browser checks for the exact source revision.

Physical acceptance remains NOT RUN: preserving upgrade, controlled power cuts during repeated settings saves, restart/recovery notices, the documented downgrade/re-upgrade boundary, USB/BLE confirmed controls and power behavior during save failures. Keep a known-good rescue package. Passing host tests does not qualify physical flash behavior, battery runtime or daily wear.
