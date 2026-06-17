#!/usr/bin/env bash
set -euo pipefail

c++ -std=c++14 \
  -Itests/stubs \
  -Iof_app/src \
  tests/detector_tests.cpp \
  of_app/src/GestureHistory.cpp \
  of_app/src/VoiceTracker.cpp \
  of_app/src/VoiceGestureDetector.cpp \
  of_app/src/ZoneGestureDetector.cpp \
  of_app/src/GlobalGestureDetector.cpp \
  -o /tmp/crowdorgan_detector_tests

/tmp/crowdorgan_detector_tests
