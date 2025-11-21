# Crowd Organ

A polyphonic instrument where a moving crowd becomes the pipes and the room becomes the organ.

This repo contains:

- `of_app/` — an openFrameworks app (**CrowdOrganHost**) that:
  - reads Kinect v1 depth + RGB,
  - reads 2× UVC webcams,
  - clusters depth into "pipes" (voices),
  - computes per-voice features (position, size, motion),
  - estimates motion fields from each webcam,
  - runs sliding-window gesture detectors,
  - sends those features and gesture hits out over OSC.
- `processing_dashboard/` — a Processing sketch (**CrowdOrganDashboard**) that:
  - listens for OSC from the host,
  - visualizes the active pipes/voices and their parameters,
  - shows global motion, per-camera motion grids, and a live gesture ticker.
- `sc/` — a SuperCollider script (**crowdOrgan.scd**) that:
  - listens for the same OSC stream,
  - instantiates one synth per `voiceId`,
  - maps position, energy, and note into a simple pipe-like voice,
  - reacts to gestures by flipping registrations, envelopes, and FX scenes.

> The host now throws `/room/gesture/*` events. Grab the playbook in
> `docs/crowd_organ_gesture_design_notes.md` and wire them into your synth or dashboard.

Any synthesis engine (SuperCollider, Pd, Max, DAW via OSC→MIDI bridge) can
listen to the OSC stream and treat the crowd and space as an organ console.

## Quickstart (grab-and-go)

If you want to be playing noise tonight, start here. We keep the ritual
practical, with side quests for folks who like to peek under the hood.

### Environment prep

- **macOS (tested conceptually on 10.15 Catalina)**
  - Install Xcode command line tools (`xcode-select --install`).
  - Install Homebrew, then grab Kinect + utility deps:

    ```bash
    brew install libfreenect
    ```

  - Download [openFrameworks 0.11+ for macOS](https://openframeworks.cc/download/) and unpack it somewhere easy, e.g. `~/of_v0.11.2_osx_release`.
- **Linux (Ubuntu-ish)**
  - Install build tools + freenect via your package manager (names vary: `libfreenect-dev`, `libusb-1.0-0-dev`, `freeglut3-dev`, `g++`, `make`).
  - Download the [openFrameworks 0.11+ Linux release](https://openframeworks.cc/download/) that matches your distro/architecture and run the bundled `install_dependencies.sh` + `install_codecs.sh` scripts.
  - Expect to fight udev rules for Kinect; the [OpenKinect docs](https://openkinect.org/wiki/Getting_Started) are your friend.

### Required openFrameworks addons

The host relies on exactly three addons (they already live in `of_app/addons.make`):

- `ofxKinect` (Kinect v1 via libfreenect)
- `ofxOsc`
- `ofxOpenCv`

The openFrameworks Project Generator will pull these in automatically when you
import `of_app/`, but double-check they exist inside your `addons/` folder.

### Configuration: `gesture_settings.json`

Drop a config file next to the host binary (openFrameworks reads from `bin/data/`), e.g. `of_app/bin/data/gesture_settings.json`:

```json
{
  "listen_port": 9001,
  "gesture_host": "127.0.0.1",
  "gesture_port": 9000,
  "enable_sending": true
}
```

- `listen_port`: where the host listens for incoming OSC (e.g., from calibration tools).
- `gesture_host` / `gesture_port`: where the host broadcasts `/room/gesture/*` (dashboard, synths, etc.).
- `enable_sending`: flip off if you want to run headless without emitting OSC.

If the file is missing, the host logs a warning and falls back to built-in defaults, so touring rigs can live dangerously.

### Run steps (host → dashboard → synth)

1. **Wire the room**
   - Plug in the Kinect and both webcams. Give USB hubs a pep talk if they start to brown out.
2. **Build + run the host (CrowdOrganHost)**
   - Copy `of_app/` into `apps/myApps/` inside your openFrameworks tree (or start a fresh OF project and paste in `src/` + `addons.make`).
   - Open the generated IDE project (Xcode, Qt Creator, or Makefile workflow) and build.
   - Ensure `gesture_settings.json` sits in `bin/data/` next to the app bundle/binary.
   - Launch the app; watch the console for Kinect connection status.
3. **Fire up the Processing dashboard**
   - Open `processing_dashboard/CrowdOrganDashboard.pde` in Processing 4.x.
   - Make sure the `oscP5` and `netP5` libraries are installed via Processing’s Contribution Manager.
   - Hit run; it listens on `127.0.0.1:9000` by default and will start drawing voices as soon as OSC arrives.
4. **Boot the SuperCollider patch**
   - Open `sc/crowdOrgan.scd` in SuperCollider and evaluate the file (Cmd/Ctrl+Enter).
   - It spins up a synth per `voiceId` and listens on port `57120` for the host’s stream.
5. **Add your own listener(s) if you’re feeling spicy**
   - Any OSC-capable tool (Pd, Max, DAW via OSC→MIDI) can co-listen. Keep the addresses in sync with `docs/OSC_SCHEMA.md` so nothing gets lost in translation.

### OSC sanity check (10-second confidence pass)

Before a show, make sure the pipeline speaks. With the host running:

1. In a second terminal, send a test message to the dashboard port (replace with your IPs if remote):

   ```bash
   oscsend 127.0.0.1 9000 /room/voice/state iiiiifff 1 0 0 0 0 0.5 0.5 0.5
   ```

   - If `oscsend` isn’t available, use any OSC poke tool (SuperCollider’s `NetAddr("127.0.0.1", 9000).sendMsg("/room/voice/state", 1, 0, 0, 0, 0, 0.5, 0.5, 0.5);`).
2. Confirm the dashboard flashes a ghost voice and SuperCollider logs the message. If either side is silent, re-check ports from `gesture_settings.json`.

For deeper OSC spelunking, see `docs/OSC_SCHEMA.md`.

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

> ⚙️ **Why stick with openFrameworks instead of “plain” C++?** The host is an
> ordinary C++14 project under the hood—the same Clang/GCC toolchains build it
> locally and in CI. We still lean on openFrameworks because it ships the
> windowing/GL glue, realtime event loop, addon ecosystem, and battle-tested
> project scaffolding that let us focus on the crowd→sound logic instead of
> rebuilding an engine from scratch. Treat it like a friendly standard library
> extension rather than a bespoke compiler.

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
- `/room/gesture/voice i s f f`
  - `voiceId, type, strength, extra (endpointY or duration)`
- `/room/gesture/zone i s f [i]`
  - `cameraId, type, strength, zoneIndex (for pulses)`
- `/room/gesture/global s f`
  - `type, strength`

## Running the system

The Quickstart above is the opinionated path; this section keeps the more
traditional checklist for reference. If you want to riff on the gestures and
what they mean musically, the zine in `docs/crowd_organ_gesture_design_notes.md`
is your north star.

## Next steps

- Refine clustering and voice assignment logic (e.g., more robust blob tracking).
- Tune the musical mapping (notes, CCs, timbre indices) in `docs/OSC_SCHEMA.md`.
- Use `docs/CAMERA_CALIBRATION.md` to decide what different room zones mean musically.
- Add per-room configuration if you plan to move installations.

MIT licensed; see `LICENSE`.
