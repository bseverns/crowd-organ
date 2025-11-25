# Crowd Organ Processing Dashboard

Part periscope, part stage manager: this Processing sketch shows you what the gesture tracker is flinging over OSC while you dial in mappings.

## Ingredients (don’t skip the groceries)
- **Processing 4.x** (3.x also works, but we’re living in the future).
- **oscP5** + **netP5** dropped into your Processing `libraries/` folder.
- Optional caffeine; it helps when you’re juggling ports.

## Run the sketch
1. Launch Processing and open `processing_dashboard/CrowdOrganDashboard.pde`.
2. Hit **Run**. The window (about 920×720) spins up and binds an OSC listener via `new OscP5(this, 9000);`.
3. To aim at a different port, change that constructor argument, save, and re-run so the listener rebinds.
4. Point your upstream tracker at the dashboard machine’s IP on that same port (default **9000**).
5. Keep the OSC shapes handy in [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md) so you know which payloads the visuals expect.

## Where to set the listening port
- Open `CrowdOrganDashboard.pde` and find the `new OscP5(this, 9000);` call near the top.
- Swap `9000` for whatever your sender uses, save, and re-run. That’s the one knob that matters here.

## Overlays, decoded
- **Header/footer**: labels the sketch and reminds you of keyboard toggles.
- **Global motion meter (top-right)**: cyan fill tracks `/room/global/motion`; last `/room/gesture/global` shows as a fading caption.
- **Voice bubbles (center)**: one circle per active voice from `/room/voice/active` + `/room/voice/state`; note labels ride on `/room/voice/note`; gesture rings pulse on `/room/gesture/voice` with radius scaled by strength.
- **Camera grids (bottom)**: per-camera heatmaps from `/room/camera/zones`; `/room/gesture/zone` outlines flash cells; sweeps paint translucent strokes across the viewport.
- **Gesture log (right)**: rolling list of the latest `/room/gesture/*` events with type/scope/strength.
- **Keyboard toggles**: `v` toggles voice gesture rings, `z` toggles zone flashes, `g` toggles global gesture captions.

## No visuals? Check these ports/packets
- Verify the dashboard is actually listening: `nc -lu 9000` (or your custom port) should print OSC bytes when the tracker is live.
- Make sure the sender targets the dashboard host + port (default **9000**), not the SuperCollider engine.
- Confirm oscP5 + netP5 are installed; missing jars = silent Processing console.
- Cross-check your OSC addresses against [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md); mismatched paths won’t register.
