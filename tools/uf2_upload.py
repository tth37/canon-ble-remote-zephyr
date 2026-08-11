#!/usr/bin/env python3
"""Copy an nRF52840 UF2 image to an explicitly selected boot volume."""

from __future__ import annotations

import os
import shutil
import string
import sys
from pathlib import Path


def candidate_roots() -> list[Path]:
    """Return platform-specific roots that may contain mounted UF2 volumes."""
    if sys.platform == "darwin":
        return [Path("/Volumes")]
    if os.name == "nt":
        return [Path(f"{letter}:/") for letter in string.ascii_uppercase]
    user = os.environ.get("USER", "")
    return [Path("/media") / user, Path("/run/media") / user]


def find_uf2_volumes() -> list[Path]:
    """Find mounted volumes identified by their standard UF2 metadata file."""
    configured = os.environ.get("UF2_VOLUME")
    if configured:
        return [Path(configured).expanduser()]

    volumes: list[Path] = []
    for root in candidate_roots():
        if (root / "INFO_UF2.TXT").is_file():
            volumes.append(root)
            continue
        if os.name == "nt":
            continue
        if not root.is_dir():
            continue
        volumes.extend(
            child
            for child in root.iterdir()
            if (child / "INFO_UF2.TXT").is_file()
        )
    return volumes


def main() -> int:
    """Validate the image and copy it to one unambiguous UF2 volume."""
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} <firmware.uf2>", file=sys.stderr)
        return 2

    image = Path(sys.argv[1]).resolve()
    if not image.is_file():
        print(f"UF2 image does not exist: {image}", file=sys.stderr)
        return 2

    volumes = find_uf2_volumes()
    if not volumes:
        print(
            "No UF2 boot volume found. Double-tap RST on the Pro Micro "
            "nRF52840, wait for its USB drive to mount, then retry.",
            file=sys.stderr,
        )
        return 2
    if len(volumes) > 1:
        print("Multiple UF2 volumes found:", file=sys.stderr)
        for volume in volumes:
            print(f"  {volume}", file=sys.stderr)
        print("Set UF2_VOLUME to the intended mount path.", file=sys.stderr)
        return 2

    destination = volumes[0] / "CANON_REMOTE.UF2"
    print(f"Copying {image.name} to {volumes[0]}...")
    try:
        shutil.copyfile(image, destination)
    except OSError as error:
        print(f"UF2 copy failed: {error}", file=sys.stderr)
        return 1
    print("Upload complete; the board should reboot automatically.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
