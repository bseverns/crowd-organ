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

## Implementation status

`of_app/` is the canonical home for the full Kinect/webcam host. The current
checked-in host already owns the OSC routing, gesture detectors, tuning reloads,
diagnostic HUD, Kinect depth blob extraction, webcam motion grids, and telemetry
emission. Its incoming `/room/voice/state`, `/room/camera/zones`, and
`/room/global/motion` receiver is still available as a bridge/test input that
feeds the same normalized feature path as live sensors.

Near-term hardening work is tracking that direction:

- keep `/room/camera/zones` canonical as `camId, cols, rows, zone...`;
- reject malformed or out-of-range OSC/config input before it mutates state;
- keep detector, replay, config-validation, and capture fixtures green before
  changing gesture logic further;
- refine the first-pass sensor pipeline with better voice tracking and richer
  per-room calibration workflows.

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

The host leans on four addons (already pinned in `of_app/addons.make`):

- `ofxKinect` (Kinect v1 via libfreenect)
- `ofxOsc`
- `ofxOpenCv`
- `ofxJSON` (lightweight config loader for `gesture_tuning.json`)

The openFrameworks Project Generator will pull these in automatically when you
import `of_app/`, but double-check they exist inside your `addons/` folder.

### Configuration: `gesture_settings.json`

Drop a config file next to the host binary (openFrameworks reads from `bin/data/`), e.g. `of_app/bin/data/gesture_settings.json`:

```json
{
  "listen_port": 9001,
  "gesture_host": "127.0.0.1",
  "gesture_port": 9000,
  "enable_sending": true,
  "enable_osc_input": true,
  "enable_sensors": true,
  "sensors": {
    "kinect_min_depth_mm": 700,
    "kinect_max_depth_mm": 4000,
    "max_kinect_voices": 8,
    "min_blob_area": 1200,
    "max_blob_area": 90000,
    "voice_match_distance": 0.35,
    "camera_width": 640,
    "camera_height": 480,
    "cam_grid_cols": 4,
    "cam_grid_rows": 4,
    "camera_motion_floor": 0.02,
    "camera_smoothing": 0.65
  },
  "routes": {
    "voice_state": {
      "address": "/room/voice/state",
      "host": "127.0.0.1",
      "port": 9000
    },
    "voice_active": {
      "address": "/room/voice/active",
      "host": "127.0.0.1",
      "port": 9000
    },
    "voice_note": {
      "address": "/room/voice/note",
      "host": "127.0.0.1",
      "port": 9000
    },
    "global_motion": {
      "address": "/room/global/motion",
      "host": "127.0.0.1",
      "port": 9000
    },
    "camera_zones": {
      "address": "/room/camera/zones",
      "host": "127.0.0.1",
      "port": 9000
    },
    "voice_gesture": {
      "address": "/room/gesture/voice",
      "host": "127.0.0.1",
      "port": 9001
    },
    "zone_gesture": {
      "address": "/room/gesture/zone",
      "host": "127.0.0.1",
      "port": 9001
    },
    "global_gesture": {
      "address": "/room/gesture/global",
      "host": "127.0.0.1",
      "port": 9001
    }
  }
}
```

- `listen_port`: where the host listens for incoming OSC (e.g., from calibration tools).
- `gesture_host` / `gesture_port`: legacy knobs that prefill all *gesture* routes (voice, zone, global). Voice state keeps its dashboard-friendly default of `127.0.0.1:9000` unless you override `routes.voice_state`.
- `enable_sending`: flip off if you want to run headless without emitting OSC.
- `enable_sensors`: turns the in-app Kinect/webcam capture path on or off.
- `enable_osc_input`: keeps the replay/bridge OSC inlet available for testing without hardware.
- `room_calibration_file`: relative `.json` path under `bin/data/` for named room profiles.
- `sensors`: Kinect depth thresholds, blob sizing, webcam resolution, grid size, and motion smoothing.
- `routes`: per-logical-event overrides if you want, for example, `/voice/{id}/gesture` on a synth box while `/zone/{id}/enter` goes to a lighting desk.

If the file is missing, the host logs a warning and falls back to built-in defaults, so touring rigs can live dangerously.

### Gesture tuning: `gesture_tuning.json`

`of_app/bin/data/gesture_tuning.json` ships with the repo. It holds every knob for the voice + zone + global detectors so you can remix the vocabulary without recompiling. Edit it mid-show and tap `r` in the host window to reload the numbers instantly via `ofxJSON`—no restart, no excuses.

```json
{
  "voice_history_capacity": 60,
  "voice": { "raise_delta_y": 0.18, "shake_min_sign_flips": 4, "hold_cooldown_ms": 1800 },
  "zone": { "sweep_window_ms": 900, "sweep_min_strength": 0.25 },
  "global": { "eruption_high": 0.7, "stillness_motion_threshold": 0.22 }
}
```

Leave any field out to keep the current value—this is a playground, not a contract.

### Room calibration: `room_calibration.json`

`of_app/bin/data/room_calibration.json` carries room-specific sensor settings:
Kinect depth/blob thresholds, camera grid dimensions, camera labels, ignored
zones, and zone labels. Press `r` in the host window to reload calibration and
gesture tuning; press `c` to save the current calibration snapshot.

Ignored zones are forced to zero before gesture detection, telemetry output, and
global motion aggregation, so they are useful for masking doorways, projector
flicker, or dead camera regions.

The Processing dashboard reads the same calibration from
the host's selected `room_calibration_file` when launched from the repo layout,
or from `processing_dashboard/data/room_calibration.json` if you want a
dashboard-local override. Camera labels, zone labels, and ignored-zone markings
then appear in the monitor view.

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

### Preflight check

Run the repo/environment preflight before hardware setup:

```bash
python3 tools/preflight.py --run-tests --build-dashboard
```

It validates config/profile JSON, checks the selected calibration file, reports
whether the local openFrameworks makefile path is usable, checks the Processing
CLI wrapper, looks for stray platform artifacts, and optionally runs the
lightweight detector/replay tests plus a dashboard compile check.

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

- Validate the full host with a real openFrameworks install, Kinect, and webcams.
- Capture real room OSC sessions with `tools/record_osc_fixture.py`, then commit
  the useful fixtures with expectation rows.
- Keep refining Kinect blob tracking against those real-room captures.
- Tune the musical mapping (notes, CCs, timbre indices) in `docs/OSC_SCHEMA.md`.
- Use `docs/CAMERA_CALIBRATION.md` to decide what different room zones mean musically.

MIT licensed; see `LICENSE`.
