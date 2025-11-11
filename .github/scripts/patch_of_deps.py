"""Utility patcher for openFrameworks' Ubuntu dependency helper.

The upstream script has not kept pace with the packages that disappeared or
shifted names between Ubuntu releases. Instead of forking that script outright
we patch it in-place so GitHub's Ubuntu 22.04 runners stay happy while the
original file remains recognizable.
"""
from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from functools import lru_cache
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


# Patterns to zap or replace in the legacy dependency installer. These are
# regexes so we can surgically target the exact package tokens without
# accidentally mangling similar names.
REPLACEMENTS: Sequence[Tuple[str, str]] = (
    # Removed from Ubuntu 22.04 repos, so attempting to install it just breaks
    # the rest of the dependency transaction.
    (r"\blibgconf-2-4\b", ""),
    # QtWebkit was fully dropped from Jammy; any attempt to install the dev
    # package winds up with "held broken packages".
    (
        r"\blibqt5webkit5-dev\b",
        "",
    ),
    # Jammy keeps the GLES2 headers but the GLES1 development package was
    # retired. Letting it through only poisons the dependency run.
    (
        r"\blibgles1-mesa-dev\b",
        "",
    ),
    # FFmpeg dropped ``libavresample`` a while back, so the stub dev package
    # disappeared alongside it.
    (
        r"\blibavresample-dev\b",
        "",
    ),
    (
        r"\bqt5-default\b",
        "qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools",
    ),
    # Pulling the Ubuntu "restricted" extras forces an interactive fonts EULA.
    # In CI that dead-ends the run, so we keep the codecs script from touching
    # it at all.
    (r"\bubuntu-restricted-extras\b", ""),
    # gstreamer1.0-libav disappeared from the Jammy repositories; retaining it
    # causes apt to refuse the whole transaction.
    (r"\bgstreamer1\.0-libav\b", ""),
    # The legacy codecs helper still tries to grab GStreamer front-ends that
    # left the archives.
    (r"\bgstreamer1\.0-(?:qt5|vaapi|omx)\b", ""),
    # GTK's old GL extension bindings are long gone.
    (r"\blibgtkglext1-dev\b", ""),
    # The codecs helper still name-drops multimedia stacks that Ubuntu pulled
    # for legal and maintenance reasons.
    (r"\blibquicktime-dev\b", ""),
    (r"\blibfaac-dev\b", ""),
    (r"\blibfaad-dev\b", ""),
    (r"\blibx264-dev\b", ""),
    (r"\blibmp4v2-dev\b", ""),
    (r"\blibmpeg2-4-dev\b", ""),
    (r"\blibmpeg3-dev\b", ""),
    (r"\blibxvidcore-dev\b", ""),
    (r"\blibvo-aacenc-dev\b", ""),
    (r"\blibvo-amrwbenc-dev\b", ""),
    (r"\blibopenni-dev\b", ""),
    (r"\bx264\b", ""),
    # 1394 renamed its dev headers; jammy ships ``libdc1394-dev``.
    (r"\blibdc1394-22-dev\b", "libdc1394-dev"),
    # Unicap never made the jump to Jammy.
    (r"\blibunicap2-dev\b", ""),
    # Schroedinger was dropped alongside the old GStreamer stack.
    (r"\blibschroedinger-dev\b", ""),
    # The OpenCORE AMR dev headers disappeared from the archives.
    (r"\blibopencore-amrnb-dev\b", ""),
    (r"\blibopencore-amrwb-dev\b", ""),
    # libmad's development headers are long gone as well.
    (r"\blibmad0-dev\b", ""),
    # libidn11-dev was superseded by libidn2-dev years ago.
    (r"\blibidn11-dev\b", "libidn2-dev"),
)

# Shell tokens we should not try to treat as packages.
CONTROL_TOKENS = {
    "&&",
    "||",
    "|",
    ";",
    "&",
    ")",
    "then",
    "fi",
    "do",
    "done",
    "esac",
    "elif",
    "else",
    "{",
    "}",
}

# Tokens that flag an array assignment as shell command scaffolding instead of a
# plain package list. When these show up inside ``FOO=(...)`` we leave the block
# alone so subcommands like "install" survive untouched. We also reuse the
# collection when analysing apt invocations so verbs such as "install" or
# "purge" never get mistaken for packages.
ASSIGNMENT_COMMAND_TOKENS = {
    "apt-get",
    "apt",
    "sudo",
    "install",
    "remove",
    "update",
    "upgrade",
    "autoremove",
    "purge",
    "dist-upgrade",
    "full-upgrade",
}

# Package names are conservative: apt labels are alphanumeric with dashes, dots
# or plus signs. Anything outside that gets left alone so shell variables and
# command substitutions survive untouched.
PACKAGE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+-]*")


APT_FAILURE_MARKERS = (
    "has no installation candidate",
    "but it is not going to be installed",
    "but it is not installable",
    "unable to locate package",
    "no packages found",
    "is not available, but is referred to by another package",
    "however the following packages replace it",
)

APT_SIMULATION_PATTERNS = (
    re.compile(
        r"package ['\"]?(?P<name>[A-Za-z0-9][A-Za-z0-9.+-]*)['\"]? has no installation candidate",
        re.IGNORECASE,
    ),
    re.compile(
        r"package ['\"]?(?P<name>[A-Za-z0-9][A-Za-z0-9.+-]*)['\"]? is not available",
        re.IGNORECASE,
    ),
    re.compile(
        r"unable to locate package ['\"]?(?P<name>[A-Za-z0-9][A-Za-z0-9.+-]*)",
        re.IGNORECASE,
    ),
    re.compile(
        r"\b(?P<name>[A-Za-z0-9][A-Za-z0-9.+-]*)\s*:\s*depends:",
        re.IGNORECASE,
    ),
    re.compile(
        r"depends:\s*(?P<name>[A-Za-z0-9][A-Za-z0-9.+-]*)\b[^(]*but it is not (?:going to be installed|installable)",
        re.IGNORECASE,
    ),
)

APT_LISTS_DIR = Path("/var/lib/apt/lists")


def _apt_metadata_available() -> bool:
    """Return True if the local apt metadata cache looks populated."""

    try:
        return any(APT_LISTS_DIR.iterdir())
    except (FileNotFoundError, PermissionError):  # pragma: no cover - container without apt
        return False


@lru_cache(maxsize=None)
def apt_candidate_exists(package: str) -> bool:
    """Return True if apt knows how to install *package* on this runner."""

    if not _apt_metadata_available():
        # When the apt lists are empty (for example in dev containers without
        # a preceding ``apt-get update``) every probe reports "unable to
        # locate". That would nuke perfectly valid packages, so we bail out
        # early and trust the upstream list in that scenario.
        return True

    env = os.environ.copy()
    env.setdefault("DEBIAN_FRONTEND", "noninteractive")

    apt_probe = ["apt-get", "-s", "install", package]
    if os.name != "nt" and hasattr(os, "geteuid") and os.geteuid() != 0:
        apt_probe = ["sudo", *apt_probe]

    probes = (
        apt_probe,
        ["apt-cache", "show", package],
    )

    for command in probes:
        try:
            result = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                check=False,
            )
        except FileNotFoundError:  # pragma: no cover - defensive guard
            continue

        output = result.stdout.decode("utf-8", errors="ignore")
        lowered_output = output.lower()
        if result.returncode == 0:
            return True

        if any(marker in lowered_output for marker in APT_FAILURE_MARKERS):
            return False

    return True


def _collect_install_block(lines: List[str], start: int) -> Tuple[int, List[str]]:
    """Return the end index and the joined lines for one install command."""

    block = [lines[start]]
    end = start
    while block[-1].rstrip().endswith("\\") and end + 1 < len(lines):
        end += 1
        block.append(lines[end])
    return end, block


def _normalize_command(block: Iterable[str]) -> str:
    """Collapse a multi-line shell command into a single logical line."""

    pieces = []
    for line in block:
        stripped = line.rstrip()
        if stripped.endswith("\\"):
            stripped = stripped[:-1]
        pieces.append(stripped.strip())
    return " ".join(piece for piece in pieces if piece)


def _shell_split(command: str) -> List[str]:
    """Split *command* into shell tokens while preserving punctuation."""

    lexer = shlex.shlex(command, posix=True, punctuation_chars=";&|()")
    lexer.whitespace_split = True
    lexer.commenters = ""
    return list(lexer)


def _join_tokens(tokens: Sequence[str]) -> str:
    """Reconstruct a shell command from lexical *tokens*."""

    if not tokens:
        return ""

    pieces: List[str] = []
    for token in tokens:
        if not pieces:
            pieces.append(token)
            continue

        if token == ";":
            if pieces:
                pieces[-1] += ";"
            else:
                pieces.append(";")
            continue

        if token in {"&&", "||", "|", "&"}:
            pieces.append(" " + token)
            continue

        if token == "(":
            if pieces:
                pieces[-1] += "("
            else:
                pieces.append("(")
            continue

        if token == ")":
            if pieces:
                pieces[-1] += ")"
            else:
                pieces.append(")")
            continue

        if pieces[-1].endswith("("):
            pieces.append(token)
            continue

        pieces.append(" " + token)

    return "".join(pieces)


def _extract_missing_from_output(
    output: str, token_map: Dict[str, str]
) -> List[str]:
    """Return packages flagged as missing inside *output*.

    ``token_map`` maps lowercase package names to the original token spelling so we
    can preserve whatever casing the upstream script used.
    """

    missing: List[str] = []
    seen = set()
    for pattern in APT_SIMULATION_PATTERNS:
        for match in pattern.finditer(output):
            candidate = match.group("name").lower()
            if candidate in token_map and candidate not in seen:
                missing.append(token_map[candidate])
                seen.add(candidate)
    return missing


def _simulate_missing_packages(packages: Sequence[str]) -> List[str]:
    """Return packages from *packages* that apt refuses to install."""

    if not packages or not _apt_metadata_available():
        return []

    env = os.environ.copy()
    env.setdefault("DEBIAN_FRONTEND", "noninteractive")

    original_order = list(dict.fromkeys(packages))
    remaining = original_order[:]
    missing: List[str] = []

    while remaining:
        command: List[str] = ["apt-get", "-s", "install", *remaining]
        if os.name != "nt" and hasattr(os, "geteuid") and os.geteuid() != 0:
            command = ["sudo", *command]

        token_map = {token.lower(): token for token in remaining}
        try:
            result = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                check=False,
            )
        except FileNotFoundError:  # pragma: no cover - defensive guard
            break

        output = result.stdout.decode("utf-8", errors="ignore")
        batch_missing = _extract_missing_from_output(output, token_map)
        if not batch_missing:
            break

        missing.extend(batch_missing)
        remaining = [token for token in remaining if token not in batch_missing]

    return missing


def _collect_parenthesized_block(lines: List[str], start: int) -> Tuple[int, List[str]]:
    """Return the end index and lines covering one ``FOO=(...)`` assignment."""

    block = [lines[start]]
    depth = lines[start].count("(") - lines[start].count(")")
    idx = start
    while depth > 0 and idx + 1 < len(lines):
        idx += 1
        line = lines[idx]
        block.append(line)
        depth += line.count("(")
        depth -= line.count(")")
    return idx, block


def _rewrite_assignment(tokens: List[str]) -> Tuple[List[str], List[str]]:
    """Remove missing apt packages from ``FOO=(...)`` style blocks."""

    if "(" not in tokens or ")" not in tokens:
        return tokens, []

    new_tokens: List[str] = []
    skipped: List[str] = []
    depth = 0
    for token in tokens:
        if token == "(":
            depth += 1
            new_tokens.append(token)
            continue

        if token == ")":
            depth = max(0, depth - 1)
            new_tokens.append(token)
            continue

        if depth:
            if token.startswith("-") or not PACKAGE_RE.fullmatch(token):
                new_tokens.append(token)
                continue

            if apt_candidate_exists(token):
                new_tokens.append(token)
            else:
                skipped.append(token)
            continue

        new_tokens.append(token)

    return new_tokens, skipped


def _rewrite_install_command(
    tokens: List[str],
) -> Tuple[List[str], List[str]]:
    """Remove dead packages from the parsed apt-get install command."""

    try:
        apt_idx = tokens.index("apt-get")
    except ValueError:
        return tokens, []

    split_idx = len(tokens)
    for idx in range(apt_idx, len(tokens)):
        if tokens[idx] in CONTROL_TOKENS:
            split_idx = idx
            break

    command_tokens = tokens[apt_idx:split_idx]
    prefix_tokens = tokens[:apt_idx]
    suffix_tokens = tokens[split_idx:]

    install_idx = None
    for idx, token in enumerate(command_tokens):
        if token == "install":
            install_idx = idx
            break
    if install_idx is None:
        return tokens, []

    pre_install = command_tokens[: install_idx + 1]
    post_install = command_tokens[install_idx + 1 :]

    new_tail: List[str] = []
    skipped: List[str] = []
    saw_dynamic_package = False
    for token in post_install:
        normalized = token.lower()
        if normalized in ASSIGNMENT_COMMAND_TOKENS:
            new_tail.append(token)
            continue
        if "$" in token or "`" in token:
            saw_dynamic_package = True
            new_tail.append(token)
            continue

        if token.startswith("-") or not PACKAGE_RE.fullmatch(token):
            new_tail.append(token)
            continue

        if apt_candidate_exists(token):
            new_tail.append(token)
        else:
            skipped.append(token)

    simulation_candidates = [
        token
        for token in new_tail
        if PACKAGE_RE.fullmatch(token) and not token.startswith("-")
    ]
    simulated_missing = _simulate_missing_packages(simulation_candidates)
    if simulated_missing:
        new_tail = [token for token in new_tail if token not in simulated_missing]
        skipped.extend(simulated_missing)

    has_package = saw_dynamic_package or any(
        PACKAGE_RE.fullmatch(token) and not token.startswith("-")
        for token in new_tail
    )

    if has_package:
        new_tokens = prefix_tokens + pre_install + new_tail + suffix_tokens
    else:
        new_tokens = prefix_tokens + ["true"] + suffix_tokens

    return new_tokens, skipped


def strip_missing_packages(script_text: str) -> Tuple[str, List[str]]:
    """Rewrite apt install invocations so missing packages get dropped."""

    lines = script_text.splitlines()
    skipped_packages: List[str] = []
    changed = False
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("#"):
            i += 1
            continue

        if not re.search(r"\bapt-get\b", line) or "install" not in line:
            i += 1
            continue

        end, block = _collect_install_block(lines, i)
        indent = re.match(r"\s*", block[0]).group(0)
        logical = _normalize_command(block)
        if not logical:
            i = end + 1
            continue

        tokens = _shell_split(logical)
        new_tokens, skipped = _rewrite_install_command(tokens)
        if skipped:
            skipped_packages.extend(skipped)

        new_command = indent + _join_tokens(new_tokens)
        original_command = indent + logical
        if new_command != original_command:
            changed = True
            lines[i] = new_command
            for remove_idx in range(i + 1, end + 1):
                lines[remove_idx] = None  # type: ignore[index]
        i = end + 1

    if changed:
        new_lines = [line for line in lines if line is not None]
        # Preserve the trailing newline from the original text if one existed.
        trailing_newline = "\n" if script_text.endswith("\n") else ""
        rewritten = "\n".join(new_lines) + trailing_newline
        return rewritten, skipped_packages

    return script_text, skipped_packages


def strip_missing_assignments(script_text: str) -> Tuple[str, List[str]]:
    """Rewrite array assignments so missing packages vanish before runtime."""

    lines = script_text.splitlines()
    skipped_packages: List[str] = []
    changed = False
    i = 0
    while i < len(lines):
        line = lines[i]
        if line is None:
            i += 1
            continue

        stripped = line.lstrip()
        if not stripped or stripped.startswith("#"):
            i += 1
            continue

        if "=" not in line:
            i += 1
            continue

        lhs, rhs = line.split("=", 1)
        stripped_rhs = rhs.strip()

        if stripped_rhs.startswith("(") and "(" in line:
            end, block = _collect_parenthesized_block(lines, i)
            assignment = "\n".join(block)
            tokens = _shell_split(assignment)
            body_tokens: List[str] = []
            depth = 0
            for token in tokens:
                if token == "(":
                    depth += 1
                    continue
                if token == ")":
                    depth = max(0, depth - 1)
                    continue
                if depth:
                    body_tokens.append(token)
            if any(token in ASSIGNMENT_COMMAND_TOKENS for token in body_tokens):
                i = end + 1
                continue
            packageish_body = [
                token
                for token in body_tokens
                if PACKAGE_RE.fullmatch(token) and not token.startswith("-")
            ]
            if not packageish_body:
                i = end + 1
                continue
            non_packageish = [
                token
                for token in body_tokens
                if not PACKAGE_RE.fullmatch(token) and not token.startswith("-")
            ]
            dynamic_tokens = [
                token for token in non_packageish if ("$" in token or "`" in token)
            ]
            static_noise = [
                token
                for token in non_packageish
                if token not in dynamic_tokens
            ]
            if (
                packageish_body
                and static_noise
                and len(packageish_body) <= len(static_noise)
            ):
                i = end + 1
                continue
            new_tokens, skipped = _rewrite_assignment(tokens)
            if skipped:
                indent = re.match(r"\s*", block[0]).group(0)
                new_assignment = indent + _join_tokens(new_tokens)
                lines[i] = new_assignment
                for remove_idx in range(i + 1, end + 1):
                    lines[remove_idx] = None  # type: ignore[index]
                skipped_packages.extend(skipped)
                changed = True
            i = end + 1
            continue

        str_match = re.match(
            r"^(?P<indent>\s*)(?P<name>[A-Za-z0-9_+]+)=([\"'])(?P<body>.*)\3\s*$",
            line,
        )
        if str_match:
            body = str_match.group("body")
            indent = str_match.group("indent")
            name = str_match.group("name")
            quote = str_match.group(3)
            tokens = body.split()
            if any(
                token in {"apt", "apt-get", "sudo", "install", "remove", "upgrade", "update"}
                for token in tokens
            ):
                i += 1
                continue
            packageish_tokens = [
                token
                for token in tokens
                if PACKAGE_RE.fullmatch(token) and not token.startswith("-")
            ]
            non_packageish = [
                token
                for token in tokens
                if not PACKAGE_RE.fullmatch(token) and not token.startswith("-")
            ]
            dynamic_tokens = [
                token for token in non_packageish if ("$" in token or "`" in token)
            ]
            static_noise = [
                token for token in non_packageish if token not in dynamic_tokens
            ]
            if not packageish_tokens:
                i += 1
                continue
            if static_noise and len(packageish_tokens) <= len(static_noise):
                i += 1
                continue
            new_tokens: List[str] = []
            skipped: List[str] = []
            for token in tokens:
                if token.startswith("$"):
                    new_tokens.append(token)
                    continue
                if token.startswith("-") or not PACKAGE_RE.fullmatch(token):
                    new_tokens.append(token)
                    continue
                if apt_candidate_exists(token):
                    new_tokens.append(token)
                else:
                    skipped.append(token)

            if skipped:
                skipped_packages.extend(skipped)
                changed = True
                new_body = " ".join(new_tokens)
                lines[i] = f"{indent}{name}={quote}{new_body}{quote}"

        i += 1

    if changed:
        new_lines = [line for line in lines if line is not None]
        trailing_newline = "\n" if script_text.endswith("\n") else ""
        return "\n".join(new_lines) + trailing_newline, skipped_packages

    return script_text, skipped_packages


def patch_dependency_helper(target: Path) -> None:
    """Rewrite the dependency helper in-place with modern package names."""

    original = target.read_text()
    patched = original
    for pattern, replacement in REPLACEMENTS:
        patched = re.sub(pattern, replacement, patched)

    total_skipped: List[str] = []
    patched, skipped = strip_missing_assignments(patched)
    if skipped:
        total_skipped.extend(skipped)

    patched, skipped = strip_missing_packages(patched)
    if skipped:
        total_skipped.extend(skipped)

    if patched != original:
        target.write_text(patched)

    if total_skipped:
        print(
            "Removed defunct apt packages: "
            + ", ".join(sorted(set(total_skipped)))
        )


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(
            "Usage: patch_of_deps.py <path/to/install_dependencies.sh> [more.sh ...]"
        )

    for script_path in sys.argv[1:]:
        patch_dependency_helper(Path(script_path))
