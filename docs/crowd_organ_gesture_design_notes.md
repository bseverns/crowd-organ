# Crowd Organ – Gesture Design Notes

This document describes a **first pass** at gesture recognition for Crowd Organ:
how to name a few common movement patterns and how to surface them over OSC.

The aim is not perfect classification, but a musical vocabulary of **recognizable
phrases** that the organ can respond to.

---

## 1. Scope

We distinguish three layers:

1. **Per-voice gestures** – what an individual tracked blob (person) is doing.
2. **Zone gestures** – patterns in the 4×4 camera motion grids.
3. **Global gestures** – collective dynamics of the whole crowd.

All of these ride on top of the existing signals:

- `/room/voice/state` for per-voice motion/energy,
- `/room/camera/zones` for per-camera motion fields,
- `/room/global/motion` for overall activity.

Gestures are emitted as **discrete events** (with cooldowns), not continuous controls.

---

## 2. Per-voice gestures (Kinect blobs)

Each Kinect blob corresponds to a `voiceId` with:

```text
/room/voice/state  voiceId x y z size motion energy
```

Internally, CrowdOrganHost can keep a **short history** (e.g. 30–45 frames) per
voice with:

- position `pos(t)`,
- velocity `vel(t)`,
- `motion(t)`.

All the gestures below are simple tests over that short history.

### 2.1. Raise / Lower

**Intuition**  
A person moves their body (or hands) noticeably **up** or **down** in the frame.

**Detection sketch**

- Look at Δy over the last N frames (e.g. 0.5–1.0 s).
- If Δy is significantly negative (moving up in normalized space) with relatively
  small x jitter → `"raise"`.
- If Δy is significantly positive → `"lower"`.

**OSC**

```text
/room/gesture/voice  voiceId  "raise"  strength  endpointY
/room/gesture/voice  voiceId  "lower"  strength  endpointY
```

- `strength` – normalized magnitude of the vertical move.
- `endpointY` – y-position at the end of the gesture (useful for mapping to
  specific registers).

**Use cases**

- Raising can open brighter stops or higher registers for that voice.
- Lowering can close them or move the pipe into mellower timbres.

---

### 2.2. Swipe Left / Right

**Intuition**  
A clear, mostly horizontal sweep.

**Detection sketch**

- Compute Δx over N frames.
- If |Δx| is large and |Δx| » |Δy|, treat as a swipe.
- Sign of Δx chooses `"swipe_left"` vs `"swipe_right"`.

**OSC**

```text
/room/gesture/voice  voiceId  "swipe_left"   strength  0.0
/room/gesture/voice  voiceId  "swipe_right"  strength  0.0
```

**Use cases**

- Switch registration sets or “manuals” for that voice.
- Flip which stop families are active (e.g. from flutes to reeds).

---

### 2.3. Shake

**Intuition**  
Small, jittery movement in place (wobbling hands, shoulders).

**Detection sketch**

- Over a short window, position stays in a small radius.
- Velocity sign flips frequently in x and/or y.
- Motion energy is nonzero but net displacement is small.

**OSC**

```text
/room/gesture/voice  voiceId  "shake"  strength  0.0
```

**Use cases**

- Engage tremolo, granular “flutter”, or heavy vibrato for that pipe.

---

### 2.4. Jump / Burst

**Intuition**  
A sudden, strong movement spike, often upward.

**Detection sketch**

- instantaneous speed (‖vel‖) exceeds a high threshold for a very short time,
- possibly with Δy > 0 over that tiny window.

**OSC**

```text
/room/gesture/voice  voiceId  "burst"  strength  0.0
```

**Use cases**

- Trigger short flurries, grace-note clusters or octave jumps on that voice.

---

### 2.5. Hold / Bow

**Intuition**  
The participant goes comparatively still while remaining present.

**Detection sketch**

- motion stays below a low threshold for > T frames,
- `energy` and presence indicate the blob hasn’t disappeared.

**OSC**

```text
/room/gesture/voice  voiceId  "hold"  strength  duration
```

- `duration` – how long the stillness has lasted (normalized).

**Use cases**

- Increase sustain or reverb, soften attacks, expose harmonics when people hold
  still, as if they are “bowing” a long pipe.

---

## 3. Zone gestures (camera motion fields)

Each camera sends a 4×4 grid (see `REGISTRATION_SHEET_DEFAULT.md`):

```text
/room/camera/zones  camId  4  4  zone0 ... zone15
```

We can use **rows, columns, and corners** to define simple spatial gestures.

### 3.1. Sweep (left→right, right→left, top→bottom, bottom→top)

**Intuition**  
Motion travels across a row or column in a specific direction.

**Detection sketch**

- Focus on a given row or column (e.g. Cam 0 top row: zones 0–3).
- Over a short time window, track which index has the max activation.
- If the index of the max consistently increases → `"sweep_lr_*"`.
- If it decreases → `"sweep_rl_*"`.

**OSC**

```text
/room/gesture/zone  camId  "sweep_lr_top"  strength
/room/gesture/zone  camId  "sweep_rl_top"  strength
/room/gesture/zone  camId  "sweep_tb_left" strength
...
```

**Use cases**

- Tie sweeps to slow, global morphs:
  - moving through harmonic presets,
  - shifting filter banks,
  - crossfading organ “manuals”.

---

### 3.2. Pulses

**Intuition**  
A zone that repeatedly brightens and dims (local rhythmic waving).

**Detection sketch**

- For a single zone, track its motion over time.
- Detect local maxima above threshold at semi-regular intervals.

**OSC**

```text
/room/gesture/zone  camId  "pulse_zone"  strength  // strength linked to pulse amplitude
```

(If you need the zone index, you can either include it in the type name or add
an int argument.)

**Use cases**

- Drive local LFO depths or rhythmic gating from specific corners.

---

## 4. Global gestures (crowd-level)

Global gestures are built from:

- `/room/global/motion  globalMotion`
- Aggregate voice counts and activity.

### 4.1. Eruption

**Intuition**  
The room suddenly comes alive.

**Detection sketch**

- globalMotion jumps from low (< ~0.2) to high (> ~0.7) in a short window.

**OSC**

```text
/room/gesture/global  "eruption"  strength
```

**Use cases**

- Briefly open “tutti”:
  - activate more stops,
  - raise reverb,
  - increase note density.

---

### 4.2. Stillness

**Intuition**  
Many people present, but movement is collectively calm.

**Detection sketch**

- voices active ≥ some count,
- globalMotion remains below a low value for a longer window.

**OSC**

```text
/room/gesture/global  "stillness"  strength
```

**Use cases**

- Thin out texture to sustained tones or a few partials.
- Lower distortion/density, expose quieter mechanical or room-like sounds.

---

## 5. OSC summary

### 5.1. Per-voice gestures

```text
/room/gesture/voice  i  s        f         f
                      |  |        |         |
                   voiceId  type  strength  extra

type (examples):
  "raise", "lower", "swipe_left", "swipe_right", "shake", "burst", "hold"
```

### 5.2. Zone gestures

```text
/room/gesture/zone   i  s              f
                     |  |              |
                   camId  type        strength

type (examples):
  "sweep_lr_top", "sweep_rl_top",
  "sweep_tb_left", "pulse_zone", ...
```

### 5.3. Global gestures

```text
/room/gesture/global  s           f
                      |           |
                      type       strength

type (examples):
  "eruption", "stillness"
```

All gesture messages are **stateless events** from the synthesizer’s point of
view: they are hooks for changing registrations, switching scenes, or triggering
short musical responses.

---

## 6. Implementation notes

- Start with **just a few gestures** (e.g. `raise`, `swipe_left/right`,
  `eruption`) and log them before mapping them.
- Prefer **simple, explainable rules** over opaque ML so you and collaborators
  can tune thresholds by feel.
- Add a **cooldown** per gesture/voice so events don’t fire every frame.
- Use gestures for **structure and registration** and keep continuous motion
  (positions, energy, zone values) for timbre and dynamics.

As the instrument settles, you can fork this document for specific pieces or
venues (e.g. `GESTURES_lab-2025.md`) and let each Crowd Organ remember which
phrases its crowd taught it.

