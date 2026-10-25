# Drift-Watch v0.14.0 release manifest

| | |
|---|---|
| Source commit | `68b3a5c5b395c0c69cc2b7fd93d8d4fdf0e1f8cb` (`main`, PR #12) |
| Built | 2026-10-25, in GitHub Actions CI run `40169870647` with `build.sh` (`DRIFT_WARNINGS=all`) |
| arduino-cli | 1.1.1 |
| ESP32 Arduino core | esp32:esp32 2.0.17 (ESP-IDF 4.4) |
| Compiler | xtensa-esp32s3-elf-gcc (crosstool-NG esp-2021r2-patch5) 8.4.0 |
| Libraries | TFT_eSPI 2.5.43, WiFiManager 2.0.17 (tzapu) |
| Board | `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc` |
| Flash | DIO, 80 MHz, 16 MB; one merged image per build, flashed at 0x0 |

## Images

| File | Flags | Bytes | SHA-256 |
|---|---|---|---|
| `drift_watch_v0.14.0.bin` | screen settings from `build.sh`; `DRIFT_WOM=0`, `DRIFT_TOUCH_STANDBY=0`, `DRIFT_LIGHT_SLEEP=1` (defaults) | 1823200 | `1d5c046828091501b4e7850f42b346cf3c2cc6406b0b7a9f569c6d54e57a0016` |
| `drift_watch_v0.14.0_diag.bin` | the same plus `-DDRIFT_DIAG=1`, which also turns on `DRIFT_WOM` and `DRIFT_TOUCH_STANDBY`, and logs frame and memory timings | 1823984 | `901992411660121e1380a5c439be97463a3066cfe938da62c2f677263ce24466` |

App size: 1,757,257 bytes (normal) and 1,758,037 bytes (diagnostic) of the 3,145,728-byte app partition (55%). Static RAM: 73,808 and 73,888 bytes of 327,680 (22%).

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
| `simulate` (interface, network scripted) | 491 of 491 |
| `simulate_network` (interface with the real network code) | 54 of 54 |
| `net_test` (network code) | 239 of 239 |
| `power_test` (sensor registers, screen-off controller) | 83 of 83 |
| Host build | g++ -std=c++17 -Wall -Wextra -Werror with address and undefined-behaviour sanitizers (`HOST_STRICT=1`): no warnings, no sanitizer reports |
| `mutation_check.sh` | 19 of 19 planted bugs caught |
| Firmware warnings, our code (`DRIFT_WARNINGS=all`) | 0 in both images |
| `tools/validate_image.py 0.14.0` | both images pass (layout, partition checksum, size, version, no old product name or test passwords) |

## Acceptance

- Software: passed, as above.
- Hardware: **pending**. v0.14.0 has not run on the watch yet; v0.11.1 was verified booting on hardware (PSRAM frame buffers, touch and motion sensor found).
