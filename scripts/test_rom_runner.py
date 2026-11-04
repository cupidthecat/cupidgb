#!/usr/bin/env python3
"""Check runner verdicts and capture files with generated, disposable ROMs."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def make_rom(program):
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes((0x00, 0xC3, 0x50, 0x01))
    rom[0x134:0x13C] = b"RUNTEST\0"
    checksum = 0
    for byte in rom[0x134:0x14D]:
        checksum = (checksum - byte - 1) & 255
    rom[0x14D] = checksum
    rom[0x150:0x150 + len(program)] = program
    return rom


def serial_program(text):
    program = bytearray((0xF3, 0xAF, 0xE0, 0x40))  # DI; XOR A; LDH (LCDC),A
    for byte in text:
        program.extend((0x3E, byte, 0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02,
                        0xF0, 0x02, 0xCB, 0x7F, 0x20, 0xFA))
    program.extend((0x76, 0x18, 0xFD))
    return program


def memory_rom(code, *, running=True, signature=True):
    program = bytearray((0xF3, 0x3E, 0x0A, 0xEA, 0, 0))
    # Signature initialization precedes running status in the actual protocol.
    for address, value in ((0xA001, 0xDE if signature else 0), (0xA002, 0xB0), (0xA003, 0x61)):
        program.extend((0x3E, value, 0xEA, address & 255, address >> 8))
    if running:
        program.extend((0x3E, 0x80, 0xEA, 0, 0xA0))
    program.extend((0x3E, code, 0xEA, 0, 0xA0, 0x76, 0x18, 0xFD))
    rom = make_rom(program)
    rom[0x147] = 2  # MBC1 with RAM
    rom[0x149] = 2  # 8 KiB RAM
    checksum = 0
    for byte in rom[0x134:0x14D]:
        checksum = (checksum - byte - 1) & 255
    rom[0x14D] = checksum
    return rom


def run(runner, rom, *options, expected_code=0):
    completed = subprocess.run([str(runner), *options, str(rom)], capture_output=True, timeout=15)
    if completed.returncode != expected_code:
        raise AssertionError((completed.returncode, expected_code, completed.stdout, completed.stderr))
    records = [json.loads(line.removeprefix("CUPIDGB_RESULT "))
               for line in completed.stdout.decode("utf-8", errors="replace").splitlines()
               if line.startswith("CUPIDGB_RESULT ")]
    if len(records) != 1:
        raise AssertionError(completed.stdout)
    return records[0]


def main():
    runner = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="cupidgb-runner-") as directory:
        root = Path(directory)
        rom = root / "generated.gb"
        image = root / "frame.ppm"
        audio = root / "audio.pcm"
        rom.write_bytes(make_rom(serial_program(b"Passed\n")))
        passed = run(runner, rom, "--protocol", "serial", "--frames", "20",
                     "--image", str(image), "--audio", str(audio))
        assert passed["status"] == "pass"
        assert passed["base_cycles"] > 0
        assert passed["audio_frames"] > 0
        assert image.read_bytes().startswith(b"P6\n160 144\n255\n")
        assert audio.stat().st_size == passed["audio_frames"] * 4
        again = run(runner, rom, "--protocol", "serial", "--frames", "20")
        for field in ("base_cycles", "audio_frames", "audio_crc32", "image_crc32"):
            assert again[field] == passed[field], field
        rom.write_bytes(make_rom(serial_program(b"Failed\n")))
        assert run(runner, rom, "--protocol", "serial", expected_code=1)["status"] == "fail"
        rom.write_bytes(make_rom(bytes((0x06, 3, 0x0E, 5, 0x16, 8, 0x1E, 13,
                                       0x26, 21, 0x2E, 34, 0x40, 0x18, 0xFD))))
        assert run(runner, rom, "--protocol", "register")["status"] == "pass"
        # Identical registers alone must not terminate a plain measurement run.
        assert run(runner, rom, "--frames", "1")["status"] == "bounded"
        rom.write_bytes(make_rom(bytes((0x06, 0x42, 0x0E, 0x42, 0x16, 0x42,
                                       0x1E, 0x42, 0x26, 0x42, 0x2E, 0x42, 0x40))))
        assert run(runner, rom, "--protocol", "register", expected_code=1)["status"] == "fail"
        rom.write_bytes(make_rom(bytes((0xF3, 0xAF, 0xE0, 0x40, 0x76, 0x18, 0xFD))))
        bounded = run(runner, rom, "--frames", "2")
        assert bounded["status"] == "bounded"
        assert bounded["vblank_frames"] == 0
        assert bounded["base_cycles"] >= 140448
        assert run(runner, rom, "--frames", "2", "--protocol", "serial",
                   expected_code=3)["status"] == "bounded"
        assert run(runner, rom, "--max-steps", "1")["status"] == "step_limit"
        assert subprocess.run([str(runner), "--frames", "0", str(rom)],
                              capture_output=True).returncode == 2
        for code, expected, exit_code in ((0, "pass", 0), (7, "fail", 1),
                                          (0x81, "reset_required", 3)):
            rom.write_bytes(memory_rom(code))
            record = run(runner, rom, "--protocol", "memory", expected_code=exit_code)
            assert record["status"] == expected
            assert record["memory_result_code"] == code
        for options in ({"running": False}, {"signature": False}):
            rom.write_bytes(memory_rom(0, **options))
            assert run(runner, rom, "--protocol", "memory", "--frames", "1",
                       expected_code=3)["status"] == "bounded"
        rom.write_bytes(make_rom(bytes((0xF3, 0xAF, 0xE0, 0x40, 0x76, 0x18, 0xFD))))
        suite = Path(__file__).with_name("run_rom_suite.py")
        report = root / "report.json"
        suite_run = subprocess.run([sys.executable, str(suite), str(runner), str(rom),
                                   "--frames", "1", "--output", str(report)],
                                  capture_output=True, timeout=15)
        assert suite_run.returncode == 1
        assert json.loads(report.read_text())["counts"] == {"bounded": 1}, report.read_text()
        trace = root / "input.trace"
        trace.write_text("0 ff\n10 7f\n12 ff\n")
        run(runner, rom, "--input-trace", str(trace), "--frames", "20")
        trace.write_text("10 ff\n9 ff\n")
        assert subprocess.run([str(runner), "--input-trace", str(trace), str(rom)],
                              capture_output=True).returncode == 2
    print("Runner integration checks passed.")


if __name__ == "__main__":
    main()
