#!/usr/bin/env python3
"""Exercise cartridge loading and model selection through the desktop entry point."""

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

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


def archive_executable():
    for name in ("7zz", "7z"):
        executable = shutil.which(name)
        if executable:
            return executable
    if sys.platform == "win32" and os.environ.get("ProgramFiles"):
        installed = Path(os.environ["ProgramFiles"]) / "7-Zip" / "7z.exe"
        if installed.is_file():
            return str(installed)
    return None


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
            assert result["window_visible"] and result["window_capture_saved"]
            window = Path(str(report) + ".window.png").read_bytes()
            assert window.startswith(b"\x89PNG\r\n\x1a\n")
            assert int.from_bytes(window[16:20], "big") == result["window_capture_width"] > 0
            assert int.from_bytes(window[20:24], "big") == result["window_capture_height"] > 0
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
        unsupported = root / "advance.gba"
        unsupported.write_bytes(plain.read_bytes())
        run(unsupported, expected=1)
        for requested in ("dmg", "dmg0", "mgb", "sgb", "sgb2"):
            run(color, "--model", requested, expected=1)
        for options in (("--model", "invalid"), ("--model", "cgb-e"),
                        ("--model", "cgb", "--smoke-test")):
            process = subprocess.run([str(binary), *options, str(root / "missing.gb")],
                                     env=environment, capture_output=True, timeout=8)
            assert process.returncode != 0
            assert b"core initialized" not in (process.stdout + process.stderr).lower()

        archive_tool = archive_executable()
        if archive_tool:
            archive = root / "-color archive.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as compressed:
                compressed.writestr("manual.gbc/", "")
                compressed.writestr("manual.txt", "Documentation")
                compressed.writestr("folder/Caf\u00e9 [A].GBC", color.read_bytes())
            assert run(archive, "--model", "auto")["hardware_model"] == "cgb"
            assert not (root / "folder").exists()

            seven_zip = root / "color.7z"
            subprocess.run([archive_tool, "a", str(seven_zip), str(color)], check=True,
                           capture_output=True, timeout=10)
            assert run(seven_zip, "--model", "auto")["hardware_model"] == "cgb"

            battery = make_rom(bytes((0x3E, 0x42, 0xEA, 0, 0xA0, 0x18, 0xFE)))
            battery[0x147] = 9  # ROM + RAM + battery.
            battery[0x149] = 2  # 8 KiB RAM.
            battery[0x14D] = (-sum(battery[0x134:0x14D]) - 25) & 255
            archive_battery = root / "game.zip"
            raw_save = root / "game.sav"
            # Distinct bytes expose accidental loading of the raw cartridge's save.
            raw_save.write_bytes(bytes([0xA5]) * 8192)
            with zipfile.ZipFile(archive_battery, "w", zipfile.ZIP_DEFLATED) as compressed:
                compressed.writestr("game.gb", battery)
            run(archive_battery)
            assert (root / "game.zip.sav").read_bytes() == bytes([0x42]) + bytes([0xFF]) * 8191
            assert raw_save.read_bytes() == bytes([0xA5]) * 8192

            for name, contents in (
                ("ambiguous", {"one.gb": plain.read_bytes(), "other.gbc": color.read_bytes()}),
                ("empty", {"notes.txt": b"No cartridge"}),
                ("large", {"too-big.gb": bytes(8 * 1024 * 1024 + 1)}),
            ):
                invalid = root / (name + ".zip")
                with zipfile.ZipFile(invalid, "w", zipfile.ZIP_DEFLATED) as compressed:
                    for member, data in contents.items():
                        compressed.writestr(member, data)
                run(invalid, expected=1)

            corrupt = root / "bad-crc.zip"
            with zipfile.ZipFile(corrupt, "w") as compressed:
                compressed.writestr("cart.gb", plain.read_bytes())
            raw = bytearray(corrupt.read_bytes())
            raw[30 + len("cart.gb") + 0x150] ^= 1
            corrupt.write_bytes(raw)
            run(corrupt, expected=1)

            link = root / "link.zip"
            with zipfile.ZipFile(link, "w") as compressed:
                member = zipfile.ZipInfo("symlink.gb")
                member.create_system = 3
                member.external_attr = stat.S_IFLNK << 16
                compressed.writestr(member, b"/" + b"elsewhere/" * 40 + b"game.gb")
            run(link, expected=1)
            print("Qt archive loading and rejection checks passed.")
        else:
            assert not os.environ.get("CUPIDGB_REQUIRE_ARCHIVE_TESTS"), "7-Zip is missing"
            print("Qt archive checks skipped because 7-Zip is unavailable.")
    print("Qt command-line model selection checks passed.")


if __name__ == "__main__":
    main()
