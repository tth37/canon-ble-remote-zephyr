#!/usr/bin/env python3
"""Fetch the pinned, minimal subset of the official CH582M/CH583M SDK."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


SDK_REPOSITORY = "https://github.com/openwch/ch583.git"
SDK_COMMIT = "bd508ad7ceed48377619837051412a651952857f"
REQUIRED_FILES = (
    "EVT/EXAM/BLE/LIB/LIBCH58xBLE.a",
    "EVT/EXAM/BLE/LIB/CH58xBLE_LIB.h",
    "EVT/EXAM/SRC/Ld/Link.ld",
)
SPARSE_PATHS = (
    "EVT/EXAM/BLE/HAL",
    "EVT/EXAM/BLE/LIB",
    "EVT/EXAM/SRC",
    "LICENSE",
)


def run(*arguments: str, cwd: Path | None = None) -> None:
    subprocess.run(arguments, cwd=cwd, check=True)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: setup_ch582m_sdk.py <destination>", file=sys.stderr)
        return 2

    destination = Path(sys.argv[1]).resolve()
    marker = destination / ".sdk-commit"
    if (
        marker.exists()
        and marker.read_text(encoding="utf-8").strip() == SDK_COMMIT
        and all((destination / relative).is_file() for relative in REQUIRED_FILES)
    ):
        print(f"CH58x SDK already pinned at {SDK_COMMIT[:12]}")
        return 0

    if destination.exists() and any(destination.iterdir()) and not (
        destination / ".git"
    ).is_dir():
        print(
            f"Refusing to replace non-SDK directory: {destination}",
            file=sys.stderr,
        )
        return 1

    destination.mkdir(parents=True, exist_ok=True)
    if not (destination / ".git").is_dir():
        run("git", "init", str(destination))
        run("git", "remote", "add", "origin", SDK_REPOSITORY, cwd=destination)
    else:
        origin = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=destination,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if origin != SDK_REPOSITORY:
            print(
                f"Refusing unexpected SDK origin: {origin}",
                file=sys.stderr,
            )
            return 1

    run("git", "sparse-checkout", "init", "--cone", cwd=destination)
    run("git", "sparse-checkout", "set", *SPARSE_PATHS, cwd=destination)
    run(
        "git",
        "fetch",
        "--filter=blob:none",
        "--depth",
        "1",
        "origin",
        SDK_COMMIT,
        cwd=destination,
    )
    run("git", "checkout", "--detach", "FETCH_HEAD", cwd=destination)
    marker.write_text(f"{SDK_COMMIT}\n", encoding="utf-8")
    print(f"CH58x SDK ready at {destination} ({SDK_COMMIT[:12]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
