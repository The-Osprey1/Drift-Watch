# Drift-Watch v0.11.1: iPhone Bluetooth, Wi-Fi and sync page fixes

Fixes iOS Bluetooth and Wi-Fi connection failures on top of v0.11.0, turns on the live GitHub Pages sync page at https://the-osprey1.github.io/DriftWatch/, and aligns `platformio.ini` with `build.sh`.

## What changed

| Problem | Before | Now |
|---|---|---|
| Bluetooth MTU and write mode (`ble_esp32.cpp`) | `BLEDevice::setMTU()` was not called, leaving Bluedroid on the 23-byte default ATT MTU (20 bytes of payload), and `COMMAND_UUID` offered `PROPERTY_WRITE_NR`. On iPhones (Bluefy and WebBLE), `Z=`, `N=`, `S=` and `P=` lines over 20 bytes were cut off or refused, and status notifications were cut off at 20 bytes | Calls `BLEDevice::setMTU(247)` straight after `BLEDevice::init()`, offers acknowledged `PROPERTY_WRITE` only on `COMMAND_UUID`, sets `setMinPreferred(0x06)` and `setMaxPreferred(0x12)`, and reassembles 20-byte `0x01`/`0x02` pieces if a browser wrapper still splits long writes |
| Drift sync page on iOS (`companion/index.html`, GitHub Pages) | GitHub Pages was not turned on, so the `Publish the Drift sync page` CI job failed and there was no `https://` address for Bluefy to open; `requestDevice` listed `namePrefix` before the 128-bit service UUID | GitHub Pages workflow deployment is enabled at https://the-osprey1.github.io/DriftWatch/. The page filters by the primary-advertised service UUID first, falls back to 20-byte `0x01`/`0x02` pieces if a long write is refused, tolerates a failed initial status read, and notes iOS Personal Hotspot's **Maximize Compatibility** (2.4 GHz) setting |
| WPA2/WPA3 routers and iPhone Personal Hotspots (`net.cpp`) | `saveNetwork()` and `saveWifi()` wrote a zeroed `wifi_config_t` (leaving Protected Management Frames and scan threshold unset), and ESP32 Arduino core 2.0.17 leaves `sae_pwe_h2e` unset (`0`), so WPA3 SAE Hash-to-Element networks — including iPhone Personal Hotspots — refused the join | Every join (`saveNetwork()`, `saveWifi()` and `startSync()`) sets `pmf_cfg.capable = true`, `pmf_cfg.required = false`, `sae_pwe_h2e = WPA3_SAE_PWE_BOTH`, `WIFI_ALL_CHANNEL_SCAN`, `WIFI_CONNECT_AP_BY_SIGNAL` and `threshold.rssi = -127` (`WIFI_AUTH_WPA_PSK` when a password is given, `WIFI_AUTH_OPEN` when blank). 32-byte SSIDs are kept whole without cutting the 32nd byte |
| Straight vs curly apostrophes in SSIDs (`net.cpp`) | iPhones name their hotspot with a curly apostrophe (`’`, U+2019) and iOS Smart Punctuation types one in web forms, while computer keyboards and most routers use straight ASCII `'`. Typing one when the network broadcast the other failed with `NO_AP_FOUND` (201) | When a join fails with `WIFI_REASON_NO_AP_FOUND` and the saved SSID holds `'` or `’`/`‘`, the watch tries the other apostrophe once. If that connects, the working name is kept; otherwise the original name is restored |
| PlatformIO config (`platformio.ini`) | Used `flash_mode = qio` and hardcoded `COM6` | Uses `flash_mode = dio` (matching `build.sh`) and leaves the serial port unset so PlatformIO detects it automatically |

## Checks

All computer checks run under `HOST_STRICT=1` (`-Wall -Wextra -Werror` with AddressSanitizer and UndefinedBehaviorSanitizer):

| Test | Result | What was added |
|---|---|---|
| `render_faces` | 0 problems | Unchanged |
| `simulate` | 404 of 404 | Unchanged |
| `simulate_network` | 39 of 39 | Runs the real interface and updated `net.cpp` together |
| `net_test` | 223 of 223 | 9 new checks: PMF, WPA3 SAE H2E, all-channel scan and auth threshold on `saveNetwork()`, `saveWifi()` and `startSync()`; 32-byte open SSID preservation; straight-to-curly and curly-to-straight apostrophe fallback; original SSID restored when neither exists |
| `power_test` | 83 of 83 | Unchanged |
| `mutation_check.sh` | 10 of 10 caught | 3 new planted bugs caught: `pmf-disabled`, `wpa3-sae-h2e-disabled`, `apostrophe-fallback-skipped` |

## On the watch

Verified on a Waveshare ESP32-S3-Touch-LCD-1.28 over USB-C (`CH343P`, `115200` baud):

- Boot log reports `ui: frame 16-bit, spare yes, cache yes`, `touch: chip id 0xB5, firmware 0x02 (ok)`, `imu: QMI8658 at 0x6B, revision 0x7C (ok)`.
- For **Phone sync** on an iPhone or iPad, open https://the-osprey1.github.io/DriftWatch/ in the free **Bluefy** app, open **Settings > Phone sync** on the watch, tap **Connect to the watch**, and tap **Sync time**.
- For an **iPhone Personal Hotspot**, turn on **Settings > Personal Hotspot > Maximize Compatibility** (switches the hotspot from 5 GHz to 2.4 GHz) and keep that screen open while the watch joins.
