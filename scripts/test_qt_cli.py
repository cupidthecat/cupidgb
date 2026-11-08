#!/usr/bin/env python3
"""Exercise session model selection through the desktop command-line entry point."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_rom_runner import make_rom


def cartridge(color=0, licensed=False):
    rom = make_rom(bytes((0x18, 0xFE)))
    rom[0x143] = color
    rom[0x14B] = 1 if licensed else 0
    checksum = 0
    for byte in rom[0x134:0x14D]:
        checksum = (checksum - byte - 1) & 255
    rom[0x14D] = checksum
    return rom


def main():
    binary = Path(sys.argv[1]).resolve()
    environment = os.environ | {"QT_QPA_PLATFORM": "offscreen",
                                "QT_FORCE_STDERR_LOGGING": "1"}
    with tempfile.TemporaryDirectory(prefix="cupidgb-cli-") as temporary:
        root = Path(temporary)
        plain = root / "plain.gb"
        licensed = root / "licensed.gb"
        color = root / "color-only.gbc"
        plain.write_bytes(cartridge())
        licensed.write_bytes(cartridge(licensed=True))
        color.write_bytes(cartridge(color=0xC0))
        ordinal = 0

        def run(rom, *options, expected=0):
            nonlocal ordinal
            ordinal += 1
            report = root / ("report-" + str(ordinal) + ".json")
            command = [str(binary), "--run-seconds", "0.06", "--report", str(report),
                       *options, str(rom)]
            process = subprocess.run(command, env=environment, capture_output=True, timeout=12)
            assert process.returncode == expected, (command, process.returncode,
                                                     process.stdout, process.stderr)
            if expected:
                assert not report.exists(), report
                return None
            result = json.loads(report.read_text(encoding="utf-8"))
            assert not result["execution_failed"] and result["screenshot_saved"]
            assert result["elapsed_seconds"] > 0
            assert Path(str(report) + ".png").exists()
            return result

        for requested, actual in (("dmg", "dmgabc"), ("dmgabc", "dmgabc"),
                                  ("dmg0", "dmg0"), ("mgb", "mgb"),
                                  ("cgb", "cgb"), ("sgb", "sgb"), ("sgb2", "sgb2")):
            result = run(plain, "--model", requested)
            assert result["requested_model"] == actual, result
            assert result["hardware_model"] == actual, result
            assert result["base_clock_hz"] == (4295454 if actual == "sgb" else 4194304)
        result = run(licensed, "--model", "auto")
        assert result["requested_model"] == "auto" and result["hardware_model"] == "cgb"
        result = run(color, "--model", "auto")
        assert result["requested_model"] == "auto" and result["hardware_model"] == "cgb"
        result = run(plain)
        assert result["requested_model"] is None
        for requested in ("dmg", "dmg0", "mgb", "sgb", "sgb2"):
            run(color, "--model", requested, expected=1)
        for options in (("--model", "invalid"), ("--model", "cgb-e"),
                        ("--model", "cgb", "--smoke-test")):
            process = subprocess.run([str(binary), *options, str(root / "missing.gb")],
                                     env=environment, capture_output=True, timeout=8)
            assert process.returncode != 0
            assert b"core initialized" not in (process.stdout + process.stderr).lower()
    print("Qt command-line model selection checks passed.")


if __name__ == "__main__":
    main()
