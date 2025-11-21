# Crowd Organ Processing Dashboard

This Processing sketch is the noisy-but-kind OSC periscope for the crowd-organ system. Think of it as a stagehand who yells back what the gesture tracker is doing while you dial in mappings.

## Required ingredients
- **Processing 3.x/4.x** installed.
- **oscP5** and **netP5** dropped into your Processing `libraries/` folder (the sketch imports both at the top; missing jars halt compilation).

## Open it, run it, aim it
1. Launch Processing and open `processing_dashboard/CrowdOrganDashboard.pde`.
2. Hit **Run**. The window (920×720) spins up and immediately binds an OSC listener via `new OscP5(this, 9000);`.
3. If your upstream sender is using a different port, change that constructor argument, save, and re-run so the listener rebinds.
4. Sender side: point your tracker at the Processing machine’s IP on the chosen port (default **9000**).

## What the overlays mean
- **Title + footer:** reminds you what you’re looking at and which toggles exist.
- **Global motion meter (top-right):** cyan fill for `/room/global/motion`, plus a fading caption for the last `/room/gesture/global` hit.
- **Voice bubbles (center):** one circle per active `/room/voice/active`/`/room/voice/state` pair. Labels show voice index + note; gesture rings flare on `/room/gesture/voice` with strength-scaled radius.
- **Camera grids (bottom):** per-camera heatmaps from `/room/camera/zones`. `/room/gesture/zone` pulses a cell outline; sweeps draw translucent cross-screen strokes.
- **Gesture log (right):** rolling feed of the latest `/room/gesture/*` events with type, scope, and strength.
- **Keyboard toggles:** `v` hides/shows voice gesture rings, `z` hides/shows zone flashes, `g` hides/shows global gesture labels.

## Port cheat sheet
- Dashboard listens on **9000** unless you edit the `OscP5` constructor.
- Gesture tracker / sender must target that same port on the dashboard host.
- SuperCollider engine (in `sc/`) typically sits on **57120**; keep it separate so packets don’t collide.

## Troubleshooting: “no visuals?” check these ports
- Confirm packets are landing: `nc -lu 9000` in a terminal on the Processing machine should print OSC bytes when the tracker is live.
- If nothing arrives, verify the sender is aimed at the right IP + port (9000 by default).
- If the window is up but blank, double-check oscP5 + netP5 are installed and the OSC addresses match (`/room/voice/state`, `/room/voice/note`, `/room/global/motion`, `/room/camera/zones`, `/room/gesture/*`).
