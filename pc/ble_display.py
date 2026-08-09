#!/usr/bin/env python3
"""Scan for the ESP32-C6 BLE display or send it a short UTF-8 message."""

from __future__ import annotations

import argparse
import asyncio
from collections.abc import Sequence

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

DEFAULT_DEVICE_NAME = "Codex Display"
TEXT_CHARACTERISTIC_UUID = "7b1e0002-6d8f-4b7a-9c2d-4a7f1d2e3c40"
MAX_TEXT_BYTES = 120


async def scan(timeout: float) -> None:
    discovered: dict[str, tuple[object, object]] = {}

    def remember(device: object, advertisement: object) -> None:
        address = getattr(device, "address", repr(device))
        discovered[address] = (device, advertisement)

    scanner = BleakScanner(detection_callback=remember)
    async with scanner:
        await asyncio.sleep(timeout)

    if not discovered:
        print("No BLE devices found. Check macOS Bluetooth permission for your terminal.")
        return

    for address, (device, advertisement) in sorted(discovered.items()):
        name = getattr(advertisement, "local_name", None) or getattr(device, "name", None)
        rssi = getattr(advertisement, "rssi", "?")
        marker = "  <--- target" if name == DEFAULT_DEVICE_NAME else ""
        print(f"{name or '(unnamed)':24} {address}  RSSI {rssi}{marker}")


async def find_display(name: str, timeout: float):
    print(f"Scanning for '{name}' for up to {timeout:g} seconds...")
    device = await BleakScanner.find_device_by_filter(
        lambda candidate, advertisement: (
            advertisement.local_name == name or candidate.name == name
        ),
        timeout=timeout,
    )
    if device is None:
        raise RuntimeError(
            f"Could not find '{name}'. Confirm the OLED says ADVERTISING, then run scan."
        )
    return device


async def send_text(name: str, text: str, timeout: float) -> None:
    payload = text.encode("utf-8")
    if not payload:
        raise ValueError("Message cannot be empty")
    if len(payload) > MAX_TEXT_BYTES:
        raise ValueError(
            f"Message is {len(payload)} bytes; maximum is {MAX_TEXT_BYTES} bytes"
        )

    device = await find_display(name, timeout)
    print(f"Connecting to {device.name or name} ({device.address})...")
    async with BleakClient(device) as client:
        await client.write_gatt_char(TEXT_CHARACTERISTIC_UUID, payload, response=True)
        echoed = await client.read_gatt_char(TEXT_CHARACTERISTIC_UUID)
        print(f"Sent {len(payload)} bytes; board echoed: {echoed.decode('utf-8', 'replace')}")


def parse_args(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=10.0, help="BLE scan timeout")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help="BLE device name")
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("scan", help="list nearby BLE devices")
    send_parser = subcommands.add_parser("send", help="send text to the OLED")
    send_parser.add_argument("text", help="up to 120 UTF-8 bytes; ASCII displays best")
    return parser.parse_args(arguments)


async def async_main(arguments: argparse.Namespace) -> None:
    if arguments.command == "scan":
        await scan(arguments.timeout)
    else:
        await send_text(arguments.name, arguments.text, arguments.timeout)


def main() -> None:
    arguments = parse_args()
    try:
        asyncio.run(async_main(arguments))
    except (BleakError, RuntimeError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error


if __name__ == "__main__":
    main()
