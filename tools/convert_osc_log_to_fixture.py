#!/usr/bin/env python3
"""Convert simple OSC logs into Crowd Organ replay fixtures.

Accepted input rows:

    1234 /room/voice/state 1 0.0 0.5 0.2 0.2 0.05 0.2
    1234,/room/voice/state,1,0.0,0.5,0.2,0.2,0.05,0.2

Comment and blank lines are ignored. The converter normalizes whitespace,
sorts rows by timestamp, and writes a .oscfixture file.
"""

from __future__ import annotations

import argparse
import csv
import shlex
import sys
from dataclasses import dataclass
from pathlib import Path


SUPPORTED_ADDRESSES = {
    "/room/voice/state",
    "/room/voice/disconnect",
    "/room/camera/zones",
    "/room/global/motion",
}


@dataclass(frozen=True)
class FixtureRow:
    timestamp: int
    address: str
    args: tuple[str, ...]
    source_line: int


def strip_comment(line: str) -> str:
    # Use shlex for whitespace rows so quoted fields can contain #. CSV rows are
    # handled separately and should not use inline comments.
    if "," in line:
        return line.strip()
    return line.split("#", 1)[0].strip()


def parse_row(line: str, line_no: int) -> FixtureRow | None:
    line = strip_comment(line)
    if not line:
        return None

    if "," in line:
        fields = [field.strip() for field in next(csv.reader([line]))]
    else:
        fields = shlex.split(line)

    if len(fields) < 2:
        raise ValueError(f"line {line_no}: expected timestamp and OSC address")

    try:
        timestamp = int(fields[0])
    except ValueError as exc:
        raise ValueError(f"line {line_no}: timestamp must be an integer millisecond value") from exc

    if timestamp < 0:
        raise ValueError(f"line {line_no}: timestamp must be non-negative")

    address = fields[1]
    if address not in SUPPORTED_ADDRESSES:
        raise ValueError(f"line {line_no}: unsupported address {address}")

    return FixtureRow(timestamp=timestamp, address=address, args=tuple(fields[2:]), source_line=line_no)


def validate_row(row: FixtureRow) -> None:
    arg_count = len(row.args)
    if row.address == "/room/voice/state" and arg_count < 7:
        raise ValueError(f"line {row.source_line}: /room/voice/state needs 7 args")
    if row.address == "/room/voice/disconnect" and arg_count < 1:
        raise ValueError(f"line {row.source_line}: /room/voice/disconnect needs 1 arg")
    if row.address == "/room/global/motion" and arg_count < 1:
        raise ValueError(f"line {row.source_line}: /room/global/motion needs 1 arg")
    if row.address == "/room/camera/zones":
        if arg_count < 3:
            raise ValueError(f"line {row.source_line}: /room/camera/zones needs camId cols rows and zone values")
        try:
            cols = int(row.args[1])
            rows = int(row.args[2])
        except ValueError as exc:
            raise ValueError(f"line {row.source_line}: camera zones cols/rows must be integers") from exc
        expected = 3 + cols * rows
        if cols <= 0 or rows <= 0:
            raise ValueError(f"line {row.source_line}: camera zones cols/rows must be positive")
        if arg_count < expected:
            raise ValueError(f"line {row.source_line}: camera zones needs {expected} args, got {arg_count}")


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


def convert(input_path: Path, output_path: Path, expectations: list[str]) -> None:
    rows: list[FixtureRow] = []
    for line_no, line in enumerate(input_path.read_text(encoding="utf-8").splitlines(), start=1):
        row = parse_row(line, line_no)
        if row is None:
            continue
        validate_row(row)
        rows.append(row)

    rows.sort(key=lambda row: (row.timestamp, row.source_line))
    if not rows:
        raise ValueError(f"{input_path} did not contain any supported OSC rows")

    lines = [
        "# Crowd Organ replay fixture v1",
        f"# Converted from {input_path}",
        "# Add or adjust expectation rows before committing a fixture.",
        "",
    ]
    if expectations:
        lines.extend(expectations)
        lines.append("")

    for row in rows:
        pieces = [str(row.timestamp), row.address, *row.args]
        lines.append(" ".join(pieces))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a simple OSC log to a Crowd Organ replay fixture.")
    parser.add_argument("input", type=Path, help="Input text/CSV OSC log")
    parser.add_argument("output", type=Path, help="Output .oscfixture path")
    parser.add_argument(
        "--expect",
        action="append",
        default=[],
        help="Expected gesture, e.g. 'voice 1 raise', 'zone 0 sweep_lr_top', 'global stillness'",
    )
    args = parser.parse_args()

    try:
        expectations = [parse_expectation(value) for value in args.expect]
        convert(args.input, args.output, expectations)
    except Exception as exc:  # noqa: BLE001 - CLI should print concise failures
        print(f"conversion failed: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
