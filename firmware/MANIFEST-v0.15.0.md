# Drift-Watch v0.15.0 release manifest

| | |
|---|---|
| Release version | `v0.15.0` |
| Built | GitHub Actions CI with `build.sh` (`DRIFT_WARNINGS=all`) |
| arduino-cli | 1.1.1 |
| ESP32 Arduino core | esp32:esp32 2.0.17 (ESP-IDF 4.4) |
| Compiler | xtensa-esp32s3-elf-gcc (crosstool-NG esp-2021r2-patch5) 8.4.0 |
| Libraries | TFT_eSPI 2.5.43, WiFiManager 2.0.17 (tzapu) |
| Board | `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc` |
| Flash | DIO, 80 MHz, 16 MB; one merged image per build, flashed at 0x0 |

## Images

| File | Flags | Bytes | Description |
|---|---|---|---|
| `drift_watch_v0.15.0.bin` | standard build; `DRIFT_WOM=0`, `DRIFT_TOUCH_STANDBY=0`, `DRIFT_LIGHT_SLEEP=1` (defaults) | ~1,827,904 | Standard release image |
| `drift_watch_v0.15.0_diag.bin` | `-DDRIFT_DIAG=1`, `DRIFT_WOM=1`, `DRIFT_TOUCH_STANDBY=1`, logs frame and memory timings | ~1,828,752 | Diagnostic telemetry image |

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

## Acceptance

- **Software**: All automated test suites (`simulate`, `simulate_network`, `net_test`, `power_test`, `render_faces`, `mutation_check.sh`) passed 100% under ASAN and UBSAN.
- **Hardware**: **Verified on physical watch**. Boot sequence, CST816S touch controller, QMI8658 motion sensor, battery divider telemetry, Settings 4-Hub fast-jump, swipe-down Quick Drawer, countdown timers, visual strobe alarm, and serial command protocol verified live on the Waveshare ESP32-S3-Touch-LCD-1.28.
