#!/usr/bin/env python3
# @file    test_qt_cli.py
# @brief   Validate desktop cartridge loading and command-line hardware selection.
# @author  cupidthecat
# @date    2026-11-16
#
# Copyright (C) 2026 cupidthecat
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://gnu.org>.
"""Exercise cartridge loading and model selection through the desktop entry point."""

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import struct
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


def extended_cartridge():
    # Read the first bank beyond 8 MiB and write its marker into battery RAM.
    program = bytes((0xF3, 0x3E, 0, 0xEA, 0, 0, 0x3E, 2, 0xEA, 1, 0,
                     0xFA, 0, 0x40, 0x47, 0x3E, 3, 0xEA, 3, 0, 0x78,
                     0xEA, 0, 0xA0, 0x18, 0xFE))
    rom = make_rom(b"", color=True)
    rom.extend(bytes(0x804000 - len(rom)))
    rom[0x102] = 0x60  # The extension header occupies 0x150 through 0x153.
    rom[0x147:0x14B] = bytes((0xBC, 8, 0xC1, 0x65))
    rom[0x152:0x154] = bytes((1, 8))  # One RAM bank with battery storage.
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
    rom[0x160:0x160 + len(program)] = program
    rom[0x800000] = 0x7B
    return rom


def large_ram_cartridge(color):
    # Each launch advances three independent bytes across the former SRAM limit.
    program = bytearray((0xF3, 0x3E, 3, 0xEA, 3, 0))
    for bank, address, increment in ((15, 0xBFFF, 0x12), (16, 0xA000, 0x35),
                                     (255, 0xBFFF, 0x57)):
        program.extend((0x3E, bank, 0xEA, 2, 0, 0xFA, address & 255, address >> 8,
                        0xC6, increment, 0xEA, address & 255, address >> 8))
    program.extend((0x18, 0xFE))
    rom = make_rom(b"", color=color)
    rom[0x102] = 0x60
    rom[0x147:0x14B] = bytes((0xBC, 0, 0xC1, 0x65))
    rom[0x152:0x154] = bytes((9, 8))  # 2 MiB SRAM with battery storage.
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
    rom[0x160:0x160 + len(program)] = program
    return rom


def oversized_archive(path):
    # The declared size must be rejected before decompression, without allocating a GiB.
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as compressed:
        compressed.writestr("too-big.gb", cartridge())
    raw = bytearray(path.read_bytes())
    size = 1024 * 1024 * 1024 + 1
    struct.pack_into("<I", raw, 22, size)
    directory = raw.index(b"PK\x01\x02")
    struct.pack_into("<I", raw, directory + 24, size)
    path.write_bytes(raw)


def check_selection(run, binary, environment, root, plain, licensed, color):
    def seed_options(values):
        return tuple(argument for value in values for argument in ("--power-on-seed", value))

    for duration in ("0", "-1", "nan", "inf", "-inf", "1e100", "86400.01", "invalid"):
        run(plain, expected=1, duration=duration, reject_before_session=True)

    for values, mode, seed in (
        (("0",), "fixed", "0000000000000000"),
        (("1", "0"), "fixed", "0000000000000000"),
        (("0", "auto"), "auto", None),
        (("auto", "0"), "fixed", "0000000000000000"),
        (("0xabc", "auto", "08"), "fixed", "0000000000000008"),
    ):
        result = run(plain, *seed_options(values))
        assert result["power_on_seed_mode"] == mode, (values, result)
        realized = result["power_on_seed_hex"]
        assert realized == f"{int(realized, 16):016x}", (values, result)
        if seed is not None:
            assert realized == seed, (values, result)
        assert result["power_on_seed_scope"] == "realized cold generation", result
        assert result["power_on_seed_is_state_ram_provenance"] is False, result
    # Every occurrence must be valid even when a later value would replace it.
    for values in (("18446744073709551616", "0"), ("+1", "auto"),
                   ("0", "0x10000000000000000"), ("", "auto"),
                   ("0x", "1"), ("auto", "1_000"), ("0", "-1", "1")):
        run(plain, *seed_options(values), expected=1)

    for sources, expected in ((("system",), "system"), (("generated",), "generated"),
                              (("system", "generated"), "generated"),
                              (("generated", "system"), "system")):
        options = tuple(argument for source in sources for argument in ("--camera-source", source))
        result = run(plain, *options)
        assert result["camera_source"] == expected, result
    for sources in (("invalid",), ("invalid", "generated"), ("generated", "")):
        options = tuple(argument for source in sources for argument in ("--camera-source", source))
        run(plain, *options, expected=1, reject_before_session=True)
    assert run(plain)["camera_source"] == "system"
    camera = root / "generated-camera.gb"
    camera_rom = make_rom(bytes((0x3E, 0x10, 0xEA, 0, 0x40,
                                 0x3E, 1, 0xEA, 0, 0xA0, 0x18, 0xFE)))
    camera_rom[0x147] = 0xFC
    camera_rom[0x149] = 4
    camera_rom[0x14D] = (-sum(camera_rom[0x134:0x14D]) - 25) & 255
    camera.write_bytes(camera_rom)
    result = run(camera, "--camera-source", "generated", duration="0.12")
    assert result["camera_source"] == "generated", result
    assert result["camera_capture_count"] == 1, result
    assert result["camera_host_provider_registered"] is False, result

    for requested, actual in (("dmg", "dmgabc"), ("dmgabc", "dmgabc"),
                              ("dmg0", "dmg0"), ("mgb", "mgb"),
                              ("cgb", "cgb"), ("cgb-e", "cgb"),
                              ("cgb-0", "cgb-0"), ("cgb-a", "cgb-a"), ("cgb-b", "cgb-b"),
                              ("cgb-c", "cgb-c"), ("cgb-d", "cgb-d"),
                              ("sgb", "sgb"), ("sgb2", "sgb2"),
                              ("sgbpal", "sgbpal"), ("sgb-external", "sgb-external"),
                              ("sgb-pal-external", "sgb-pal-external"),
                              ("sgb2-external", "sgb2-external")):
        result = run(plain, "--model", requested)
        assert result["requested_model"] == actual, result
        assert result["hardware_model"] == actual, result
        assert result["base_clock_hz"] == {"sgb": 4295454, "sgbpal": 4256274,
                                           "sgb-external": 4295454,
                                           "sgb-pal-external": 4256274}.get(actual, 4194304)
    result = run(licensed, "--model", "auto")
    assert result["requested_model"] == "auto" and result["hardware_model"] == "cgb"
    result = run(color, "--model", "auto")
    assert result["requested_model"] == "auto" and result["hardware_model"] == "cgb"
    result = run(plain)
    assert result["requested_model"] is None
    assert result["post_warmup"]["reason"] == "run_does_not_exceed_warmup"
    # Exercise the independent five-second warmup cutoff and the final
    # deadline on the same host clock. A blocked event loop may miss the
    # warmup window, but must report that rather than inventing a sample.
    warmed = run(plain, duration="5.25")
    post_warmup = warmed["post_warmup"]
    assert post_warmup["warmup_target_seconds"] == 5
    if post_warmup["available"]:
        assert 5 <= post_warmup["sample_start_elapsed_seconds"] < warmed["elapsed_seconds"]
        assert post_warmup["sample_end_elapsed_seconds"] == warmed["elapsed_seconds"]
        assert post_warmup["elapsed_seconds"] > 0
    else:
        assert post_warmup["reason"] == "warmup_sample_unavailable"
    for requested in ("dmg", "dmg0", "mgb", "sgb", "sgb2", "sgbpal", "sgb-external",
                      "sgb-pal-external", "sgb2-external"):
        run(color, "--model", requested, expected=1)
    for options in (("--camera-source", "generated", "--smoke-test"),
                    ("--model", "invalid"), ("--model", "cgb-f"),
                    ("--model", "cgb", "--smoke-test"),
                    ("--smoke-workflow", "storage"),
                    ("--smoke-test", "--smoke-workflow", "invalid"),
                    ("--smoke-test", "--smoke-workflow", "storage", "--audio-check")):
        process = subprocess.run([str(binary), *options, str(root / "missing.gb")],
                                 env=environment, capture_output=True, timeout=8)
        assert process.returncode != 0
        assert b"core initialized" not in (process.stdout + process.stderr).lower()
    print("Qt command-line model selection checks passed.")


def check_cartridge(run, root, plain, color):
    extended = root / "extended-tpp1.gbc"
    extended.write_bytes(extended_cartridge())
    run(extended, "--model", "cgb")
    extended_save = extended.with_suffix(".sav").read_bytes()
    assert len(extended_save) == 8192 + 20
    assert extended_save[:8192] == bytes((0x7B,)) + bytes((0xFF,)) * 8191
    assert extended_save[8192:8198] == b"TPP1\x01\x00"
    for is_color in (False, True):
        large = root / ("tpp1-2mib-cgb.gbc" if is_color else "tpp1-2mib-dmg.gb")
        large.write_bytes(large_ram_cartridge(is_color))
        expected_ram = bytearray((0xFF,)) * (2 * 1024 * 1024)
        for _ in range(2):
            run(large, "--model", "cgb" if is_color else "dmg")
            for offset, increment in ((0x1FFFF, 0x12), (0x20000, 0x35),
                                      (0x1FFFFF, 0x57)):
                expected_ram[offset] = (expected_ram[offset] + increment) & 255
            persisted = large.with_suffix(".sav").read_bytes()
            assert len(persisted) == len(expected_ram) + 20
            assert persisted[:len(expected_ram)] == expected_ram
            assert persisted[len(expected_ram):len(expected_ram) + 6] == b"TPP1\x01\x00"
        large.with_suffix(".sav").write_bytes(persisted + b"\x00")
        run(large, expected=1, error_contains=b"size does not match cartridge RAM")

    # The selected capacity limit still permits supported older battery layouts.
    for trailer_size in (44, 48):
        rtc_rom = cartridge()
        rtc_rom[0x147] = 0x10
        rtc_rom[0x149] = 2
        rtc_rom[0x14D] = (-sum(rtc_rom[0x134:0x14D]) - 25) & 255
        rtc_path = root / f"rtc-layout-{trailer_size}.gb"
        rtc_path.write_bytes(rtc_rom)
        rtc_path.with_suffix(".sav").write_bytes(bytes((0xA5,)) * 8192 + bytes(trailer_size))
        run(rtc_path, "--model", "dmg")
        restored = rtc_path.with_suffix(".sav").read_bytes()
        assert len(restored) == 8192 + 48 and restored[:8192] == bytes((0xA5,)) * 8192
    motion_rom = cartridge()
    motion_rom[0x147] = 0x22
    motion_rom[0x149] = 2
    motion_rom[0x14D] = (-sum(motion_rom[0x134:0x14D]) - 25) & 255
    motion_path = root / "motion-legacy-ram.gb"
    motion_path.write_bytes(motion_rom)
    motion_path.with_suffix(".sav").write_bytes(bytes(range(256)) + bytes(8192 - 256))
    run(motion_path, "--model", "dmg")
    assert motion_path.with_suffix(".sav").read_bytes() == bytes(range(256))
    unsupported = root / "advance.gba"
    unsupported.write_bytes(plain.read_bytes())
    run(unsupported, expected=1)

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

        extended_zip = root / "extended-tpp1.zip"
        with zipfile.ZipFile(extended_zip, "w", zipfile.ZIP_DEFLATED) as compressed:
            compressed.writestr("cartridge.gbc", extended.read_bytes())
        run(extended_zip, "--model", "cgb")
        archived_save = root / "extended-tpp1.zip.sav"
        archived_battery = archived_save.read_bytes()
        assert len(archived_battery) == 8192 + 20
        assert archived_battery[:8192] == extended_save[:8192]
        assert archived_battery[8192:8198] == b"TPP1\x01\x00"
        assert extended.with_suffix(".sav").read_bytes() == extended_save

        # An archive owns a separate save from the raw cartridge with the same stem.
        large_zip = root / "large-sram-tpp1.zip"
        large_archive_save = root / "large-sram-tpp1.zip.sav"
        large_raw_save = root / "large-sram-tpp1.sav"
        raw_large_bytes = bytes((0xA5,)) * (2 * 1024 * 1024)
        large_raw_save.write_bytes(raw_large_bytes)
        with zipfile.ZipFile(large_zip, "w", zipfile.ZIP_DEFLATED) as compressed:
            compressed.writestr("cartridge.gbc", large_ram_cartridge(True))
        expected_archive_ram = bytearray((0xFF,)) * (2 * 1024 * 1024)
        for _ in range(2):
            run(large_zip, "--model", "cgb")
            for offset, increment in ((0x1FFFF, 0x12), (0x20000, 0x35),
                                      (0x1FFFFF, 0x57)):
                expected_archive_ram[offset] = (expected_archive_ram[offset] + increment) & 255
            archived_large = large_archive_save.read_bytes()
            assert len(archived_large) == len(expected_archive_ram) + 20
            assert archived_large[:len(expected_archive_ram)] == expected_archive_ram
            assert archived_large[len(expected_archive_ram):len(expected_archive_ram) + 6] == \
                b"TPP1\x01\x00"
            assert large_raw_save.read_bytes() == raw_large_bytes

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
        ):
            invalid = root / (name + ".zip")
            with zipfile.ZipFile(invalid, "w", zipfile.ZIP_DEFLATED) as compressed:
                for member, data in contents.items():
                    compressed.writestr(member, data)
            run(invalid, expected=1)

        too_large = root / "too-large.zip"
        oversized_archive(too_large)
        run(too_large, expected=1,
            error_contains=b"The archived Game Boy file has an invalid size.")

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
    print("Qt cartridge and battery checks passed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="CupidGB frontend executable")
    parser.add_argument("--group", choices=("all", "selection", "cartridge"), default="all",
                        help="Run every check or one independently bounded workflow.")
    arguments = parser.parse_args()
    binary = Path(arguments.binary).resolve()
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

        def run(rom, *options, expected=0, error_contains=None, duration="0.06",
                reject_before_session=False):
            nonlocal ordinal
            ordinal += 1
            report = root / ("report-" + str(ordinal) + ".json")
            command = [str(binary), "--run-seconds", duration, "--report", str(report),
                       *options, str(rom)]
            process = subprocess.run(command, env=environment, capture_output=True,
                                     timeout=12 if expected else max(12, float(duration) + 12))
            assert process.returncode == expected, (command, process.returncode,
                                                     process.stdout, process.stderr)
            if expected:
                assert not report.exists(), report
                if error_contains is not None:
                    assert error_contains in process.stderr, process.stderr
                if reject_before_session:
                    output = (process.stdout + process.stderr).lower()
                    assert b"core initialized" not in output, output
                    assert b"loaded game boy rom" not in output, output
                return None
            result = json.loads(report.read_text(encoding="utf-8"))
            assert not result["execution_failed"] and result["screenshot_saved"]
            expected_source = "linux_monotonic_raw" if sys.platform == "linux" else "qt_elapsed_timer"
            assert result["host_clock"] == expected_source, result
            assert result["requested_duration_seconds"] == float(duration), result
            assert result["elapsed_seconds"] >= result["requested_duration_seconds"], result
            assert Path(str(report) + ".png").exists()
            if result["hardware_model"].endswith("-external"):
                image = Path(str(report) + ".png").read_bytes()
                assert image.startswith(b"\x89PNG\r\n\x1a\n")
                assert int.from_bytes(image[16:20], "big") == 160
                assert int.from_bytes(image[20:24], "big") == 144
            assert result["window_visible"] and result["window_capture_saved"]
            window = Path(str(report) + ".window.png").read_bytes()
            assert window.startswith(b"\x89PNG\r\n\x1a\n")
            assert int.from_bytes(window[16:20], "big") == result["window_capture_width"] > 0
            assert int.from_bytes(window[20:24], "big") == result["window_capture_height"] > 0
            return result

        if arguments.group in ("all", "selection"):
            check_selection(run, binary, environment, root, plain, licensed, color)
        if arguments.group in ("all", "cartridge"):
            check_cartridge(run, root, plain, color)


if __name__ == "__main__":
    main()
