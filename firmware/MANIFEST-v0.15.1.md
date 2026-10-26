# Drift-Watch v0.15.1 release manifest

| | |
|---|---|
| Source commit | `d7e345a2a5b835c668e607b6c556dae84f194e54` (`main`, PR #21 merge) |
| Release version | `v0.15.1` |
| Built | 2026-10-26, on the Windows PC "Oppenheimer" with `build.sh` (`DRIFT_WARNINGS=all`, then `DRIFT_DIAG=1`), in a separate arduino-cli setup at `C:\Users\devyn\arduino-driftwatch`; both builds exited 0 with no warnings |
| arduino-cli | 1.1.1 (download checked against the official v1.1.1 release checksum) |
| ESP32 Arduino core | esp32:esp32 2.0.17 (ESP-IDF 4.4); its `esptool.exe` 4.5.1 called through a two-line `esptool.py` wrapper outside the repo |
| Libraries | TFT_eSPI 2.5.43, WiFiManager 2.0.17 (tzapu) |
| Board | `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc` |
| Flash | DIO, 80 MHz, 16 MB; one merged image per build, flashed at 0x0 |

These are the same tool versions the CI uses.

## Images

| File | Flags | Bytes | SHA-256 |
|---|---|---|---|
| `drift_watch_v0.15.1.bin` | standard build; `DRIFT_WOM=0`, `DRIFT_TOUCH_STANDBY=0`, `DRIFT_LIGHT_SLEEP=1` (defaults) | 1828784 | `7d407e09c89984680000737acdc4e0bb1d8835157642bbdcb8e7d5e2d401aa03` |
| `drift_watch_v0.15.1_diag.bin` | `-DDRIFT_DIAG=1`, `DRIFT_WOM=1`, `DRIFT_TOUCH_STANDBY=1`, logs frame and memory timings | 1829584 | `93fd18f3863e7e47621cdf456a26d69ab20140e9be2b5fadee9d887032932a53` |

The app uses 56% of its 3 MB partition in both images.

## Merged image layout

| Offset | Part |
|---|---|
| 0x0 | bootloader |
| 0x8000 | partition table |
| 0xE000 | boot_app0 (OTA data) |
| 0x10000 | app |

The partition table is the same as v0.15.0 (nvs, otadata, app0 and app1 at 3 MB each, ffat, coredump). `tools/validate_image.py 0.15.1` passed every line for both images: layout, partition table, size, version, timing log in the diagnostic build only, and no stray names or test passwords.

## Acceptance

- **Software:** all host test suites passed in strict mode (address and undefined behaviour checkers on) on this commit in [CI run 40258377261](https://github.com/The-Osprey1/DriftWatch/actions/runs/40258377261): faces 0 problems, interface 560/560, interface with network 54/54, network 239/239, power 87/87, and 26/26 planted bugs caught. They were not run on the PC (no g++ there).
- **Hardware:** **partly verified on the watch.** Passed: USB-C sync with the screen off (`DriftWatchSync.ps1`, serial wake from light sleep) and Windows sync on plug-in (`RegisterAutoSyncTask.ps1`). Not yet checked on the watch: the timer alarm on Radar and Phantom with the screen off, dismissing the alarm in Settings, the flashlight time-out, and the Drift sync page over USB-C.
