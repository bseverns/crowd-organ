# Crowd Organ Processing Dashboard

This Processing sketch is the scrappy OSC spyglass for the crowd-organ system. It drinks OSC, paints telemetry, and helps you sanity-check what the gesture tracker is actually emitting.

## Dependencies
- **Processing** (tested with 3.x/4.x).
- **oscP5** + **netP5** libraries dropped into your Processing `libraries/` folder. The sketch imports both at the top, so missing them will break compilation.

## Open + Run
1. Launch Processing and open `CrowdOrganDashboard.pde` from `processing_dashboard/`.
2. Hit **Run**. The window comes up at 920×720 and immediately starts listening for OSC.
3. By default the sketch binds to **port 9000**. If your upstream tracker uses a different port, edit the `new OscP5(this, 9000);` line near the top of the sketch before running.

## What you are seeing
- **Title + footer**: The top bar reminds you this is the OSC monitor; the footer calls out keyboard toggles.
- **Global motion meter** (top-right): A cyan bar showing `/room/global/motion`. Recent global gestures tint a caption underneath.
- **Voice bubbles** (center): Up to eight active voices drawn as circles on a stage map. Labels include voice index, note, and velocity; halos appear for recent `/room/gesture/voice` events and scale with gesture strength.
- **Camera grids** (bottom): One mini-grid per camera, filled with the latest `/room/camera/zones` values. Pulsing outlines mark `/room/gesture/zone` hits; sweeps paint a translucent sweep overlay.
- **Gesture log** (right): A rolling list of the last ~18 gestures across scopes (voice/zone/global) with strength and contextual notes (e.g., y-position for raises).
- **Keyboard toggles**: `v` hides/shows voice gesture halos, `z` hides/shows zone flashes, `g` hides/shows global gesture labels.

## Re-pointing the OSC listener
- Change the constructor `new OscP5(this, 9000);` to match whatever port your sender is blasting at.
- If you hot-swap ports mid-session, re-run the sketch; Processing does not hot-rebind.

## Troubleshooting: “why is everything quiet?”
- Confirm your tracker is sending to the Processing machine’s IP on **port 9000** (or whatever you set). A quick `nc -lu 9000` in another terminal can confirm packets are arriving.
- Make sure oscP5 + netP5 are actually installed; missing jars give a red stack trace before the window appears.
- If the UI is up but blank, verify the OSC addresses match (`/room/voice/state`, `/room/voice/note`, `/room/global/motion`, `/room/camera/zones`, `/room/gesture/*`).
