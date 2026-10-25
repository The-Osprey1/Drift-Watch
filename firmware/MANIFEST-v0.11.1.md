# Drift-Watch v0.11.1 release manifest

| | |
|---|---|
| Source commit | `3cb68e7c1ef05b2ff7ee98300b3baf85b2d43acb` (on `main` `7df5fd2`) |
| Built | 2026-10-24, from a clean checkout of that commit, with `build.sh` |
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
| `drift_watch_v0.11.1.bin` | screen settings from `build.sh`; `DRIFT_WOM=0`, `DRIFT_TOUCH_STANDBY=0`, `DRIFT_LIGHT_SLEEP=1` (defaults) | 1786432 | `ddfb5ef2d50f915e3f433b09796f786c3cfb9090c511518c640f1616ec988a3e` |
| `drift_watch_v0.11.1_diag.bin` | the same plus `-DDRIFT_DIAG=1`, which also turns on `DRIFT_WOM` and `DRIFT_TOUCH_STANDBY` | 1787088 | `06d799b9b9de8e3a264d26651fcb6cf65f1538af45afe3b91d00361b1f0c3f0c` |

App size: 1,720,493 bytes (normal) and 1,721,145 bytes (diagnostic) of the 3,145,728-byte app partition (55%). Static RAM: 73,368 and 73,440 bytes of 327,680.

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
| `render_faces` | 0 problems |
| `simulate` (interface, network scripted) | 404 of 404 |
| `simulate_network` (interface with the real network code) | 39 of 39 |
| `net_test` (network code) | 223 of 223 |
| `power_test` (sensor registers, screen-off controller) | 83 of 83 |
| Host build | g++ -std=c++17 -Wall -Wextra -Werror with address and undefined-behaviour sanitizers (`HOST_STRICT=1`): no warnings, no sanitizer reports |
| `mutation_check.sh` | 10 of 10 planted bugs caught |
| Firmware warnings, our code (`DRIFT_WARNINGS=all`) | 0 in both images |
| Firmware warnings, core and libraries | 0 in this local build; the clean CI build records its own count in its warning report |
| `tools/validate_image.py 0.11.1` | both images pass (layout, partition checksum, size, version, no old product name or test passwords) |

## Acceptance

- Software: passed, as above.
- Hardware: verified boot, PSRAM frame buffers, CST816S touch (`0xB5` firmware `0x02`), QMI8658 IMU (`0x6B` revision `0x7C`) and serial clock sync on Waveshare ESP32-S3-Touch-LCD-1.28.
- Power: **pending** (not measured).
