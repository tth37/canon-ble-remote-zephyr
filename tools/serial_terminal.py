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
FALLBACK_PORT = (
    "/dev/cu.usbserial-310"
    if TARGET == "esp32c6"
    else "/dev/cu.usbmodem-NRF52840"
)
LEGACY_BAUD = os.environ.get("C6_SERIAL_BAUD") if TARGET == "esp32c6" else None
LEGACY_EOL = os.environ.get("C6_SERIAL_EOL") if TARGET == "esp32c6" else None
DEFAULT_BAUD = int(os.environ.get("SERIAL_BAUD", LEGACY_BAUD or "115200"))
DEFAULT_EOL = os.environ.get("SERIAL_EOL", LEGACY_EOL or "CR").upper()


class TransmitCR(miniterm.Transform):
    """Send CR for Enter without rewriting received CRLF into two newlines."""

    def tx(self, text: str) -> str:
        return text.replace("\n", "\r")


def find_default_port() -> str:
    """Choose an explicit, known, or uniquely identifiable USB serial port."""
    configured_port = (
        os.environ.get("SERIAL_PORT")
        or os.environ.get(f"{TARGET.upper()}_SERIAL_PORT")
        or (os.environ.get("C6_SERIAL_PORT") if TARGET == "esp32c6" else None)
    )
    if configured_port:
        return configured_port

    if PREFERRED_PORT and Path(PREFERRED_PORT).exists():
        return PREFERRED_PORT

    usb_ports = [
        port
        for port in list_ports.comports()
        if port.vid is not None
        and (
            port.device.startswith("/dev/cu.")
            or port.device.startswith("COM")
            or port.device.startswith("/dev/ttyUSB")
            or port.device.startswith("/dev/ttyACM")
        )
    ]
    if TARGET == "nrf52840":
        native_usb_ports = [
            port.device
            for port in usb_ports
            if "usbmodem" in port.device.lower()
            or "cdc" in (port.description or "").lower()
        ]
        if len(native_usb_ports) == 1:
            return native_usb_ports[0]
    if len(usb_ports) == 1:
        return usb_ports[0].device

    return PREFERRED_PORT or FALLBACK_PORT


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
        # Zephyr's USB CDC shell waits for DTR. The ESP32-C6 UART bridge can
        # use DTR as a reset signal, so keep it deasserted on that target.
        default_dtr=TARGET == "nrf52840",
        default_rts=False,
    )


if __name__ == "__main__":
    main()
