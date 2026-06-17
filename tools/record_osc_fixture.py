#!/usr/bin/env python3
"""Record live OSC messages into Crowd Organ replay fixture rows.

The recorder intentionally has no third-party dependencies. It implements the
small OSC 1.0 subset the host emits for regression fixtures: int32, float32,
and string arguments, plus recursive bundle unpacking.
"""

from __future__ import annotations

import argparse
import shlex
import socket
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SUPPORTED_ADDRESSES = {
    "/room/voice/state",
    "/room/voice/disconnect",
    "/room/camera/zones",
    "/room/global/motion",
}


@dataclass(frozen=True)
class OscMessage:
    address: str
    args: tuple[Any, ...]


class OscParseError(ValueError):
    pass


def align4(value: int) -> int:
    return (value + 3) & ~3


def read_padded_string(packet: bytes, offset: int) -> tuple[str, int]:
    end = packet.find(b"\0", offset)
    if end < 0:
        raise OscParseError("unterminated OSC string")

    raw = packet[offset:end]
    try:
        value = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise OscParseError("OSC string is not valid UTF-8") from exc

    return value, align4(end + 1)


def read_int32(packet: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(packet):
        raise OscParseError("truncated int32 argument")
    return struct.unpack(">i", packet[offset : offset + 4])[0], offset + 4


def read_float32(packet: bytes, offset: int) -> tuple[float, int]:
    if offset + 4 > len(packet):
        raise OscParseError("truncated float32 argument")
    return struct.unpack(">f", packet[offset : offset + 4])[0], offset + 4


def parse_message(packet: bytes) -> OscMessage:
    address, offset = read_padded_string(packet, 0)
    if not address.startswith("/"):
        raise OscParseError(f"invalid OSC address {address!r}")

    typetag, offset = read_padded_string(packet, offset)
    if not typetag.startswith(","):
        raise OscParseError(f"invalid OSC type tag {typetag!r}")

    args: list[Any] = []
    for tag in typetag[1:]:
        if tag == "i":
            value, offset = read_int32(packet, offset)
            args.append(value)
        elif tag == "f":
            value, offset = read_float32(packet, offset)
            args.append(value)
        elif tag == "s":
            value, offset = read_padded_string(packet, offset)
            args.append(value)
        elif tag == "T":
            args.append(True)
        elif tag == "F":
            args.append(False)
        else:
            raise OscParseError(f"unsupported OSC type tag {tag!r}")

    return OscMessage(address=address, args=tuple(args))


def parse_packet(packet: bytes) -> list[OscMessage]:
    if packet.startswith(b"#bundle\0"):
        if len(packet) < 16:
            raise OscParseError("truncated OSC bundle header")

        messages: list[OscMessage] = []
        offset = 16  # '#bundle' string plus 8-byte timetag.
        while offset < len(packet):
            element_size, offset = read_int32(packet, offset)
            if element_size < 0:
                raise OscParseError("negative OSC bundle element size")
            end = offset + element_size
            if end > len(packet):
                raise OscParseError("truncated OSC bundle element")
            messages.extend(parse_packet(packet[offset:end]))
            offset = end
        return messages

    return [parse_message(packet)]


def format_arg(value: Any) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return f"{value:.6g}"
    return shlex.quote(str(value))


def format_fixture_row(timestamp_ms: int, message: OscMessage) -> str:
    return " ".join([str(timestamp_ms), message.address, *(format_arg(arg) for arg in message.args)])


def parse_expectation(value: str) -> str:
    fields = value.split()
    if len(fields) < 2:
        raise ValueError("--expect must look like 'global stillness', 'voice 1 raise', or 'zone 0 sweep_lr_top'")
    scope = fields[0]
    if scope in {"voice", "zone"}:
        if len(fields) != 3:
            raise ValueError("--expect voice/zone must include id and gesture type")
        int(fields[1])
        return f"expect {scope} {fields[1]} {fields[2]}"
    if scope == "global":
        if len(fields) != 2:
            raise ValueError("--expect global must include only gesture type")
        return f"expect global {fields[1]}"
    raise ValueError(f"--expect has unknown scope {scope}")


def write_header(output: Path, host: str, port: int, append: bool, expectations: list[str]) -> None:
    if append and output.exists() and output.stat().st_size > 0:
        return

    with output.open("a", encoding="utf-8") as handle:
        handle.write("# Crowd Organ replay fixture v1\n")
        handle.write(f"# Recorded from OSC {host}:{port}\n")
        handle.write("# Add expectation rows before committing a fixture.\n\n")
        if expectations:
            for expectation in expectations:
                handle.write(expectation + "\n")
            handle.write("\n")


def record(args: argparse.Namespace) -> int:
    allowed = set(SUPPORTED_ADDRESSES)
    if args.address:
        allowed.update(args.address)

    expectations = [parse_expectation(value) for value in args.expect]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.output, args.host, args.port, args.append, expectations)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.settimeout(0.25)

    start_time: float | None = None
    recorded = 0
    deadline = time.monotonic() + args.duration if args.duration else None

    if not args.quiet:
        print(f"recording OSC on {args.host}:{args.port} -> {args.output}", file=sys.stderr)

    try:
        with args.output.open("a", encoding="utf-8") as handle:
            while True:
                now = time.monotonic()
                if deadline is not None and now >= deadline:
                    break
                if args.max_messages is not None and recorded >= args.max_messages:
                    break

                try:
                    packet, _addr = sock.recvfrom(args.max_packet_size)
                except socket.timeout:
                    continue

                try:
                    messages = parse_packet(packet)
                except OscParseError as exc:
                    if not args.quiet:
                        print(f"skipping malformed OSC packet: {exc}", file=sys.stderr)
                    continue

                for message in messages:
                    if message.address not in allowed:
                        continue
                    if start_time is None:
                        start_time = now
                    timestamp_ms = int(round((now - start_time) * 1000.0))
                    handle.write(format_fixture_row(timestamp_ms, message) + "\n")
                    recorded += 1
                    if args.flush_every > 0 and recorded % args.flush_every == 0:
                        handle.flush()
                    if args.max_messages is not None and recorded >= args.max_messages:
                        break
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()

    if not args.quiet:
        print(f"recorded {recorded} supported OSC messages", file=sys.stderr)
    return 0 if recorded > 0 or args.allow_empty else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Record supported live OSC messages into a replay fixture.")
    parser.add_argument("output", type=Path, help="Output .oscfixture path")
    parser.add_argument("--host", default="0.0.0.0", help="UDP bind host, default: 0.0.0.0")
    parser.add_argument("--port", type=int, default=9000, help="UDP bind port, default: 9000")
    parser.add_argument("--append", action="store_true", help="Append to an existing fixture")
    parser.add_argument("--duration", type=float, help="Stop after this many seconds")
    parser.add_argument("--max-messages", type=int, help="Stop after this many recorded supported messages")
    parser.add_argument("--max-packet-size", type=int, default=65535, help="Maximum UDP packet size to read")
    parser.add_argument("--flush-every", type=int, default=1, help="Flush every N recorded messages; 0 disables explicit flush")
    parser.add_argument("--allow-empty", action="store_true", help="Exit successfully even if no supported messages were recorded")
    parser.add_argument("--quiet", action="store_true", help="Suppress progress output")
    parser.add_argument(
        "--expect",
        action="append",
        default=[],
        help="Expected gesture, e.g. 'voice 1 raise', 'zone 0 sweep_lr_top', 'global stillness'",
    )
    parser.add_argument(
        "--address",
        action="append",
        default=[],
        help="Additional OSC address to record. Repeat for multiple addresses.",
    )
    return record(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
