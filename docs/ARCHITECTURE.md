# Crowd Organ Architecture

High-level flow:

```text
Kinect v1 depth + RGB
        +
  2× HD webcams
        │
        ▼
CrowdOrganHost (openFrameworks)
  - capture + basic processing
  - depth clustering → "pipes" / voices
  - per-voice features: pos, size, motion
  - per-camera motion grids and global motion
  - OSC out
        │
        ├───────────────► CrowdOrganDashboard (Processing)
        │                     - visualization
        │                     - voice circles + camera grids
        │
        └───────────────► Synth engine (SC/Pd/Max/DAW etc.)
                              - polyphonic "pipes"
                              - global & zone-based modulation
```

## Components

### CrowdOrganHost (openFrameworks)

Responsibilities:

- Initialize Kinect + webcams.
- Grab frames each update.
- From Kinect depth:
  - Downsample and threshold to a foreground range (e.g., 1–4 m).
  - Find blobs in the thresholded depth image via OpenCV.
  - For each blob, compute:
    - 3D centroid (`x, y, z`) in Kinect space,
    - approximate `size`,
    - motion energy (difference vs. previous positions).
- Manage a fixed pool of "pipes"/voices:
  - Assign blobs to voices based on spatial proximity.
  - Spawn new voice when a new blob appears.
  - Deactivate voices when blobs disappear.
- From each webcam:
  - Convert frames to grayscale and blur.
  - Compute per-frame difference vs. previous frame.
  - Aggregate motion in a `camGridCols × camGridRows` grid.
  - Maintain smoothed `globalMotion` per camera and across cameras.
- Map voice features into:
  - pitch, velocity,
  - pan, brightness, and other continuous controls.
- Maintain short motion histories per voice/zone and run the gesture detectors.
- Emit OSC messages with:
  - per-voice state (`/room/voice/state`, `/room/voice/note`, `/room/voice/active`),
  - gesture events (`/room/gesture/voice`, `/room/gesture/zone`, `/room/gesture/global`),
  - global motion (`/room/global/motion`),
  - per-camera motion grids (`/room/camera/zones`).

### CrowdOrganDashboard (Processing)

Responsibilities:

- Listen for OSC messages on a configured port.
- Maintain a simple `Voice` structure mirroring the OSC schema.
- Maintain per-camera motion grids, a global motion value, and the rolling gesture log.
- Visualize:
  - active vs. inactive pipes/voices,
  - their positions and sizes,
  - optional text readout of pitch/velocity,
  - global motion meter,
  - per-camera heatmaps (zones that glow when motion increases),
  - gesture pings so the operator can hear/see what just fired.

Optional future:

- GUI controls that send OSC back to the host for live remapping or calibration.
- Overlays that annotate which camera zones mean which musical functions (stops, couplers, etc.).

### SuperCollider engine (crowdOrgan.scd)

Responsibilities:

- Listen for `/room/voice/*`, `/room/gesture/*`, and camera motion messages on OSC.
- Maintain one synth voice per `voiceId`.
- Map:
  - `note`, `velocity` → base frequency and amplitude,
  - `x` → pan or spatialization,
  - `energy`/`motion` → brightness, modulation index, grain density, etc.
- Use:
  - `/room/global/motion` as a master modulation source (e.g., reverb mix, master filter tilt),
  - `/room/gesture/global` to jump between registrations or scene presets,
  - `/room/camera/zones` as zone-specific modulators (e.g., certain corners increase distortion),
  - `/room/gesture/voice` and `/room/gesture/zone` to drive per-voice timbre flips, swells, and rhythmic accents.

The goal is for `CrowdOrganHost` to be musically agnostic: it just describes "what the crowd and space are doing" in a stable format that instruments can interpret in different ways.
