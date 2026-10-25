# DriftWatch precision face revamp

## Review
Evidence: current main `de0aa125`, the native v0.12.0 themed-face sheet, and the firmware drawing/motion code.

- Digital's saturated date banner competed with the time; the skyline added a second visual subject.
  Replaced with a quiet date register, yellow brackets and a small DRIFT signature.
- Four digital faces shared heavy, leaning segment digits. Added upright, tabular vector numerals
  with round strokes; Wasteland retains its distinct phosphor segments.
- Stealth, Corpo and themed dials shared a broad sword silhouette. Stealth/Corpo now use batons;
  themed hands are finer; Hologram keeps its skeleton geometry and Phantom its faceted hands.
- Radar's beam and trail ran through the central register. Their motion is now limited to a
  peripheral annulus (r=76..96), tested over a full revolution to keep r<74 unchanged.
- Wasteland's secondary text measured only 2.55:1 on its background after RGB565 conversion.
  The brighter supporting ink measures 8.93:1. Yozakura's supporting ink rises from 4.94:1 to 8.53:1.

## Motion decisions
Touch/slide interaction keeps its existing fast cadence. Decorative entrances and Orbit's
minute marker use a 33 ms budget (about 30 fps); simulation checks enforce both minimum and
maximum frame counts. Entrances remain finite, interruptible, correct from frame one and
absent on wake. Motion Off, low memory and low-power behavior are retained.
Phantom's eclipse, balance wheel and return behavior remain intact.

## Validation
CI compiles with warnings as errors and host address/undefined-behavior sanitizers.
The face renderer checks all twelve faces across 12/24-hour, unset/sync/failure, battery and
charging states, entrance endpoints, time protection and petal paths.
New checks verify all ten numerals at every used size, blank leading cells, unique glyphs,
declared bounds and the radar's protected central register.

## Native visual review
Inspected the gallery and radar / Phantom / Orbit / Yozakura animation-state sheet from CI.
The first pass exposed a Yozakura blossom touching the taller time; the branch endpoint and
last blossom were moved above the numeral block. Digital time, analog hands, date registers
and the peripheral radar remain legible at native resolution.

## Executed checks
The first full build passed both normal and diagnostic firmware with zero compiler warnings.
Host suites passed all face/state checks, 479 interface checks, 54 integrated-network checks,
239 network checks and 83 power checks after aligning the two cadence assertions with the
intentional 30 fps decorative budget. Mutation testing is tracked on the PR checks.
Two snapshot sheets are committed; each CI run also produces a twelve-second motion GIF.
Physical watch timing, LCD appearance and battery impact still require a device test.
