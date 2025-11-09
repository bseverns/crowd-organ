# Crowd Organ

A polyphonic instrument where a moving crowd becomes the pipes and the room becomes the organ.

This repo contains:

- `of_app/` — an openFrameworks app (**CrowdOrganHost**) that:
  - reads Kinect v1 depth + RGB,
  - reads 2× UVC webcams,
  - clusters depth into "pipes" (voices),
  - computes per-voice features (position, size, motion),
  - estimates motion fields from each webcam,
  - sends those features out over OSC.
- `processing_dashboard/` — a Processing sketch (**CrowdOrganDashboard**) that:
  - listens for OSC from the host,
  - visualizes the active pipes/voices and their parameters,
  - shows global motion and per-camera motion grids.
- `sc/` — a SuperCollider script (**crowdOrgan.scd**) that:
  - listens for the same OSC stream,
  - instantiates one synth per `voiceId`,
  - maps position, energy, and note into a simple pipe-like voice.

Any synthesis engine (SuperCollider, Pd, Max, DAW via OSC→MIDI bridge) can
listen to the OSC stream and treat the crowd and space as an organ console.

## Hardware

- 1× Microsoft Kinect v1 (Xbox 360 version)
- 1× Kinect v1 AC power/USB adapter
- 2× HD webcams (UVC-compliant, e.g. 1920×1080 or 1280×720)
- macOS machine (tested conceptually on macOS 10.15, Catalina)
- Optional future target: Linux box for "appliance" deployment

## Software stack

### openFrameworks app (`of_app/`)

- [openFrameworks](https://openframeworks.cc/) (0.11+)
- Addons:
  - `ofxKinect` (Kinect v1 via libfreenect)
  - `ofxOsc`
  - `ofxOpenCv`
- Dependencies:
  - `libfreenect` (OpenKinect) installed on macOS, e.g. via Homebrew:

    ```bash
    brew install libfreenect
    ```

### Processing dashboard (`processing_dashboard/`)

- [Processing](https://processing.org/) 4.x
- Libraries:
  - `oscP5`
  - `netP5`

### SuperCollider script (`sc/`)

- [SuperCollider](https://supercollider.github.io/)
- Loads `crowdOrgan.scd` and boots the server.
- Listens on the language port (57120) for `/room/voice/*` and camera motion messages.

## OSC overview

By default the host app sends OSC to:

- Dashboard: `127.0.0.1:9000`
- SuperCollider: `127.0.0.1:57120`

Message shapes (full details in `docs/OSC_SCHEMA.md`):

- `/room/voice/state i f f f f f f`
  - `voiceId, x, y, z, size, motion, energy`
- `/room/voice/note i f f`
  - `voiceId, note, velocity`
- `/room/voice/active i i`
  - `voiceId, activeFlag (0|1)`
- `/room/global/motion f`
  - `globalMotion` (0..1)
- `/room/camera/zones i i i f...`
  - `cameraId, cols, rows, zoneMotion[]` (0..1 per cell)

## Building the openFrameworks app (macOS)

1. Install openFrameworks for macOS from the official site.
2. Install libfreenect:

   ```bash
   brew install libfreenect
   ```

3. Copy the `of_app/` folder into your `apps/myApps/` folder inside your openFrameworks tree
   (or create a new project and transplant the `src/` files).
4. Ensure `addons.make` lists the required addons.
5. Open the generated Xcode project and build/run.

## Running the system

1. Connect Kinect and webcams to the Mac.
2. Start SuperCollider, open `sc/crowdOrgan.scd`, and evaluate the file.
3. Start the `CrowdOrganHost` openFrameworks app.
4. Start the `CrowdOrganDashboard` Processing sketch:
   - It will listen on port `9000` for OSC updates.
5. Optionally add other synth engines (Pd, Max, DAW) listening to the same OSC.

## Next steps

- Refine clustering and voice assignment logic (e.g., more robust blob tracking).
- Tune the musical mapping (notes, CCs, timbre indices) in `docs/OSC_SCHEMA.md`.
- Use `docs/CAMERA_CALIBRATION.md` to decide what different room zones mean musically.
- Add per-room configuration if you plan to move installations.

MIT licensed; see `LICENSE`.
