#!/usr/bin/env python3
"""Make compiler paths in a generated compilation database self-contained."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def normalize_command(command: str, toolchain_bin: Path) -> str:
    """Replace a leading compiler basename when that local tool exists."""
    executable, separator, remainder = command.partition(" ")
    candidate = toolchain_bin / executable
    if not separator or "/" in executable or not candidate.is_file():
        return command
    return f"{candidate}{separator}{remainder}"


def normalize_entry(entry: dict[str, Any], toolchain_bin: Path) -> None:
    """Normalize either compile_commands command representation in place."""
    command = entry.get("command")
    if isinstance(command, str):
        entry["command"] = normalize_command(command, toolchain_bin)

    arguments = entry.get("arguments")
    if isinstance(arguments, list) and arguments:
        executable = arguments[0]
        if isinstance(executable, str) and "/" not in executable:
            candidate = toolchain_bin / executable
            if candidate.is_file():
                arguments[0] = str(candidate)


def main() -> int:
    """Normalize one compilation database using a local toolchain bin dir."""
    if len(sys.argv) != 3:
        print(
            f"Usage: {Path(sys.argv[0]).name} "
            "<compile_commands.json> <toolchain-bin>",
            file=sys.stderr,
        )
        return 2

    database_path = Path(sys.argv[1]).resolve()
    toolchain_bin = Path(sys.argv[2]).resolve()
    if not database_path.is_file() or not toolchain_bin.is_dir():
        print("Compilation database or toolchain directory is missing.",
              file=sys.stderr)
        return 2

    database = json.loads(database_path.read_text(encoding="utf-8"))
    if not isinstance(database, list):
        print("Compilation database root must be a JSON array.", file=sys.stderr)
        return 2

    for entry in database:
        if isinstance(entry, dict):
            normalize_entry(entry, toolchain_bin)

    database_path.write_text(
        json.dumps(database, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
