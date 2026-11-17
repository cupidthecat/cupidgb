#!/usr/bin/env python3
# @file    test_rom_runner.py
# @brief   Validate headless execution and capture contracts with generated cartridges.
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
"""Check runner verdicts and capture files with generated, disposable ROMs."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


FIXED_POWER_ON_SEED = "0"


def make_rom(program, *, color=False):
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes((0x00, 0xC3, 0x50, 0x01))
    rom[0x134:0x13C] = b"RUNTEST\0"
    rom[0x143] = 0x80 if color else 0
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
    if "--power-on-seed" not in options:
        options = ("--power-on-seed", FIXED_POWER_ON_SEED, *options)
    completed = subprocess.run([str(runner), *options, str(rom)], capture_output=True, timeout=15)
    if completed.returncode != expected_code:
        raise AssertionError((completed.returncode, expected_code, completed.stdout, completed.stderr))
    records = [json.loads(line.removeprefix("CUPIDGB_RESULT "))
               for line in completed.stdout.decode("utf-8", errors="replace").splitlines()
               if line.startswith("CUPIDGB_RESULT ")]
    if len(records) != 1:
        raise AssertionError(completed.stdout)
    return records[0]


def stable_result(record):
    return {key: value for key, value in record.items() if key != "host_seconds"}


def memory_serial_program(address, *, bank=None):
    program = bytearray((0xF3, 0xAF, 0xE0, 0x40))  # DI; LCD off
    if bank is not None:
        program.extend((0x3E, bank, 0xE0, 0x70))
    program.extend((0x21, address & 255, address >> 8, 0x06, 16))
    # Send sixteen untouched bytes; wait for each transfer before the next read.
    program.extend((0x2A, 0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02,
                    0xF0, 0x02, 0xCB, 0x7F, 0x20, 0xFA, 0x05, 0x20, 0xF0,
                    0x76, 0x18, 0xFD))
    return program


def memory_serial_sample(runner, rom, *options):
    # Leave the startup option absent when testing the automatic default.
    completed = subprocess.run([str(runner), "--model", "cgb", "--frames", "2",
                                "--serial-output", *options, str(rom)],
                               capture_output=True, timeout=15)
    assert completed.returncode == 0, (completed.stdout, completed.stderr)
    separator = b"\r\nCUPIDGB_RESULT " if sys.platform == "win32" else b"\nCUPIDGB_RESULT "
    serial, result = completed.stdout.rsplit(separator, 1)
    if sys.platform == "win32":
        # The runner's diagnostic stream uses the Windows CRT's text mode.
        serial = serial.replace(b"\r\n", b"\n")
    record = json.loads(result)
    assert record["status"] == "bounded" and record["serial_bytes"] == 16, record
    # ROM-loading diagnostics precede the serial bytes counted by the result.
    assert len(serial) >= record["serial_bytes"], serial
    serial = serial[-record["serial_bytes"]:]
    assert len(serial) == 16, serial
    seed_hex = record["power_on_seed_hex"]
    assert seed_hex == f"{int(seed_hex, 16):016x}", seed_hex
    return serial, record


def startup_checks(runner, root):
    rom = root / "startup.gbc"
    rom.write_bytes(make_rom(bytes((0xF3, 0x76, 0x18, 0xFD)), color=True))
    valid_seeds = ("0", "08", "0008", "0x123456789abcdef0", "0XABCDEF0000000001",
                   "18446744073709551615", "0xffffffffffffffff")
    for seed in valid_seeds:
        first = run(runner, rom, "--power-on-seed", seed, "--frames", "1")
        second = run(runner, rom, "--power-on-seed", seed, "--frames", "1")
        expected = int(seed, 16 if seed.lower().startswith("0x") else 10)
        assert first["power_on_seed_hex"] == f"{expected:016x}", first
        assert stable_result(first) == stable_result(second), seed
    overridden = run(runner, rom, "--power-on-seed", "auto", "--power-on-seed", "0",
                     "--frames", "1")
    assert overridden["power_on_seed_hex"] == "0000000000000000", overridden

    suite = Path(__file__).with_name("run_rom_suite.py")
    invalid_report = root / "invalid-seed-report.json"
    invalid_seeds = ("", "-1", "+1", " 1", "1 ", "\t1", "0x", "0X", "0x-1", "0x+1",
                     "12x", "0b1", "1_000", "AUTO", "18446744073709551616",
                     "18446744073709551615999", "0x10000000000000000",
                     "0xfffffffffffffffff")
    for seed in invalid_seeds:
        invalid = subprocess.run([str(runner), "--power-on-seed", seed, str(rom)],
                                 capture_output=True, timeout=15)
        assert invalid.returncode == 2 and b"CUPIDGB_RESULT " not in invalid.stdout, seed
        invalid_suite = subprocess.run(
            [sys.executable, str(suite), str(runner), str(rom),
             "--output", str(invalid_report), "--power-on-seed", seed],
            capture_output=True, timeout=15)
        assert invalid_suite.returncode == 2, (seed, invalid_suite.stderr)
        assert not invalid_report.exists(), seed
    missing = subprocess.run([str(runner), str(rom), "--power-on-seed"],
                             capture_output=True, timeout=15)
    assert missing.returncode == 2 and b"CUPIDGB_RESULT " not in missing.stdout
    for option in ("--frames", "--max-steps"):
        for value in ("0", "-1", "10000000001", "18446744073709551616"):
            invalid = subprocess.run([str(runner), option, value, str(rom)],
                                     capture_output=True, timeout=15)
            assert invalid.returncode == 2, (option, value, invalid.stderr)
    largest_frames = run(runner, rom, "--frames", "10000000000", "--max-steps", "1")
    largest_steps = run(runner, rom, "--frames", "1", "--max-steps", "10000000000")
    assert largest_frames["status"] == "step_limit" and largest_steps["status"] == "bounded"

    rom.write_bytes(make_rom(memory_serial_program(0xC000), color=True))
    observed = [memory_serial_sample(runner, rom, "--power-on-seed", seed)
                for seed in ("0", "0", "1")]
    assert observed[0][0] == observed[1][0] and observed[0][0] != observed[2][0]
    assert any(observed[0][0]), "A fixed zero seed must still generate power-on noise."
    assert stable_result(observed[0][1]) == stable_result(observed[1][1])
    for options in ((), ("--power-on-seed", "auto")):
        automatic, automatic_record = memory_serial_sample(runner, rom, *options)
        replay, replay_record = memory_serial_sample(
            runner, rom, "--power-on-seed", "0x" + automatic_record["power_on_seed_hex"])
        assert automatic == replay
        assert stable_result(automatic_record) == stable_result(replay_record)

    # HLE clears CGB WRAM bank 2. Fresh firmware must see this generation's
    # original cold bytes instead of inheriting that post-boot memory edit.
    program = memory_serial_program(0xD000, bank=2)
    rom.write_bytes(make_rom(program, color=True))
    hle, _ = memory_serial_sample(runner, rom, "--power-on-seed", "0")
    assert hle == bytes(16), hle
    boot = root / "startup-boot.bin"
    firmware = bytearray(0x900)
    firmware[:len(program)] = program
    boot.write_bytes(firmware)
    cold, cold_record = memory_serial_sample(runner, rom, "--power-on-seed", "0",
                                             "--boot", str(boot))
    repeated, repeated_record = memory_serial_sample(runner, rom, "--power-on-seed", "0",
                                                     "--boot", str(boot))
    assert any(cold) and cold == repeated
    assert cold_record["power_on_seed_hex"] == "0000000000000000"
    assert stable_result(cold_record) == stable_result(repeated_record)
    automatic, automatic_record = memory_serial_sample(runner, rom, "--boot", str(boot))
    replay, replay_record = memory_serial_sample(
        runner, rom, "--boot", str(boot),
        "--power-on-seed", "0x" + automatic_record["power_on_seed_hex"])
    assert automatic == replay
    assert stable_result(automatic_record) == stable_result(replay_record)

    report = root / "startup-report.json"
    for seed in ("0", "auto", "0XFFFFFFFFFFFFFFFF"):
        completed = subprocess.run(
            [sys.executable, str(suite), str(runner), str(rom), "--model", "cgb",
             "--protocol", "none", "--frames", "2", "--boot", str(boot),
             "--power-on-seed", seed, "--output", str(report)],
            capture_output=True, timeout=15)
        assert completed.returncode == 1, completed.stderr
        result = json.loads(report.read_text())
        assert result["counts"] == {"bounded": 1}, result
        suite_record = result["results"][0]
        replay_seed = "0x" + suite_record["power_on_seed_hex"] if seed == "auto" else seed
        _, direct = memory_serial_sample(runner, rom, "--boot", str(boot),
                                          "--power-on-seed", replay_seed)
        for field, value in stable_result(direct).items():
            assert suite_record[field] == value, (seed, field, suite_record, direct)


def illegal_opcode_checks(runner, root):
    rom = root / "illegal.gb"
    opcodes = (0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD)
    for model in ("dmg", "cgb"):
        for opcode in opcodes:
            rom.write_bytes(make_rom(bytes((opcode,)), color=model == "cgb"))
            record = run(runner, rom, "--model", model, expected_code=1)
            assert record["status"] == "illegal_opcode", record
            assert record["illegal_opcode"] == opcode and record["illegal_address"] == 0x150
            assert record["steps"] == 3 and record["base_cycles"] == 24, record
    for protocol in ("serial", "register", "fibonacci", "memory"):
        rom.write_bytes(make_rom(bytes((0xD3,))))
        record = run(runner, rom, "--protocol", protocol, expected_code=1)
        assert record["status"] == "illegal_opcode", record
    # A CB operand with the same byte value remains a valid instruction.
    rom.write_bytes(make_rom(bytes((0xCB, 0xD3, 0x76))))
    assert run(runner, rom, "--frames", "1")["status"] == "bounded"

    # At 96 base cycles the last fetch emits the first PCM sample. Both the
    # completed fetch and its output must survive a simultaneous step limit.
    audio = root / "illegal.pcm"
    rom.write_bytes(make_rom(bytes(19)))
    control = run(runner, rom, "--max-steps", "21")
    assert control["status"] == "step_limit"
    assert control["illegal_opcode"] == -1 and control["illegal_address"] == -1
    rom.write_bytes(make_rom(bytes(18) + bytes((0xD3,))))
    final = run(runner, rom, "--max-steps", "21", "--audio", str(audio), expected_code=1)
    assert final["status"] == "illegal_opcode" and final["illegal_address"] == 0x162
    assert final["audio_frames"] == 1 and audio.stat().st_size == 4
    for field in ("steps", "base_cycles", "presentation_events", "audio_frames", "audio_crc32"):
        assert final[field] == control[field], (field, final, control)

    # Complete an LCD-off presentation on the illegal fetch and frame deadline.
    program = bytes((0xF3, 0xAF, 0xE0, 0x40)) + bytes(17545)
    rom.write_bytes(make_rom(program + bytes((0,))))
    control = run(runner, rom, "--frames", "1")
    rom.write_bytes(make_rom(program + bytes((0xD3,))))
    final = run(runner, rom, "--frames", "1", expected_code=1)
    assert control["status"] == "bounded" and final["status"] == "illegal_opcode"
    assert final["base_cycles"] == 70224 and final["presentation_events"] == 1
    assert final["vblank_frames"] == 0 and final["illegal_address"] == 0x150 + len(program)
    for field in ("steps", "base_cycles", "presentation_events", "audio_frames", "audio_crc32"):
        assert final[field] == control[field], (field, final, control)

    # HALT's PC suppression changes the following PC, not the fetched address.
    rom.write_bytes(make_rom(bytes((0xF3, 0x3E, 1, 0xEA, 0xFF, 0xFF,
                                   0xE0, 0x0F, 0x76, 0xD3))))
    halted = run(runner, rom, expected_code=1)
    assert halted["status"] == "illegal_opcode" and halted["illegal_address"] == 0x159

    for protocol, message in (("serial", b"Passed"),
                              ("fibonacci", bytes((3, 5, 8, 13, 21, 34)))):
        program = serial_program(message[:-1])[:-3]
        # Disarm the old byte, reset DIV, then arm the final byte. The write
        # leaves DIV at 11 M-cycles; bit eight completes at cycle 1024.
        program.extend((0xAF, 0xE0, 0x02, 0xE0, 0x04,
                        0x3E, message[-1], 0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02))
        program.extend(bytes(1012))
        address = 0x150 + len(program)
        rom.write_bytes(make_rom(program + bytes((0,))))
        control = run(runner, rom, "--protocol", protocol)
        assert control["status"] == "pass" and control["serial_bytes"] == len(message)
        rom.write_bytes(make_rom(program + bytes((0xD3,))))
        final = run(runner, rom, "--protocol", protocol, expected_code=1)
        assert final["status"] == "illegal_opcode" and final["illegal_address"] == address
        assert final["serial_bytes"] == len(message)
        for field in ("steps", "base_cycles", "audio_frames", "audio_crc32"):
            assert final[field] == control[field], (field, final, control)

    report = root / "illegal-report.json"
    suite = Path(__file__).with_name("run_rom_suite.py")
    completed = subprocess.run([sys.executable, str(suite), str(runner), str(rom),
                                "--power-on-seed", FIXED_POWER_ON_SEED,
                                "--output", str(report)], capture_output=True, timeout=15)
    assert completed.returncode == 1, completed.stderr
    result = json.loads(report.read_text())
    assert result["counts"] == {"illegal_opcode": 1}, result
    assert result["results"][0]["exit_code"] == 1


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
        assert passed["base_clock_hz"] == 4194304
        assert passed["serial_bytes"] == len(b"Passed")
        quiet = subprocess.run([str(runner), "--power-on-seed", FIXED_POWER_ON_SEED,
                                "--protocol", "serial", str(rom)],
                               capture_output=True, check=True)
        assert b"Passed" not in quiet.stdout.split(b"CUPIDGB_RESULT", 1)[0]
        printed = subprocess.run([str(runner), "--power-on-seed", FIXED_POWER_ON_SEED,
                                  "--protocol", "serial", "--serial-output",
                                  str(rom)], capture_output=True, check=True)
        assert b"Passed" in printed.stdout.split(b"CUPIDGB_RESULT", 1)[0]
        printed_record = json.loads(next(line.removeprefix("CUPIDGB_RESULT ")
                                        for line in printed.stdout.decode().splitlines()
                                        if line.startswith("CUPIDGB_RESULT ")))
        assert printed_record["serial_bytes"] == passed["serial_bytes"]
        for field in ("status", "base_cycles", "audio_frames", "audio_crc32", "image_crc32"):
            assert printed_record[field] == passed[field], field
        if sys.platform.startswith("linux"):
            # A buffered stream can accept every byte and fail only at the final flush.
            with open("/dev/full", "wb") as failed_output:
                failed = subprocess.run([str(runner), "--power-on-seed", FIXED_POWER_ON_SEED,
                                         "--protocol", "serial", "--serial-output",
                                         str(rom)], stdout=failed_output, stderr=subprocess.PIPE,
                                        timeout=15)
            assert failed.returncode == 2, failed.stderr
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
        register = run(runner, rom, "--protocol", "register")
        assert register["status"] == "pass"
        assert register["steps"] == 9 and register["base_cycles"] == 72, register
        # Identical registers alone must not terminate a plain measurement run.
        assert run(runner, rom, "--frames", "1")["status"] == "bounded"
        rom.write_bytes(make_rom(bytes((0x06, 0x42, 0x0E, 0x42, 0x16, 0x42,
                                       0x1E, 0x42, 0x26, 0x42, 0x2E, 0x42, 0x40))))
        assert run(runner, rom, "--protocol", "register", expected_code=1)["status"] == "fail"
        illegal_opcode_checks(runner, root)
        for data, expected, code in ((bytes((3, 5, 8, 13, 21, 34)), "pass", 0),
                                     (bytes((0x42,)) * 6, "fail", 1)):
            rom.write_bytes(make_rom(serial_program(data)))
            signature = run(runner, rom, "--protocol", "fibonacci", expected_code=code)
            assert signature["status"] == expected and signature["serial_bytes"] == 6
            assert run(runner, rom, "--frames", "2")["status"] == "bounded"
        for data in (bytes((3, 5, 8, 13, 21)), bytes((0x42,)) * 5, b"Passed"):
            rom.write_bytes(make_rom(serial_program(data)))
            assert run(runner, rom, "--protocol", "fibonacci", "--frames", "2",
                       expected_code=3)["status"] == "bounded"
        rom.write_bytes(make_rom(serial_program(b"prefix\0" + bytes((3, 5, 8, 13, 21, 34)))))
        assert run(runner, rom, "--protocol", "fibonacci")["status"] == "pass"
        rom.write_bytes(make_rom(bytes((0xF3, 0xAF, 0xE0, 0x40, 0x76, 0x18, 0xFD))))
        bounded = run(runner, rom, "--frames", "2")
        assert bounded["status"] == "bounded"
        assert bounded["vblank_frames"] == 0
        assert bounded["presentation_events"] == 2
        assert bounded["base_cycles"] >= 140448
        for model, clock in (("sgb", 4295454), ("sgb2", 4194304)):
            model_result = run(runner, rom, "--frames", "2", "--model", model)
            assert model_result["base_clock_hz"] == clock
            assert model_result["base_cycles"] == bounded["base_cycles"]
            assert model_result["audio_frames"] == model_result["base_cycles"] * 44100 // clock
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
                                   "--frames", "1", "--power-on-seed", "0x1234",
                                   "--output", str(report)],
                                  capture_output=True, timeout=15)
        assert suite_run.returncode == 1
        assert json.loads(report.read_text())["counts"] == {"bounded": 1}, report.read_text()
        assert json.loads(report.read_text())["results"][0]["power_on_seed_hex"] == "0000000000001234"
        trace = root / "input.trace"
        trace.write_text("0 ff\n10 7f\n12 ff\n")
        run(runner, rom, "--input-trace", str(trace), "--frames", "20")
        trace.write_text("10 ff\n9 ff\n")
        assert subprocess.run([str(runner), "--input-trace", str(trace), str(rom)],
                              capture_output=True).returncode == 2
        startup_checks(runner, root)
    print("Runner integration checks passed.")


if __name__ == "__main__":
    main()
