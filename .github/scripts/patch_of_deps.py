"""Utility patcher for openFrameworks' Ubuntu dependency helper.

This helper script exists because the openFrameworks tarballs still ship
with an `install_dependencies.sh` that targets long-dead Ubuntu releases.
Rather than forking that helper wholesale, we surgically prune or swap a
few packages so the upstream script stays mostly stock while the GitHub
Actions runner on Ubuntu 22.04 keeps chugging along.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


# Patterns to zap or replace in the legacy dependency installer. These are
# regexes so we can surgically target the exact package tokens without
# accidentally mangling similar names.
REPLACEMENTS = (
    (r"\blibgconf-2-4\b", ""),
    (
        r"\bqt5-default\b",
        "qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools",
    ),
)


def patch_dependency_helper(target: Path) -> None:
    """Rewrite the dependency helper in-place with modern package names."""
    original = target.read_text()
    patched = original
    for pattern, replacement in REPLACEMENTS:
        patched = re.sub(pattern, replacement, patched)

    if patched != original:
        target.write_text(patched)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(
            "Usage: patch_of_deps.py <path/to/install_dependencies.sh>"
        )

    patch_dependency_helper(Path(sys.argv[1]))
