# Drift-Watch v0.10.0: Architecture Proposals & Design Roadmap

This document outlines the proposed architectural upgrades, physical wrist findings, and new watch face concepts gathered during live watch testing on the Waveshare ESP32-S3-Touch-LCD-1.28.

---

## 1. Summary of Proposals

| Area | Current State (v0.9.0) | Proposed Upgrade (v0.10.0) |
|---|---|---|
| **Orientation** | Standard 0° orientation | **Rotate 90° clockwise** (display buffer + CST816S touch affine transform) to match natural wrist/strap ergonomics |
| **New Watch Face** | 5 Faces (Digital, Stealth, Corpo, Holo, Radar) | **"Vanishing Second Hand" (Phantom / Eclipse Face)**: dial aperture with mechanical tourbillon oscillation and shadow ingestion |
| **Connectivity** | Wi-Fi SoftAP only (`Drift-Setup` at 192.168.4.1) | **BLE Web Bluetooth / Current Time Service (CTS)**: 1-tap phone sync via browser without connecting to an ad-hoc Wi-Fi network; captive DNS fix |
| **Timekeeping** | Manual time setting or NTP sync | **Light/Deep Sleep RTC Persistence** using ESP32-S3 internal slow clock so time survives sleep periods |
| **Wrist Gestures** | Basic Z-axis threshold polling | **6-Axis Kinematic Gesture Engine**: angular velocity roll rate + pitch window ($35^\circ \to 65^\circ$) + twist-to-sleep |
| **Radar Face** | 1.1s rapid sweep | **Re-tuned to 3.5s authentic CRT/PPI phosphor sweep** with exponential trail decay |

---

## 2. 90° Clockwise Screen & Touch Coordinate Transformation

### A. Display Rotation
- In `platformio.ini` / `TFT_eSPI` setup and initialization, rotate the display coordinate space 90° clockwise:
  $$\text{Rotation} = (\text{Current Rotation} + 1) \pmod 4$$
- Ensure all cached PSRAM sprites (`TFT_eSprite`) respect the native 240×240 pixel memory stride.

### B. Touch Coordinate Affine Transform
The CST816S capacitive touch controller reports coordinates relative to its physical silicon matrix. When the display is rotated 90° clockwise, raw touch samples $(X_{\text{raw}}, Y_{\text{raw}})$ must be remapped:

$$\begin{cases} X_{\text{screen}} = 239 - Y_{\text{raw}} \\ Y_{\text{screen}} = X_{\text{raw}} \end{cases}$$

- Swipe axes in `ui.cpp` are swapped accordingly:
  - Horizontal swipe (Face switching / Paging) maps to physical horizontal wrist swipes.
  - Vertical swipe (Pulling down to dismiss, swiping up for Settings) maps to physical vertical swipes.
- Hit-testing tables in `screens.cpp` maintain 68px minimum touch targets calibrated to the rotated circle.

---

## 3. "Vanishing Second Hand" Watch Face (Phantom / Eclipse Face)

Inspired by conceptual horology and mechanical mystery watches (such as the Cartier *Pendule Mystérieuse* and retrograde dials):

```
                      12 o'clock
                 .---.---------.---.
              .-'     \  VOID /     '-.
            .'         \ APER/         '.
           /     55s    \TUR/    05s     \
          ;      ===>    \ /    ===>      ;
         :        Needle  |  Re-emerges    :
         |       dissolves|  from shadow   |
         |                O                |
         :             (Pivot)             :
          ;                               ;
           \                             /
            '.                         .'
              '-.                   .-'
                 '---.---------.---'
                      6 o'clock
```

### A. The Optical Illusion Mechanics
A physical hand cannot evaporate; it relies on spatial misdirection:
1. **The Cutout Aperture**: A recessed wedge cut into the dial plate between $328^\circ$ (55s) and $32^\circ$ (05s) reveals an active oscillating tourbillon balance wheel (4 Hz / 28,800 vph) with breathing hairspring and ruby jewel.
2. **Drop-Shadow Ingestion**: When the second hand sweeps past 55s, the overhanging dial plate casts a realistic ambient drop-shadow over the needle. The needle visibly slips *underneath* the dial into the chamber.
3. **Total Eclipse**: Between 58s and 02s, the second hand is completely omitted across 12 o'clock.
4. **Mechanical Spring Rebound**: When the hand re-emerges at 05s, it simulates a physical retrograde hand striking a banking pin with damped harmonic oscillation:
   $$\theta_{\text{draw}} = \theta + e^{-8f} \sin(4\pi f) \times 1.8^\circ$$

### B. Mathematical Length Model
```cpp
float calculateNeedleLength(float deg, float maxLen = 100.0f, float minLen = 22.0f) {
  while (deg >= 360.0f) deg -= 360.0f;
  while (deg < 0.0f)    deg += 360.0f;

  if (deg >= 30.0f && deg <= 330.0f) {
    return maxLen; // Standard visible sweep
  } else if (deg > 330.0f && deg <= 352.0f) {
    float f = (352.0f - deg) / 22.0f; // Diving into shadow
    return minLen + (maxLen - minLen) * f;
  } else if (deg > 352.0f || deg < 8.0f) {
    return 0.0f;   // Completely vanished
  } else {
    float f = (deg - 8.0f) / 22.0f;   // Re-emerging from lip
    return minLen + (maxLen - minLen) * f;
  }
}
```

An interactive prototype is available in `designs/vanishing_second_hand_prototypes.html`.

---

## 4. Connectivity & Timekeeping Revolution

### A. The Root Problem with Wi-Fi Captive Portals on Wearables
- Requiring phones to disconnect from home Wi-Fi and join an unencrypted `Drift-Setup` ad-hoc network causes major friction.
- Modern iOS and Android devices aggressively suppress 2.4 GHz ad-hoc networks without internet or steer toward 5 GHz.
- The 5-minute portal timeout causes the network to disappear before users can finish pairing.

### B. Proposed Solution: BLE Web Bluetooth Companion
- Implement a lightweight BLE GATT server:
  - Standard **Current Time Service (CTS, UUID 0x1805)**.
  - Or a Nordic UART Service (NUS) profile.
- **Web Bluetooth Pairing**: Host a zero-install static webpage (e.g. on GitHub Pages). The user opens the page on Safari/Chrome on their phone, taps **"Sync Time"**, and the browser securely connects via BLE to send:
  - Exact UTC epoch timestamp.
  - POSIX time zone string.
  - Home Wi-Fi credentials (optional).
- Time sync takes under 1.5 seconds with zero Wi-Fi hopping.

### C. Captive Portal Hardening (Wi-Fi Fallback)
If Wi-Fi setup is retained:
- Explicitly configure static AP IP: `WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0)`.
- Set SoftAP transmit power to `WIFI_POWER_19_5dBm`.
- Implement an RFC 7710 Captive DNS server redirecting all DNS queries to `192.168.4.1` so iOS (`captive.apple.com`) and Android (`connectivitycheck.gstatic.com`) immediately pop the login sheet.

---

## 5. 6-Axis Motion & Wrist Gesture Kinematics

### A. Current Limitation
v0.9.0 checks gravity vector $G_z$ on wake polling. It does not differentiate between arm elevation, walking sway, or intentional wrist turns.

### B. Dual-Stage Kinematic Model (Garmin / Apple watchOS Pattern)
1. **Roll-Rate Trigger**: Sample QMI8658 gyroscope. An intentional wrist flick exhibits angular velocity $\omega_x > 80^\circ/\text{s}$.
2. **Presentation Pitch Window**: Screen pitch angle held between $35^\circ$ and $65^\circ$ towards the face.
3. **Rest Stability**: Accelerometer norm $|a_{\text{net}} - 1.0g| < 0.2g$ for $> 150\text{ ms}$ (rejects continuous motion while running/walking).
4. **Twist-to-Sleep**: Turning wrist away ($G_z < 0.2g$) dims and puts screen to sleep within 400 ms, saving substantial battery.

### C. Hardware Interrupt Waking
Wire QMI8658's Wake-On-Motion (WOM) interrupt pin on GPIO 4 to `gpio_wakeup_enable()`, waking the CPU from light sleep only on genuine motion rather than continuous timer dozing.

---

## 6. Radar Face Kinematics Tuning

- **Current Behavior**: Single sweep over 1.1s, which feels rushed and jittery.
- **Proposed Tuning**:
  - Sweep duration increased to **3.5 seconds per revolution** (approx. 17 RPM).
  - True exponential phosphor decay trail:
    $$\alpha(i) = \alpha_0 \cdot e^{-k \cdot i}$$
  - Smooth 30–60 FPS sweep during active wake.

---

## 7. Interactive Artifacts in Repository

- `designs/vanishing_second_hand_prototypes.html`: Full HTML5 Canvas interactive prototype featuring Haute Mechanical, Quantum Void, and Ghost Eclipse designs with time scrubbing, 90° rotation preview, and mechanical spring rebound simulation.
