#!/usr/bin/env python3
"""Regression tests for the interactive serial-terminal defaults."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

from serial.tools import miniterm


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT))

from tools.serial_terminal import apply_terminal_defaults  # noqa: E402


class SerialTerminalDefaultsTest(unittest.TestCase):
    def test_pyserial_default_filter_reproduces_visible_escape_glyph(self) -> None:
        rendered = miniterm.NoTerminal().rx("\x1b[1;32mcanon> ")
        self.assertEqual(rendered, "␛[1;32mcanon> ")

    def test_defaults_preserve_ansi_terminal_sequences(self) -> None:
        arguments = apply_terminal_defaults([])
        self.assertIn("--filter", arguments)
        self.assertEqual(arguments[arguments.index("--filter") + 1], "direct")

    def test_explicit_filter_is_not_overridden(self) -> None:
        arguments = apply_terminal_defaults(["--filter", "printable"])
        self.assertEqual(arguments.count("--filter"), 1)
        self.assertIn("printable", arguments)

    def test_eol_and_filter_defaults_are_independent(self) -> None:
        arguments = apply_terminal_defaults(["--eol=LF"])
        self.assertIn("--eol=LF", arguments)
        self.assertIn("direct", arguments)


if __name__ == "__main__":
    unittest.main()
