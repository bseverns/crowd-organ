# Crowd Organ SuperCollider patch

This is the sound engine notebook: run it, tweak it, and let OSC gestures sculpt the pipes. It’s half lab journal, half gig checklist.

## Evaluate it
1. Boot SuperCollider and run `s.boot;` so the audio server is live.
2. Open `sc/crowdOrgan.scd` and evaluate the whole file (select all + `Cmd/Ctrl+Enter`). It waits for the server, builds SynthDefs, and installs OSCdefs.
3. By default it listens on the SC language port (**57120** unless you’ve changed your SC prefs). Aim your sender at that port on the machine running SuperCollider.

## OSC addresses this file handles
- `/room/voice/active` — `voiceId, active(0|1)` spins up or releases a `\crowdPipe` synth per voice.
- `/room/voice/state` — `voiceId, x, y, z, size, motion, energy`; updates pan and energy, then re-applies level math.
- `/room/voice/note` — `voiceId, note (MIDI), velocity`; sets frequency, base amp, and opens the envelope.
- `/room/global/motion` — caches an overall activity meter that other gestures might reference.
- `/room/camera/zones` — `camId, cols, rows, <zone floats>` stored for gesture context.
- `/room/gesture/voice` — voice-specific gestures (`raise`, `lower`, `swipe_left/right`, `shake`, `burst`, `hold`) routed through `~gestureHandlers[\voice]`.
- `/room/gesture/zone` — zone pulses/sweeps (`pulse_zone`, `sweep_*`) that bump global gain/color.
- `/room/gesture/global` — global macros (`eruption`, `stillness`).

## Default ports and routing expectations
- SuperCollider language port: **57120** (printed via `NetAddr.langPort` when the file loads).
- Processing dashboard (if running): **9000**. Keep senders targeting the right host+port to avoid cross-talk.
- If you change the SC lang port in your environment, update your sender config; the code reads whatever `NetAddr.langPort` is.

## Quick tweaks while jamming
- **Per-voice tone:** edit `~registerBright` and `~registerGain` arrays, then call `~applyAllVoices.()` to hear the new bias.
- **Per-voice energy/amp:** adjust a voice’s `~voiceState[vid][\baseAmp]` or `\energy` and run `~applyVoiceLevels.(vid)`. You can also poke `~crowdVoices[vid].set(\amp, ...)` for surgical moves.
- **Gesture mapping:** replace lambdas in `~gestureHandlers` (e.g., reassign `~gestureHandlers[\voice][\shake]`) and re-evaluate the line; new messages use your updated behavior.
- **Global vibe:** tweak `~globalAmpBoost` or `~globalColorOffset` and call `~applyAllVoices.()` to push/pull the whole ensemble.

## Troubleshooting: “no sound?” verify these steps
- Is the SC server running? (`s.running` should be `true` and the post window should show the SynthDef compiled without red errors.)
- Are OSC messages hitting **57120** on the SC host? Test with another OSC tool or print `NetAddr.langPort` to confirm the port.
- Did you activate any voices? Send `/room/voice/active, <id>, 1` or `/room/voice/note` so a synth has something to play.
- If the Processing dashboard is also live, ensure you’re not accidentally beaming audio-control messages to its **9000** listener.
