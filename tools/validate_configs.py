#!/usr/bin/env python3
"""Validate Crowd Organ JSON configs with stdlib-only checks.

The schema files in ``schemas/`` document the expected shapes. This script
implements the same constraints plus cross-field checks so CI does not need an
external jsonschema package.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "of_app" / "bin" / "data"
SCHEMA_DIR = ROOT / "schemas"


class ValidationError(Exception):
    pass


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError as exc:
        raise ValidationError(f"{path}: invalid JSON: {exc}") from exc


def path_name(parts: list[str]) -> str:
    return ".".join(parts) if parts else "<root>"


def require_type(value: Any, expected_type: type | tuple[type, ...], path: list[str]) -> None:
    if expected_type is int:
        ok = isinstance(value, int) and not isinstance(value, bool)
    elif expected_type is float:
        ok = isinstance(value, (int, float)) and not isinstance(value, bool)
    else:
        ok = isinstance(value, expected_type)
    if not ok:
        raise ValidationError(f"{path_name(path)} has invalid type")


def optional_int(obj: dict[str, Any], key: str, path: list[str], minimum: int, maximum: int) -> int | None:
    if key not in obj:
        return None
    value = obj[key]
    require_type(value, int, path + [key])
    if value < minimum or value > maximum:
        raise ValidationError(f"{path_name(path + [key])} must be between {minimum} and {maximum}")
    return value


def optional_number(obj: dict[str, Any], key: str, path: list[str], minimum: float, maximum: float) -> float | None:
    if key not in obj:
        return None
    value = obj[key]
    require_type(value, float, path + [key])
    numeric = float(value)
    if numeric < minimum or numeric > maximum:
        raise ValidationError(f"{path_name(path + [key])} must be between {minimum} and {maximum}")
    return numeric


def optional_string(obj: dict[str, Any], key: str, path: list[str], allow_empty: bool = True) -> str | None:
    if key not in obj:
        return None
    value = obj[key]
    require_type(value, str, path + [key])
    if not allow_empty and not value:
        raise ValidationError(f"{path_name(path + [key])} must not be empty")
    return value


def optional_bool(obj: dict[str, Any], key: str, path: list[str]) -> bool | None:
    if key not in obj:
        return None
    value = obj[key]
    require_type(value, bool, path + [key])
    return value


def is_safe_data_relative_json_path(value: str) -> bool:
    return (
        bool(value)
        and not value.startswith("/")
        and "\\" not in value
        and ".." not in value
        and value.endswith(".json")
    )


def validate_route(route: Any, path: list[str]) -> None:
    require_type(route, dict, path)
    address = optional_string(route, "address", path, allow_empty=False)
    host = optional_string(route, "host", path, allow_empty=False)
    optional_int(route, "port", path, 1, 65535)
    if address is None or host is None or "port" not in route:
        raise ValidationError(f"{path_name(path)} must include address, host, and port")
    if not address.startswith("/"):
        raise ValidationError(f"{path_name(path + ['address'])} must start with /")


def validate_gesture_settings(data: Any) -> None:
    require_type(data, dict, ["gesture_settings"])
    optional_int(data, "listen_port", ["gesture_settings"], 1, 65535)
    optional_string(data, "gesture_host", ["gesture_settings"], allow_empty=False)
    optional_int(data, "gesture_port", ["gesture_settings"], 1, 65535)
    optional_bool(data, "enable_sending", ["gesture_settings"])
    optional_bool(data, "enable_osc_input", ["gesture_settings"])
    optional_bool(data, "enable_sensors", ["gesture_settings"])
    calibration_file = optional_string(data, "room_calibration_file", ["gesture_settings"], allow_empty=False)
    if calibration_file is not None and not is_safe_data_relative_json_path(calibration_file):
        raise ValidationError("gesture_settings.room_calibration_file must be a relative .json path under bin/data")

    sensors = data.get("sensors")
    if sensors is not None:
        require_type(sensors, dict, ["gesture_settings", "sensors"])
        min_depth = optional_int(sensors, "kinect_min_depth_mm", ["gesture_settings", "sensors"], 1, 10000)
        max_depth = optional_int(sensors, "kinect_max_depth_mm", ["gesture_settings", "sensors"], 1, 10000)
        min_blob = optional_int(sensors, "min_blob_area", ["gesture_settings", "sensors"], 1, 500000)
        max_blob = optional_int(sensors, "max_blob_area", ["gesture_settings", "sensors"], 1, 2000000)
        optional_int(sensors, "max_kinect_voices", ["gesture_settings", "sensors"], 1, 64)
        optional_number(sensors, "voice_match_distance", ["gesture_settings", "sensors"], 0.01, 2.0)
        optional_int(sensors, "camera_width", ["gesture_settings", "sensors"], 160, 1920)
        optional_int(sensors, "camera_height", ["gesture_settings", "sensors"], 160, 1920)
        optional_int(sensors, "cam_grid_cols", ["gesture_settings", "sensors"], 1, 16)
        optional_int(sensors, "cam_grid_rows", ["gesture_settings", "sensors"], 1, 16)
        optional_number(sensors, "camera_motion_floor", ["gesture_settings", "sensors"], 0.0, 1.0)
        optional_number(sensors, "camera_smoothing", ["gesture_settings", "sensors"], 0.0, 0.99)
        if min_depth is not None and max_depth is not None and min_depth >= max_depth:
            raise ValidationError("gesture_settings.sensors.kinect_min_depth_mm must be less than kinect_max_depth_mm")
        if min_blob is not None and max_blob is not None and min_blob >= max_blob:
            raise ValidationError("gesture_settings.sensors.min_blob_area must be less than max_blob_area")

    routes = data.get("routes")
    require_type(routes, dict, ["gesture_settings", "routes"])
    for key, route in routes.items():
        validate_route(route, ["gesture_settings", "routes", key])


def validate_gesture_tuning(data: Any) -> None:
    require_type(data, dict, ["gesture_tuning"])
    optional_int(data, "voice_history_capacity", ["gesture_tuning"], 1, 600)

    voice = data.get("voice", {})
    require_type(voice, dict, ["gesture_tuning", "voice"])
    optional_number(voice, "raise_delta_y", ["gesture_tuning", "voice"], 0.01, 2.0)
    optional_number(voice, "lower_delta_y", ["gesture_tuning", "voice"], 0.01, 2.0)
    optional_number(voice, "swipe_delta_x", ["gesture_tuning", "voice"], 0.01, 2.0)
    optional_number(voice, "swipe_orthogonality", ["gesture_tuning", "voice"], 0.1, 10.0)
    optional_number(voice, "raise_horizontal_limit", ["gesture_tuning", "voice"], 0.0, 2.0)
    optional_number(voice, "swipe_vertical_limit", ["gesture_tuning", "voice"], 0.0, 2.0)
    optional_number(voice, "shake_radius", ["gesture_tuning", "voice"], 0.01, 2.0)
    optional_int(voice, "shake_min_sign_flips", ["gesture_tuning", "voice"], 1, 64)
    optional_number(voice, "shake_min_motion", ["gesture_tuning", "voice"], 0.0, 10.0)
    burst_threshold = optional_number(voice, "burst_speed_threshold", ["gesture_tuning", "voice"], 0.0, 100.0)
    burst_max = optional_number(voice, "burst_max_speed", ["gesture_tuning", "voice"], 0.01, 100.0)
    optional_number(voice, "hold_motion_threshold", ["gesture_tuning", "voice"], 0.0, 10.0)
    optional_int(voice, "hold_duration_ms", ["gesture_tuning", "voice"], 1, 60000)
    min_window = optional_int(voice, "min_window_ms", ["gesture_tuning", "voice"], 1, 60000)
    max_window = optional_int(voice, "max_window_ms", ["gesture_tuning", "voice"], 1, 60000)
    optional_int(voice, "gesture_cooldown_ms", ["gesture_tuning", "voice"], 0, 60000)
    optional_int(voice, "burst_cooldown_ms", ["gesture_tuning", "voice"], 0, 60000)
    optional_int(voice, "hold_cooldown_ms", ["gesture_tuning", "voice"], 0, 60000)
    if burst_threshold is not None and burst_max is not None and burst_threshold >= burst_max:
        raise ValidationError("gesture_tuning.voice.burst_speed_threshold must be less than burst_max_speed")
    if min_window is not None and max_window is not None and min_window > max_window:
        raise ValidationError("gesture_tuning.voice.min_window_ms must be <= max_window_ms")

    zone = data.get("zone", {})
    require_type(zone, dict, ["gesture_tuning", "zone"])
    zone_history = optional_int(zone, "history_ms", ["gesture_tuning", "zone"], 1, 60000)
    sweep_window = optional_int(zone, "sweep_window_ms", ["gesture_tuning", "zone"], 1, 60000)
    optional_int(zone, "sweep_min_steps", ["gesture_tuning", "zone"], 2, 256)
    optional_number(zone, "sweep_min_strength", ["gesture_tuning", "zone"], 0.0, 1.0)
    optional_int(zone, "sweep_cooldown_ms", ["gesture_tuning", "zone"], 0, 60000)
    optional_number(zone, "pulse_threshold", ["gesture_tuning", "zone"], 0.0, 1.0)
    optional_number(zone, "pulse_slope_threshold", ["gesture_tuning", "zone"], 0.0, 1.0)
    optional_int(zone, "pulse_cooldown_ms", ["gesture_tuning", "zone"], 0, 60000)
    if zone_history is not None and sweep_window is not None and sweep_window > zone_history:
        raise ValidationError("gesture_tuning.zone.sweep_window_ms must be <= history_ms")

    global_cfg = data.get("global", {})
    require_type(global_cfg, dict, ["gesture_tuning", "global"])
    global_history = optional_int(global_cfg, "history_ms", ["gesture_tuning", "global"], 1, 60000)
    eruption_low = optional_number(global_cfg, "eruption_low", ["gesture_tuning", "global"], 0.0, 1.0)
    eruption_high = optional_number(global_cfg, "eruption_high", ["gesture_tuning", "global"], 0.0, 1.0)
    optional_int(global_cfg, "eruption_cooldown_ms", ["gesture_tuning", "global"], 0, 60000)
    eruption_window = optional_int(global_cfg, "eruption_window_ms", ["gesture_tuning", "global"], 1, 60000)
    optional_number(global_cfg, "stillness_motion_threshold", ["gesture_tuning", "global"], 0.0, 1.0)
    optional_int(global_cfg, "stillness_duration_ms", ["gesture_tuning", "global"], 1, 60000)
    optional_int(global_cfg, "stillness_min_voices", ["gesture_tuning", "global"], 1, 64)
    optional_int(global_cfg, "stillness_cooldown_ms", ["gesture_tuning", "global"], 0, 60000)
    if eruption_low is not None and eruption_high is not None and eruption_low > eruption_high:
        raise ValidationError("gesture_tuning.global.eruption_low must be <= eruption_high")
    if global_history is not None and eruption_window is not None and eruption_window > global_history:
        raise ValidationError("gesture_tuning.global.eruption_window_ms must be <= history_ms")


def validate_room_calibration(data: Any) -> None:
    require_type(data, dict, ["room_calibration"])
    optional_string(data, "room_name", ["room_calibration"], allow_empty=False)
    optional_string(data, "notes", ["room_calibration"])

    kinect = data.get("kinect")
    require_type(kinect, dict, ["room_calibration", "kinect"])
    min_depth = optional_int(kinect, "min_depth_mm", ["room_calibration", "kinect"], 1, 10000)
    max_depth = optional_int(kinect, "max_depth_mm", ["room_calibration", "kinect"], 1, 10000)
    min_blob = optional_int(kinect, "min_blob_area", ["room_calibration", "kinect"], 1, 500000)
    max_blob = optional_int(kinect, "max_blob_area", ["room_calibration", "kinect"], 1, 2000000)
    optional_int(kinect, "max_voices", ["room_calibration", "kinect"], 1, 64)
    optional_number(kinect, "voice_match_distance", ["room_calibration", "kinect"], 0.01, 2.0)
    if min_depth is not None and max_depth is not None and min_depth >= max_depth:
        raise ValidationError("room_calibration.kinect.min_depth_mm must be less than max_depth_mm")
    if min_blob is not None and max_blob is not None and min_blob >= max_blob:
        raise ValidationError("room_calibration.kinect.min_blob_area must be less than max_blob_area")

    cameras = data.get("cameras")
    require_type(cameras, list, ["room_calibration", "cameras"])
    seen_ids: set[int] = set()
    for index, camera in enumerate(cameras):
        path = ["room_calibration", "cameras", str(index)]
        require_type(camera, dict, path)
        cam_id = optional_int(camera, "id", path, 0, 15)
        if cam_id is None:
            raise ValidationError(f"{path_name(path)} missing id")
        if cam_id in seen_ids:
            raise ValidationError(f"{path_name(path + ['id'])} duplicates camera id {cam_id}")
        seen_ids.add(cam_id)

        optional_string(camera, "label", path)
        grid = camera.get("grid")
        require_type(grid, dict, path + ["grid"])
        cols = optional_int(grid, "cols", path + ["grid"], 1, 16)
        rows = optional_int(grid, "rows", path + ["grid"], 1, 16)
        if cols is None or rows is None:
            raise ValidationError(f"{path_name(path + ['grid'])} missing cols/rows")
        zone_count = cols * rows

        ignored = camera.get("ignored_zones", [])
        require_type(ignored, list, path + ["ignored_zones"])
        for zone in ignored:
            require_type(zone, int, path + ["ignored_zones"])
            if zone < 0 or zone >= zone_count:
                raise ValidationError(f"{path_name(path + ['ignored_zones'])} zone {zone} outside grid 0..{zone_count - 1}")

        labels = camera.get("zone_labels", {})
        require_type(labels, dict, path + ["zone_labels"])
        for key, label in labels.items():
            require_type(label, str, path + ["zone_labels", key])
            try:
                zone_index = int(key)
            except ValueError as exc:
                raise ValidationError(f"{path_name(path + ['zone_labels', key])} key must be numeric") from exc
            if zone_index < 0 or zone_index >= zone_count:
                raise ValidationError(f"{path_name(path + ['zone_labels', key])} outside grid 0..{zone_count - 1}")


def ensure_schema_files_parse() -> None:
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        load_json(path)


def main() -> int:
    try:
        ensure_schema_files_parse()
        settings_path = DATA_DIR / "gesture_settings.json"
        settings = load_json(settings_path)
        validate_gesture_settings(settings)
        validate_gesture_tuning(load_json(DATA_DIR / "gesture_tuning.json"))

        calibration_file = settings.get("room_calibration_file", "room_calibration.json")
        if not isinstance(calibration_file, str) or not is_safe_data_relative_json_path(calibration_file):
            raise ValidationError("gesture_settings.room_calibration_file must be a relative .json path under bin/data")
        validate_room_calibration(load_json(DATA_DIR / calibration_file))
    except ValidationError as exc:
        print(f"config validation failed: {exc}", file=sys.stderr)
        return 1

    print("config validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
