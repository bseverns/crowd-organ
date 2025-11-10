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
from typing import Iterable, List, Sequence, Tuple


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

# Package names are conservative: apt labels are alphanumeric with dashes, dots
# or plus signs. Anything outside that gets left alone so shell variables and
# command substitutions survive untouched.
PACKAGE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+-]*")


APT_FAILURE_MARKERS = (
    "has no installation candidate",
    "but it is not going to be installed",
    "but it is not installable",
    "Unable to locate package",
    "No packages found",
)

APT_LISTS_DIR = Path("/var/lib/apt/lists")


def _apt_metadata_available() -> bool:
    """Return True if the local apt metadata cache looks populated."""

    try:
        return any(APT_LISTS_DIR.iterdir())
    except FileNotFoundError:  # pragma: no cover - container without apt
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
        if result.returncode == 0:
            return True

        if any(marker in output for marker in APT_FAILURE_MARKERS):
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


def patch_dependency_helper(target: Path) -> None:
    """Rewrite the dependency helper in-place with modern package names."""

    original = target.read_text()
    patched = original
    for pattern, replacement in REPLACEMENTS:
        patched = re.sub(pattern, replacement, patched)

    patched, skipped = strip_missing_packages(patched)

    if patched != original:
        target.write_text(patched)

    if skipped:
        print(
            "Removed defunct apt packages: " + ", ".join(sorted(set(skipped)))
        )


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(
            "Usage: patch_of_deps.py <path/to/install_dependencies.sh> [more.sh ...]"
        )

    for script_path in sys.argv[1:]:
        patch_dependency_helper(Path(script_path))
