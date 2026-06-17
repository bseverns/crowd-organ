# Crowd Organ Processing Dashboard

Part periscope, part stage manager: this Processing sketch shows you what the gesture tracker is flinging over OSC while you dial in mappings.

## Ingredients (don’t skip the groceries)
- **Processing 4.x** (3.x will limp along, but 4.x is the happy path).
- **oscP5** + **netP5** sitting in your Processing `libraries/` folder (restart Processing after dropping them in).
- Optional caffeine; it helps when you’re juggling ports.

## Run the sketch
1. Launch Processing and open `processing_dashboard/CrowdOrganDashboard.pde`.
2. Hit **Run**. The window (about 920×720) spins up and binds an OSC listener via `new OscP5(this, 9000);`.
3. To aim at a different port, change that constructor argument, save, and re-run so the listener rebinds.
4. Point your upstream tracker at the dashboard machine’s IP on that same port (default **9000**). You can also mirror gesture bursts to the secondary gesture feed on **9001** if you prefer—just align the port here to match.
5. Keep the OSC shapes handy in [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md) so you know which payloads the visuals expect.

The dashboard will load room labels from `processing_dashboard/data/room_calibration.json`
when present. From this repo layout it also reads the host's selected
`room_calibration_file` from `../of_app/bin/data/gesture_settings.json`, then
falls back to `../of_app/bin/data/room_calibration.json`, so camera labels,
named zones, and ignored-zone hatching show up without duplicating the config.

## Where to set the listening port
- Crack open `CrowdOrganDashboard.pde` and find the `new OscP5(this, 9000);` call near the top.
- Swap `9000` for whatever your sender uses, save, and re-run. That’s the one knob that matters here; everything else adapts to whatever arrives.

## Overlays, decoded
- **Header/footer**: labels the sketch and reminds you of keyboard toggles.
- **Global motion meter (top-right)**: cyan fill tracks `/room/global/motion`; the most recent `/room/gesture/global` shows as a fading caption.
- **Voice bubbles (center)**: one circle per active voice from `/room/voice/active` + `/room/voice/state`; note labels ride on `/room/voice/note`; gesture rings pulse on `/room/gesture/voice` with radius scaled by strength.
- **Camera grids (bottom)**: per-camera heatmaps from `/room/camera/zones`; calibration labels annotate known cameras/zones, ignored zones are crossed out, `/room/gesture/zone` outlines flash cells, and sweeps paint translucent strokes across the viewport.
- **Gesture log (right)**: rolling list of the latest `/room/gesture/*` events with type/scope/strength.
- **Keyboard toggles**: `v` toggles voice gesture rings, `z` toggles zone flashes, `g` toggles global gesture captions.
- **Host controls**: `r` sends `/room/config/reload`, `m` toggles `/room/config/sending`, `s` toggles `/room/config/sensors`, and `x` sends `/room/global/reset` to `127.0.0.1:9001`.

## No visuals? Check these ports/packets
- Verify the dashboard is actually listening: `nc -lu 9000` (or your custom port) should print OSC bytes when the tracker is live.
- Make sure the sender targets the dashboard host + port (default **9000**), not the SuperCollider engine or the raw gesture stream on **9001**.
- Confirm oscP5 + netP5 are installed; missing jars = silent Processing console.
- Cross-check your OSC addresses against [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md); mismatched paths won’t register.
