# Crowd Organ Camera & Zone Calibration Notes

This document is a working notebook for deciding **what each camera and zone means musically**
inside the Crowd Organ.

The goal is not photogrammetry-level calibration, but a repeatable way to say:

> "When someone moves in *this* corner as seen by *this* camera, we know which musical
> stop or pipe family is being tugged."

## 1. Physical layout notes

For each installation, capture a quick sketch:

- Where is the Kinect? Which direction is it facing?
- Where are Camera 0 and Camera 1 mounted?
- Rough distances from cameras to the main interaction volume.
- Any major occluders (columns, pillars, furniture) that cast "motion shadows".

You can keep a plain-text block like:

```text
Room name: ______________________
Kinect:   mounted on ______ wall, height ~____cm, facing ______
Cam 0:    mounted on ______ wall, height ~____cm, facing ______
Cam 1:    mounted on ______ wall, height ~____cm, facing ______
Notes:    _______________________
```

## 2. Understanding the grids

Each camera sends a grid of zones:

- `cols` × `rows` (e.g., 4×4 = 16 cells)
- Message: `/room/camera/zones camId cols rows zone0 zone1 ... zoneN`

Zone ordering is **row-major**:

```text
index = row * cols + col

(0,0) (1,0) (2,0) (3,0)
(0,1) (1,1) (2,1) (3,1)
(0,2) ...
(0,3) ...
```

For a 4×4 grid:

```text
  0   1   2   3
  4   5   6   7
  8   9  10  11
 12  13  14  15
```

You can treat each index as a tiny **motion fader** in that patch of the camera image.

## 3. Mapping zones to musical roles (stops, couplers, etc.)

A simple approach:

1. Stand in the room and move deliberately in one patch at a time.
2. Watch the CrowdOrganDashboard heatmaps for each camera.
3. Note which zone index lights up for which piece of the physical room.

Then create a small table for each camera:

```text
Camera 0 (left side of room, facing stage)

Zone index  Physical area            Musical role
----------  -----------------------  ----------------------------
 0          upper-left (doorway)     adds shimmer / high reverb
 3          upper-right (projector)  widens stereo image
12          lower-left (near synth)  boosts bass drive
15          lower-right (audience)   increases delay feedback

Camera 1 (rear of room, facing center)

Zone index  Physical area            Musical role
----------  -----------------------  ----------------------------
 2          top center               opens band-pass filter
 7          right mid                raises grain density
 8          left floor               saturates noise layer
...
```

You can paste these tables here per installation, or keep them in separate room-specific
markdown files.

## 4. Deciding function families

It can be helpful to group zones into **function families**, e.g.:

- **Texture** (e.g., reverb length, shimmer, chorus depth)
- **Density** (e.g., grain rate, note rate, probability)
- **Harshness** (e.g., distortion drive, wavefold amount)
- **Space** (e.g., stereo width, reverb mix, panning bias)

Then assign:

- Camera 0 top rows → **Texture**
- Camera 0 bottom rows → **Harshness**
- Camera 1 left side → **Density**
- Camera 1 right side → **Space**

You can reflect this in your SuperCollider code by summing specific zone indices into named
control buses or environment variables.

## 5. Threshold and sensitivity tuning

If too much is happening:

- Increase the motion threshold in the host app:
  - Raise `thresholdValue` in `processKinect()` if Kinect blobs are noisy.
  - Increase `step` or adjust the mapping in `processCameras()` if camera zones flicker too
    easily.
- Add dead zones:
  - Decide that some cells always map to zero or are ignored in synthesis.

If too little is happening:

- Lower thresholds.
- Increase the blur radius or reduce downsampling to capture finer motion.
- Consider changing `camGridCols` and `camGridRows` (e.g., 3×2 for coarser zones).

## 6. Saving a calibration snapshot

Once you like how a particular room feels:

- Save:
  - The camera positions,
  - The zone-to-parameter tables,
  - Any threshold/grid size changes you made in the host app.

You can either:

- Commit those as code changes in a branch (e.g. `crowd-organ-lab-2025`), or
- Keep them as JSON/YAML in `config/` and have the host read them at startup.

The point is that "the corner that swells the drone" stays the same from night to night,
which makes the Crowd Organ feel like an instrument with a memory.
