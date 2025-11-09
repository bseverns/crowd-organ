# Crowd Organ OSC Schema

All messages originate from **CrowdOrganHost**.

Default destinations:

- Dashboard:
  - Host: `127.0.0.1`
  - Port: `9000`
- Synth (SuperCollider by default):
  - Host: `127.0.0.1`
  - Port: `57120`

You can add or remove OSC outputs in the host app as needed.

## Per-voice messages

### `/room/voice/state`

Continuous per-frame state of a voice (pipe).

- Address: `/room/voice/state`
- Args:
  1. `int32` — `voiceId` (0..N-1)
  2. `float` — `x` (normalized horizontal position, ~-1.0..1.0)
  3. `float` — `y` (normalized vertical position, ~0.0..1.0)
  4. `float` — `z` (normalized depth, ~0.0..1.0)
  5. `float` — `size` (0.0..1.0, relative blob size)
  6. `float` — `motion` (0.0..1.0, instantaneous motion energy)
  7. `float` — `energy` (0.0..1.0, smoothed "loudness" proxy)

### `/room/voice/active`

Voice lifecycle changes.

- Address: `/room/voice/active`
- Args:
  1. `int32` — `voiceId`
  2. `int32` — `active` (0 = off, 1 = on)

Intended use: allocate or free synth voices when blobs appear/disappear.

### `/room/voice/note`

Explicit pitch/velocity mapping.

- Address: `/room/voice/note`
- Args:
  1. `int32` — `voiceId`
  2. `float` — `note` (e.g. MIDI note number 0–127, as float)
  3. `float` — `velocity` (0.0–1.0)

A synth engine might create one synth instance per `voiceId` and update parameters from
both `/state` and `/note`.

## Camera motion messages

### `/room/global/motion`

Aggregate motion across all cameras.

- Address: `/room/global/motion`
- Args:
  1. `float` — `globalMotion` (0.0..1.0), smoothed average motion level

Intended use: global modulation source (e.g., reverb amount, overall density, master filter
tilt, etc.).

---

### `/room/camera/zones`

Per-camera, per-zone motion grid.

- Address: `/room/camera/zones`
- Args:
  1. `int32` — `cameraId` (0..NUM_CAMERAS-1)
  2. `int32` — `cols` (number of columns in the grid)
  3. `int32` — `rows` (number of rows in the grid)
  4. `float` × (`cols * rows`) — `zoneMotion` values (0.0..1.0, row-major order)

Interpretation:

- The image from each webcam is divided into a `cols × rows` grid.
- For each cell, a motion energy value is computed from frame differences:
  - 0.0 = no apparent motion
  - 1.0 = strong motion in that zone

Intended use:

- Map specific zones to specific parameters:
  - e.g., "top-right of camera 1" → delay feedback,
  - "bottom-left of camera 0" → distortion drive,
  - sums of certain zones → per-engine excitation.

## Extensions

You can extend the schema with, for instance:

- `/room/global/reset` — command to clear all voices and motion buffers.
- `/room/config/*` — configuration or calibration messages (e.g., adjusting thresholds, grid
  sizes, or mapping ranges from a GUI).

Keep everything under `/room/...` to avoid collisions.
