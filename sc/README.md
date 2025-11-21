# Crowd Organ SuperCollider patch

This file is the noisy heart of the rig. Boot SuperCollider, load `crowdOrgan.scd`, and let the OSC river drive the synths.

## Boot + load
1. Start SuperCollider and evaluate `s.boot;`.
2. Open `crowdOrgan.scd` and run the whole file (or select-all + `Cmd/Ctrl+Enter`). It waits for the server to boot and then wires up OSCdefs.
3. By default SC listens on the language port (usually **57120**). Point your sender at that port and the host where SC is running.

## OSC addresses it responds to
- `/room/voice/active` — `voiceId, active(0|1)` to create or release synth voices.
- `/room/voice/state` — `voiceId, x, y, z, size, motion, energy`; pan + energy drive amplitude/coloring.
- `/room/voice/note` — `voiceId, note (MIDI), velocity`; triggers envelopes and sets frequency.
- `/room/global/motion` — overall activity meter; stored for gesture logic.
- `/room/camera/zones` — `camId, cols, rows, <zone floats>`; cached for reference.
- `/room/gesture/voice` — `voiceId, type, strength, extra`; mapped by `~gestureHandlers[\voice]` (raise/lower/swipe/shake/burst/hold).
- `/room/gesture/zone` — `camId, type, strength, [zoneIndex]`; sweeps and pulses tweak global boost/color offsets.
- `/room/gesture/global` — `type, strength`; global eruptions/stillness macros.

## Tweaking synth behavior live
- Inspect `~gestureHandlers` to see what each gesture does; you can replace lambdas on the fly (e.g., redefine `~gestureHandlers[\voice][\shake]` and re-run it).
- `~applyVoiceLevels.(vid)` recalculates a voice’s amp/color/tremolo from its state; call it after editing `~voiceState[vid]`.
- Registers and tone: `~registerBright` and `~registerGain` arrays set the brightness and gain per register; edit them to bias the ensemble.
- Global coloration/gain: tweak `~globalAmpBoost` or `~globalColorOffset` and call `~applyAllVoices.()` to hear the impact.
- The `\crowdPipe` SynthDef exposes `freq`, `amp`, `pan`, `bright`, `color`, `trem`, and `burst`—poke them directly via `~crowdVoices[vid].set(...)` for surgical changes.

## Troubleshooting: “why do I hear nothing?”
- Verify your OSC sender is hitting the SC language port (**57120** unless you changed it). `NetAddr.langPort` prints to the post window at load time.
- Confirm the server is actually booted (`s.running` should be `true`) and the SynthDef compiled (no red errors in the post window).
- Make sure at least one `/room/voice/active` message marked a voice active; without that no synths spin up.
- If Processing is also running, double-check port separation: Processing default 9000 (dashboard) vs SuperCollider 57120 (engine).
