# Crowd Organ SuperCollider patch

The sound engine is a lab journal disguised as a gig checklist. Fire it up, stream OSC into it, and tweak live like the punk-rock professor you are.

## Evaluate `crowdOrgan.scd`
1. Boot SuperCollider and run `s.boot;` so the audio server is alive.
2. Open `sc/crowdOrgan.scd`, select all, and hit `Cmd/Ctrl+Enter` (or evaluate line by line if you like the suspense).
3. The script waits for the server, builds SynthDefs, and installs OSCdefs bound to the current SC language port.
4. Keep [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md) nearby; it documents the OSC payloads this patch responds to.

## OSC addresses + ports it listens to
- **Port**: whatever `NetAddr.langPort` reports (default **57120** in a stock SC install). Aim your sender there; adjust the host machine IP in your tracker if SC lives elsewhere.
- `/room/voice/active` — `voiceId, active(0|1)` spins up or releases a `\crowdPipe` synth per voice.
- `/room/voice/state` — `voiceId, x, y, z, size, motion, energy`; updates panning and energy math.
- `/room/voice/note` — `voiceId, note (MIDI), velocity`; sets frequency, base amp, opens the envelope, and pins velocity for downstream gesture math.
- `/room/global/motion` — caches an overall activity meter.
- `/room/camera/zones` — `camId, cols, rows, <zone floats>` stored for gesture context.
- `/room/gesture/voice` — voice gestures (`raise`, `lower`, `swipe_left/right`, `shake`, `burst`, `hold`, plus the new `pause` + `sweep` articulations) dispatched through `~gestureHandlers[\voice]`.
- `/room/gesture/zone` — zone gestures (`pulse_zone`, `sweep_*`) that bump global gain/color.
- `/room/gesture/global` — global macros (`eruption`, `stillness`).

### What changed in this rev (teach the crew)
- `\crowdPipe` now eats **velocity**, **articulation mode**, **morph**, and **handoff** controls. Velocity scales the whole patch, articulation toggles envelope flavors (normal / pause-hold duck / sweep LFO), morph tugs timbre toward reed or saw + tilts the filter, and handoff lengthens release tails for voice swaps.
- Gesture **strength is treated as velocity**: every voice gesture calls `~registerVelocity`, so scrubbing hard adds gain/tone pressure even before a note-on packet arrives. `/room/voice/note` also logs its incoming velocity for the same lane.
- Added **articulation gestures**: send `pause` to duck and chill a voice, `sweep` to ride the slow envelope, or keep using `hold` (now a pause-flavored envelope) to soften + darken a voice. `shake` also flags a sweep mode for extra shimmer.
- **Motion morph targets**: per-voice `state[\morph]` blends energy + `/room/global/motion` so busy rooms glide brighter/tremmier while calmer rooms stay reed-like. Swipe gestures nudge morph toward more harmonic grind.
- **Voice handoff**: when `/room/voice/active` drops, the synth tails out with a longer release and can be **adopted** by the next activation instead of hard-cutting. No more cliff-edge silences when tracking flips between IDs, and the tail only gets freed if nobody claims it within ~3s.

## Quick knobs while jamming
- **Per-voice tone**: edit `~registerBright` / `~registerGain`, then call `~applyAllVoices.()`.
- **Per-voice energy/amp**: nudge `~voiceState[vid][\baseAmp]` or `\energy`, then re-run `~applyVoiceLevels.(vid)`; direct `~crowdVoices[vid].set(\amp, ...)` also works.
- **Gesture mapping**: replace lambdas inside `~gestureHandlers` (e.g., `~gestureHandlers[\voice][\shake] = { |vid, str| ... };`) and re-evaluate.
- **Global gain**: push/pull `~globalAmpBoost` (and follow with `~applyAllVoices.()` to normalize the crew).

## No sound? Verify server + port
- Is the SC server running? `s.running` should be `true` and the post window should show SynthDefs compiling cleanly.
- Are OSC messages hitting the SC language port (default **57120**)? Send a test packet or print `NetAddr.langPort`.
- Did you activate any voices? `/room/voice/active, <id>, 1` or `/room/voice/note` wakes up a synth.
- Is the Processing dashboard also running? Make sure audio-control packets are aimed at the SC port, not the dashboard’s **9000** listener. If you prefer splitting gesture vs. state feeds, double-check the ports in `gesture_settings.json` against the address list above.
- If messages look off, compare against [`docs/OSC_SCHEMA.md`](../docs/OSC_SCHEMA.md); wrong shapes = ignored events.
