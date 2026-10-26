# Drift-Watch v0.15.2: Cupertino Studio & Direct Touch Controls

`v0.15.2` refines both the Web Studio and the on-watch touch navigation into a clean, buttonless circular touch experience with adaptive Light/Dark Cupertino styling, unclipped typography, and all `v0.15.1` daily-driver reliability fixes built in.

## Highlights

- **Buttonless 240×240 Touch Navigation**:
  - **Swipe left / right** on the watch face to glide between active faces.
  - **Swipe down** on the watch face to open the **Quick Drawer** (1m / 3m / 5m countdown timers with progress ring and 2-minute auto-timeout flashlight).
  - **Swipe up** on the watch face to open **Settings** (`Display`, `Clock & Sync`, `Gestures`, `System Info`).
- **Simplified Cupertino Web Studio**:
  - Adaptive **Dark** and **Light** appearance with translucent vibrancy materials and dynamic accent color syncing to the active watch face.
  - Streamlined 4-tab deck (**Face Gallery**, **Sync & Wi-Fi**, **Quick Tools**, **Diagnostics**) with collapsible disclosure drawers for secondary tools.
  - Spacious 2-column watch face gallery cards and stage readouts with zero text truncation (`text-overflow: ellipsis` removed).
- **Bluetooth & USB-C Sync Improvements**:
  - **Bluetooth** always opens the browser device picker (`requestDevice`) so switching watches or reconnecting on mobile/desktop never gets stuck on a stale remembered device (`PR #22`).
  - Expanded USB-C and BLE command set (`INFO?`, `TIME:`, `WIFI:`, `FACES:`, `TIMER:`, `BRIGHT:`, `MOTION:`, `FORMAT:`, `NIGHT:`, `FACE:`).
  - Includes all `v0.15.1` daily-driver fixes (timer alarm ownership across all faces, clean Settings repaint on alarm dismiss, 2-minute flashlight auto-return, and UART wake from light sleep).
