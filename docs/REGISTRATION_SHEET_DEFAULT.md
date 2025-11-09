# Crowd Organ – Default Registration Sheet

This is a **working registration** for Crowd Organ: a first-pass mapping between
sensors (Kinect + cameras) and musical roles (stops, pipes, and couplers).

Treat this as a starting palette, not a prescription. You can fork it per room,
per ensemble, or per piece.

---

## 0. Reference: camera grids & indices

Each camera sends a 4×4 motion grid as:

- Message: `/room/camera/zones camId cols rows zone0 zone1 ... zone15`
- With `cols = 4`, `rows = 4`.

Index layout (row-major):

```text
Cam grid (4×4, indices)

  0   1   2   3
  4   5   6   7
  8   9  10  11
 12  13  14  15
```

You can think of each zone index as a tiny continuous fader in that patch of the image.

---

## 1. Stops overview

Suggested "stops" and what they listen to:

1. **Shimmer Swell** – Camera 0, top row (indices 0–3)  
2. **Floor Drone** – Camera 1, bottom row (indices 12–15)  
3. **Entrance Chimes** – Camera 0, left column (indices 0, 4, 8, 12)  
4. **Turbulence Reed** – Camera 1, right mid (indices 3, 7, 11)  
5. **High Pipes (Choir)** – Kinect blobs with low `y` (heads / raised arms)  
6. **Bass Pipes (Pedal)** – Kinect blobs with high `y` (floor / legs)  
7. **Crowd Swell Pedal** – `/room/global/motion`  
8. **Whisper Dust** – Small blobs with high motion (low `size`, high `motion`)

Below are more detailed notes for each.

---

## 2. Camera-based stops

### Stop 1 – Shimmer Swell (Cam 0 top row)

**Physical region**  
- The upper edge of Camera 0’s FOV: raised hands, upper bodies, or ceiling-level gestures.

**Sensors**  
- `/room/camera/zones 0 4 4 ...`  
- Zones: indices **0, 1, 2, 3**

**Simple control value (conceptual)**

```text
shimmer = avg( zone[0], zone[1], zone[2], zone[3] )
```

**Suggested mapping**  
- Increase:
  - High-frequency reverb send,
  - Shimmer / octave-up pitch-shifter mix,
  - Air band on a global EQ.
- Musically: when people reach up or move high in Cam 0’s frame, the organ acquires a halo.

---

### Stop 2 – Floor Drone (Cam 1 bottom row)

**Physical region**  
- Lower edge of Camera 1’s FOV: floor-level motion, feet, sitting bodies, kids on the ground.

**Sensors**  
- `/room/camera/zones 1 4 4 ...`  
- Zones: indices **12, 13, 14, 15**

**Control value**

```text
floor_drone = avg( zone[12], zone[13], zone[14], zone[15] )
```

**Suggested mapping**  
- Increase:
  - Sub-oscillator / low sine layer,
  - Low-pass filter resonance or slow detuned bass pad.
- Maybe also darken the overall spectrum slightly when this gets high.

---

### Stop 3 – Entrance Chimes (Cam 0 left column)

**Physical region**  
- Left column in Cam 0: often aligned with a doorway or one side of the space.

**Sensors**  
- Zones: **0, 4, 8, 12**

**Control value**

```text
entrance = avg( zone[0], zone[4], zone[8], zone[12] )
```

**Suggested mapping**  
- Trigger or modulate:
  - Sparse, bright chimes or bell events.
  - A probabilistic “welcome fanfare” when motion spikes.

This makes crossing the threshold into the space audibly special.

---

### Stop 4 – Turbulence Reed (Cam 1 right mid)

**Physical region**  
- Right side, mid height in Camera 1: could be a particular corner or wall.

**Sensors**  
- Zones: **3, 7, 11**

**Control value**

```text
turbulence = avg( zone[3], zone[7], zone[11] )
```

**Suggested mapping**  
- Drive:
  - Distortion or wavefolding amount,
  - Noise layer level,
  - FM modulation index.

This is your “angry corner”: when people thrash here, the organ gets teeth.

---

## 3. Kinect-based pipe groupings

Each tracked Kinect blob becomes a **pipe/voice**, sending:

```text
/room/voice/state  voiceId x y z size motion energy
/room/voice/note   voiceId note velocity
```

Here `y` is normalized vertical position (0..1).

### Stop 5 – High Pipes (Choir)

**Condition (conceptual)**

```text
highPipes = voices where y < 0.3
```

**Suggested mapping**

- Restrict these voices to a higher pitch band:
  - Map `y` for these pipes into e.g. MIDI 60–96.
- Emphasize:
  - Less low-end (HPF up),
  - More shimmer/reverb send.

Interpretation: raised arms, jumping, or dancing high in the frame become the bright choir.

---

### Stop 6 – Bass Pipes (Pedal)

**Condition**

```text
bassPipes = voices where y > 0.7
```

**Suggested mapping**

- Restrict pitch range:
  - Map `y` into e.g. MIDI 24–48.
- Add:
  - Slight saturation or sub-osc layer.

Interpretation: people close to the floor (walking, sitting) anchor the harmonic floor.

---

### Stop 7 – Crowd Swell Pedal (Global motion)

**Sensor**

```text
/room/global/motion  globalMotion
```

**Suggested mapping**

- Use `globalMotion` as a **macro**:
  - Scale overall master reverb send,
  - Widen or narrow stereo image,
  - Increase overall density (note rate, grain density).

Low motion = intimate, dry, close.  
High motion = the organ opens up and fills the space.

---

### Stop 8 – Whisper Dust (Small, fast blobs)

**Condition**

Small, jittery blobs can be detected by:

- Low `size` and high `motion` / `energy`.

Conceptually:

```text
whisperSources = voices where size < 0.3 and motion > 0.6
whisperLevel   = sum( whisperSources.motion or energy ) / numSources
```

**Suggested mapping**

- Control:
  - A high, fragile granular or noise layer,
  - Randomized high-note sprinkles.

Kids, quick hand gestures, or transient movements spawn a rustling, whispering cloud.

---

## 4. Putting it together

A rough reference table:

| Stop # | Name             | Primary source                            | Example mapping                               |
|-------:|------------------|-------------------------------------------|-----------------------------------------------|
| 1      | Shimmer Swell    | Cam 0 zones 0–3 (top row)                 | High reverb/shimmer mix                       |
| 2      | Floor Drone      | Cam 1 zones 12–15 (bottom row)            | Sub/bass layer level                          |
| 3      | Entrance Chimes  | Cam 0 zones 0,4,8,12 (left column)        | Sparse chime triggers                         |
| 4      | Turbulence Reed  | Cam 1 zones 3,7,11 (right mid)            | Distortion / noise intensity                  |
| 5      | High Pipes       | Kinect blobs with y < 0.3                 | Higher pitch range, bright timbres            |
| 6      | Bass Pipes       | Kinect blobs with y > 0.7                 | Lower pitch range, warmer / heavier timbres   |
| 7      | Crowd Swell      | `/room/global/motion`                     | Macro for density / reverb / stereo width     |
| 8      | Whisper Dust     | Small, fast blobs (low size, high motion) | Granular / noise “dust” layer                 |

Use this sheet when you:

- Tune SuperCollider mappings in `crowdOrgan.scd`,
- Explain the instrument to collaborators,
- Or adapt Crowd Organ to a specific venue by adjusting which zones are which stops.

Fork this per installation (e.g. `REGISTRATION_sheet_lab-2025.md`) so the organ can remember
how each room likes to sing.
