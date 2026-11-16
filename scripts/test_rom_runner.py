#!/usr/bin/env python3
"""Check runner verdicts and capture files with generated, disposable ROMs."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


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
    completed = subprocess.run([str(runner), *options, str(rom)], capture_output=True, timeout=15)
    if completed.returncode != expected_code:
        raise AssertionError((completed.returncode, expected_code, completed.stdout, completed.stderr))
    records = [json.loads(line.removeprefix("CUPIDGB_RESULT "))
               for line in completed.stdout.decode("utf-8", errors="replace").splitlines()
               if line.startswith("CUPIDGB_RESULT ")]
    if len(records) != 1:
        raise AssertionError(completed.stdout)
    return records[0]


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
        quiet = subprocess.run([str(runner), "--protocol", "serial", str(rom)],
                               capture_output=True, check=True)
        assert b"Passed" not in quiet.stdout.split(b"CUPIDGB_RESULT", 1)[0]
        printed = subprocess.run([str(runner), "--protocol", "serial", "--serial-output",
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
                failed = subprocess.run([str(runner), "--protocol", "serial", "--serial-output",
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
