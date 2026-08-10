#!/usr/bin/env python3
"""Open an interactive serial terminal for the selected firmware target."""

from __future__ import annotations

import os
import sys
from pathlib import Path

from serial.tools import list_ports
from serial.tools import miniterm


TARGET = os.environ.get("SERIAL_TARGET", "esp32c6")
PREFERRED_PORT = "/dev/cu.usbserial-310" if TARGET == "esp32c6" else ""
DEFAULT_BAUD = int(
    os.environ.get(
        "SERIAL_BAUD", os.environ.get("C6_SERIAL_BAUD", "115200")
    )
)
DEFAULT_EOL = os.environ.get(
    "SERIAL_EOL", os.environ.get("C6_SERIAL_EOL", "CR")
).upper()


class TransmitCR(miniterm.Transform):
    """Send CR for Enter without rewriting received CRLF into two newlines."""

    def tx(self, text: str) -> str:
        return text.replace("\n", "\r")


def find_default_port() -> str:
    """Choose an explicit, known, or uniquely identifiable USB serial port."""
    configured_port = (
        os.environ.get("SERIAL_PORT")
        or os.environ.get(f"{TARGET.upper()}_SERIAL_PORT")
        or os.environ.get("C6_SERIAL_PORT")
    )
    if configured_port:
        return configured_port

    if PREFERRED_PORT and Path(PREFERRED_PORT).exists():
        return PREFERRED_PORT

    usb_ports = [
        port.device
        for port in list_ports.comports()
        if port.vid is not None
        and (
            port.device.startswith("/dev/cu.")
            or port.device.startswith("COM")
            or port.device.startswith("/dev/ttyUSB")
            or port.device.startswith("/dev/ttyACM")
        )
    ]
    if len(usb_ports) == 1:
        return usb_ports[0]

    return PREFERRED_PORT or "/dev/cu.usbserial-CH582M"


def apply_terminal_defaults(arguments: list[str]) -> list[str]:
    """Add defaults that pyserial's miniterm API does not expose directly."""
    has_eol = any(
        argument == "--eol" or argument.startswith("--eol=")
        for argument in arguments
    )
    if has_eol:
        return arguments
    return [*arguments, "--eol", DEFAULT_EOL]


def main() -> None:
    """Run pyserial's interactive terminal with project-friendly defaults."""
    # ESP-IDF accepts CR or LF as Enter. Miniterm's CRLF default therefore
    # submits two empty lines and prints two prompts for a single key press.
    sys.argv[1:] = apply_terminal_defaults(sys.argv[1:])
    # Pyserial's built-in CR mode also maps received CR to LF, turning the
    # board's CRLF output into LFLF. Only alter the transmit direction.
    miniterm.EOL_TRANSFORMATIONS["cr"] = TransmitCR
    miniterm.main(
        default_port=find_default_port(),
        default_baudrate=DEFAULT_BAUD,
        default_dtr=False,
        default_rts=False,
    )


if __name__ == "__main__":
    main()
