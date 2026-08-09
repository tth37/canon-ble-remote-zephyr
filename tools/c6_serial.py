#!/usr/bin/env python3
"""Open an interactive serial terminal for the ESP32-C6 UART console."""

from __future__ import annotations

import os
import sys
from pathlib import Path

from serial.tools import list_ports
from serial.tools import miniterm


PREFERRED_PORT = "/dev/cu.usbserial-310"
DEFAULT_BAUD = int(os.environ.get("C6_SERIAL_BAUD", "115200"))
DEFAULT_EOL = os.environ.get("C6_SERIAL_EOL", "CR").upper()


def find_default_port() -> str:
    """Choose an explicit, known, or uniquely identifiable USB serial port."""
    configured_port = os.environ.get("C6_SERIAL_PORT")
    if configured_port:
        return configured_port

    if Path(PREFERRED_PORT).exists():
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

    return PREFERRED_PORT


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
    miniterm.main(
        default_port=find_default_port(),
        default_baudrate=DEFAULT_BAUD,
        default_dtr=False,
        default_rts=False,
    )


if __name__ == "__main__":
    main()
