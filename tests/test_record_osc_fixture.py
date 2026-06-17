#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "record_osc_fixture.py"
SPEC = importlib.util.spec_from_file_location("record_osc_fixture", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
record_osc_fixture = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = record_osc_fixture
SPEC.loader.exec_module(record_osc_fixture)


def osc_string(value: str) -> bytes:
    raw = value.encode("utf-8") + b"\0"
    return raw + (b"\0" * ((4 - len(raw) % 4) % 4))


def osc_message(address: str, typetag: str, *payloads: bytes) -> bytes:
    return osc_string(address) + osc_string("," + typetag) + b"".join(payloads)


def test_parse_supported_message() -> None:
    packet = osc_message(
        "/room/camera/zones",
        "iiiffff",
        struct.pack(">i", 2),
        struct.pack(">i", 2),
        struct.pack(">i", 2),
        struct.pack(">f", 0.0),
        struct.pack(">f", 0.25),
        struct.pack(">f", 0.5),
        struct.pack(">f", 1.0),
    )

    messages = record_osc_fixture.parse_packet(packet)
    assert len(messages) == 1
    assert messages[0].address == "/room/camera/zones"
    assert messages[0].args[:3] == (2, 2, 2)
    assert messages[0].args[3:] == (0.0, 0.25, 0.5, 1.0)
    assert record_osc_fixture.format_fixture_row(123, messages[0]) == "123 /room/camera/zones 2 2 2 0 0.25 0.5 1"


def test_parse_bundle() -> None:
    first = osc_message("/room/global/motion", "f", struct.pack(">f", 0.75))
    second = osc_message("/room/voice/disconnect", "i", struct.pack(">i", 4))
    packet = b"#bundle\0" + (b"\0" * 8)
    packet += struct.pack(">i", len(first)) + first
    packet += struct.pack(">i", len(second)) + second

    messages = record_osc_fixture.parse_packet(packet)
    assert [message.address for message in messages] == ["/room/global/motion", "/room/voice/disconnect"]
    assert messages[0].args == (0.75,)
    assert messages[1].args == (4,)


def test_parse_expectation() -> None:
    assert record_osc_fixture.parse_expectation("voice 2 raise") == "expect voice 2 raise"
    assert record_osc_fixture.parse_expectation("zone 0 sweep_lr_top") == "expect zone 0 sweep_lr_top"
    assert record_osc_fixture.parse_expectation("global stillness") == "expect global stillness"


def main() -> int:
    test_parse_supported_message()
    test_parse_bundle()
    test_parse_expectation()
    print("osc recorder tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
