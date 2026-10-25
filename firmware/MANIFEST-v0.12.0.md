# Drift-Watch v0.12.0 release manifest

| | |
|---|---|
| Source commit | `f0d46da3175dad7ee7024ee61b4fca4519147751` (branch `v0.12.0-motion-faces`, on `main` `c385476`) |
| Built | 2026-10-25, from that source, with `build.sh` (`DRIFT_WARNINGS=all`) |
| arduino-cli | 1.1.1 |
| ESP32 Arduino core | esp32:esp32 2.0.17 (ESP-IDF 4.4) |
| Compiler | xtensa-esp32s3-elf-gcc (crosstool-NG esp-2021r2-patch5) 8.4.0 |
| esptool | v4.5.1 (from the core's tools) |
| Libraries | TFT_eSPI 2.5.43, WiFiManager 2.0.17 (tzapu) |
| Board | `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc` |
| Flash | DIO, 80 MHz, 16 MB; one merged image per build, flashed at 0x0 |

## Images

| File | Flags | Bytes | SHA-256 |
|---|---|---|---|
| `drift_watch_v0.12.0.bin` | screen settings from `build.sh`; `DRIFT_WOM=0`, `DRIFT_TOUCH_STANDBY=0`, `DRIFT_LIGHT_SLEEP=1` (defaults) | 1797440 | `9053cec6c65a44e9158c2218703f38f40c4df1f31c3673c3668ab48dd854f49a` |
| `drift_watch_v0.12.0_diag.bin` | the same plus `-DDRIFT_DIAG=1`, which also turns on `DRIFT_WOM` and `DRIFT_TOUCH_STANDBY`, and logs entrance frame times | 1798256 | `ca4740af53764859663b94fa3435712006a34a25f98e446f6da9aaae773d73cc` |

App size: 1,731,489 bytes (normal) and 1,732,313 bytes (diagnostic) of the 3,145,728-byte app partition (55%). Static RAM: 73,632 and 73,712 bytes of 327,680.

## Merged image layout

| Offset | Part |
|---|---|
| 0x0 | bootloader |
| 0x8000 | partition table |
| 0xE000 | boot_app0 (OTA data) |
| 0x10000 | app |

Partition table in both images:

| Name | Type | Subtype | Offset | Size |
|---|---|---|---|---|
| nvs | data | 0x02 | 0x009000 | 0x005000 (20 KB) |
| otadata | data | 0x00 | 0x00E000 | 0x002000 (8 KB) |
| app0 | app | 0x10 | 0x010000 | 0x300000 (3072 KB) |
| app1 | app | 0x11 | 0x310000 | 0x300000 (3072 KB) |
| ffat | data | 0x81 | 0x610000 | 0x9E0000 (10112 KB) |
| coredump | data | 0x03 | 0xFF0000 | 0x010000 (64 KB) |

## Checks at this commit

| Check | Result |
|---|---|
| `render_faces` (now with entrance checks) | 0 problems |
| `simulate` (interface, network scripted) | 456 of 456 |
| `simulate_network` (interface with the real network code) | 39 of 39 |
| `net_test` (network code) | 223 of 223 |
| `power_test` (sensor registers, screen-off controller) | 83 of 83 |
| Host build | g++ -std=c++17 -Wall -Wextra -Werror with address and undefined-behaviour sanitizers (`HOST_STRICT=1`): no warnings, no sanitizer reports |
| `mutation_check.sh` | 13 of 13 planted bugs caught |
| Firmware warnings, our code (`DRIFT_WARNINGS=all`) | 0 in both images |
| Firmware warnings, core and libraries | 0 in this local build; the clean CI build records its own count in its warning report |
| `tools/validate_image.py 0.12.0` | both images pass (layout, partition checksum, size, version, no old product name or test passwords) |

## Acceptance

- Software: passed, as above.
- Hardware: **pending**. v0.12.0 has not run on the watch. v0.11.1 booted on it (PSRAM frame buffers, touch and motion sensor found).
- Entrance smoothness: **pending** (flash the diagnostic image and read the `diag: entrance and marker frames` line).
- Power: **pending** (not measured).
