#!/usr/bin/env python3
"""Pre-show repo and local environment checks for Crowd Organ."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "of_app" / "bin" / "data"


class Result:
    def __init__(self, level: str, label: str, detail: str = "") -> None:
        self.level = level
        self.label = label
        self.detail = detail


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def command_ok(command: list[str], timeout: float = 10.0) -> tuple[bool, str]:
    try:
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout, check=False)
    except Exception as exc:  # noqa: BLE001 - preflight should report concise environment failures
        return False, str(exc)
    output = (completed.stdout + completed.stderr).strip()
    return completed.returncode == 0, output.splitlines()[0] if output else ""


def check_configs() -> Result:
    ok, output = command_ok(["python3", "tools/validate_configs.py"])
    return Result("pass" if ok else "fail", "config validation", output)


def check_selected_calibration() -> Result:
    settings_path = DATA_DIR / "gesture_settings.json"
    try:
        settings = load_json(settings_path)
    except Exception as exc:  # noqa: BLE001
        return Result("fail", "selected calibration", f"cannot read {settings_path}: {exc}")

    rel = settings.get("room_calibration_file", "room_calibration.json")
    if not isinstance(rel, str) or rel.startswith("/") or "\\" in rel or ".." in rel or not rel.endswith(".json"):
        return Result("fail", "selected calibration", f"unsafe room_calibration_file: {rel!r}")

    path = DATA_DIR / rel
    if not path.exists():
        return Result("fail", "selected calibration", f"missing {path.relative_to(ROOT)}")
    return Result("pass", "selected calibration", str(path.relative_to(ROOT)))


def check_openframeworks() -> Result:
    of_root = Path(os.environ["OF_ROOT"]).expanduser() if "OF_ROOT" in os.environ else (ROOT / ".." / ".." / "..").resolve()
    compile_mk = of_root / "libs" / "openFrameworksCompiled" / "project" / "makefileCommon" / "compile.project.mk"
    if compile_mk.exists():
        return Result("pass", "openFrameworks makefile", str(compile_mk))
    return Result("warn", "openFrameworks makefile", f"missing {compile_mk}; set OF_ROOT or copy of_app into apps/myApps")


def check_processing() -> Result:
    cli = shutil.which("processing-java")
    if not cli:
        return Result("warn", "Processing CLI", "processing-java not found; run dashboard manually in Processing")
    try:
        text = Path(cli).read_text(encoding="utf-8", errors="ignore")
        match = re.search(r'cd "([^"]+)"', text)
        if match and not Path(match.group(1)).exists():
            return Result("warn", "Processing CLI", f"{cli} points at missing {match.group(1)}")
    except OSError:
        pass
    ok, output = command_ok([cli, "--help"], timeout=8.0)
    if ok:
        return Result("pass", "Processing CLI", cli)
    return Result("warn", "Processing CLI", output or f"{cli} did not run cleanly")


def check_artifacts() -> Result:
    artifacts = [path for path in ROOT.rglob(".DS_Store") if ".git" not in path.parts]
    if artifacts:
        return Result("warn", "platform artifacts", ", ".join(str(path.relative_to(ROOT)) for path in artifacts[:5]))
    return Result("pass", "platform artifacts", "no .DS_Store files outside .git")


def run_test(label: str, command: list[str]) -> Result:
    ok, output = command_ok(command, timeout=60.0)
    return Result("pass" if ok else "fail", label, output)


def build_dashboard() -> Result:
    cli = shutil.which("processing-java")
    if not cli:
        return Result("warn", "dashboard build", "processing-java not found")
    source = ROOT / "processing_dashboard" / "CrowdOrganDashboard.pde"
    if not source.exists():
        return Result("fail", "dashboard build", f"missing {source.relative_to(ROOT)}")

    with tempfile.TemporaryDirectory(prefix="crowdorgan_dashboard_") as tmp:
        sketch = Path(tmp) / "CrowdOrganDashboard"
        sketch.mkdir()
        shutil.copy2(source, sketch / "CrowdOrganDashboard.pde")
        output = Path(tmp) / "build"
        ok, detail = command_ok([cli, f"--sketch={sketch}", f"--output={output}", "--force", "--build"], timeout=60.0)
    return Result("pass" if ok else "fail", "dashboard build", detail)


def print_results(results: list[Result]) -> int:
    order = {"pass": "PASS", "warn": "WARN", "fail": "FAIL"}
    for result in results:
        detail = f" - {result.detail}" if result.detail else ""
        print(f"{order[result.level]:4} {result.label}{detail}")
    return 1 if any(result.level == "fail" for result in results) else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check repo config and local show-readiness hints.")
    parser.add_argument("--run-tests", action="store_true", help="Also run detector and replay fixture tests")
    parser.add_argument("--build-dashboard", action="store_true", help="Compile-check the Processing dashboard via a temporary sketch folder")
    args = parser.parse_args()

    checks: list[Callable[[], Result]] = [
        check_configs,
        check_selected_calibration,
        check_openframeworks,
        check_processing,
        check_artifacts,
    ]
    results = [check() for check in checks]

    if args.run_tests:
        results.append(run_test("detector tests", ["bash", "tests/run_detector_tests.sh"]))
        results.append(run_test("replay fixture tests", ["bash", "tests/run_replay_fixture_tests.sh"]))
    if args.build_dashboard:
        results.append(build_dashboard())

    return print_results(results)


if __name__ == "__main__":
    raise SystemExit(main())
