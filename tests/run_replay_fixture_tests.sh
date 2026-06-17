#!/usr/bin/env bash
set -euo pipefail

python3 tests/test_record_osc_fixture.py

c++ -std=c++14 \
  -Itests/stubs \
  -Iof_app/src \
  tests/replay_fixture_tests.cpp \
  of_app/src/GestureHistory.cpp \
  of_app/src/VoiceTracker.cpp \
  of_app/src/VoiceGestureDetector.cpp \
  of_app/src/ZoneGestureDetector.cpp \
  of_app/src/GlobalGestureDetector.cpp \
  -o /tmp/crowdorgan_replay_fixture_tests

/tmp/crowdorgan_replay_fixture_tests tests/fixtures/basic_room_replay.oscfixture

tmp_log="/tmp/crowdorgan_basic_room_replay.log"
tmp_fixture="/tmp/crowdorgan_basic_room_replay.oscfixture"
grep -E '^[0-9]+ ' tests/fixtures/basic_room_replay.oscfixture > "$tmp_log"
python3 tools/convert_osc_log_to_fixture.py "$tmp_log" "$tmp_fixture" \
  --expect "voice 1 raise" \
  --expect "zone 0 sweep_lr_top" \
  --expect "global stillness" >/dev/null
/tmp/crowdorgan_replay_fixture_tests "$tmp_fixture"
