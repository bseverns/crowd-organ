# Replay Fixtures

Replay fixtures are small, line-oriented OSC transcripts used to regression-test
gesture detection without Kinect/webcam hardware or a running openFrameworks app.

Run them with:

```bash
bash tests/run_replay_fixture_tests.sh
```

Record a live host/dashboard OSC stream into a fixture-shaped file with:

```bash
python3 tools/record_osc_fixture.py tests/fixtures/my_room.oscfixture --port 9000 \
  --expect "voice 1 raise" \
  --expect "global stillness"
```

Convert a simple timestamped OSC log into a fixture with:

```bash
python3 tools/convert_osc_log_to_fixture.py capture.log tests/fixtures/my_room.oscfixture \
  --expect "voice 1 raise" \
  --expect "zone 0 sweep_lr_top"
```

Accepted converter input rows:

```text
1234 /room/voice/state 1 0.0 0.5 0.2 0.2 0.05 0.2
1234,/room/voice/state,1,0.0,0.5,0.2,0.2,0.05,0.2
```

The live recorder listens on UDP, records supported fixture input addresses, and
writes relative millisecond timestamps. Stop it with `Ctrl-C`, or use
`--duration 30` / `--max-messages 500` for bounded captures. It understands
plain OSC messages and bundles containing `int32`, `float32`, and string
arguments.

## Format

Event rows use canonical OSC schema order:

```text
<time_ms> <osc_address> <args...>
```

Supported event addresses:

```text
/room/voice/state voiceId x y z size motion energy
/room/voice/disconnect voiceId
/room/camera/zones camId cols rows zone0 zone1 ...
/room/global/motion globalMotion
```

Expectation rows declare gestures that must be observed during replay:

```text
expect voice <voiceId> <gestureType>
expect zone <camId> <gestureType>
expect global <gestureType>
```

Comments start with `#`. Event rows are stable-sorted by timestamp before replay,
so fixtures can group expectations at the top and keep related messages readable.

## Current Coverage

`tests/fixtures/basic_room_replay.oscfixture` verifies:

- a per-voice `raise`,
- a non-square `/room/camera/zones camId cols rows ...` sweep,
- a global `stillness` gesture.

The replay harness intentionally uses the same detector classes as the host.
It does not test openFrameworks capture, OSC sockets, rendering, or hardware
setup; those still need the full app build and live hardware.
