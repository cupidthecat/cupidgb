#!/usr/bin/env python3
# @file    run_rom_suite.py
# @brief   Run external GB/GBC test cartridges and retain explicit verdicts.
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
"""Run externally supplied GB/GBC test ROMs and keep explicit test verdicts."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def parse_power_on_seed(value):
    if value == "auto":
        return value
    hexadecimal = value.startswith(("0x", "0X"))
    digits = value[2:] if hexadecimal else value
    alphabet = "0123456789abcdef" if hexadecimal else "0123456789"
    message = "Use auto or an unsigned 64-bit decimal or 0x hexadecimal power-on seed."
    if not digits or any(digit.lower() not in alphabet for digit in digits):
        raise argparse.ArgumentTypeError(message)
    significant = digits.lstrip("0") or "0"
    if len(significant) > (16 if hexadecimal else 20):
        raise argparse.ArgumentTypeError(message)
    if int(significant, 16 if hexadecimal else 10) > (1 << 64) - 1:
        raise argparse.ArgumentTypeError(message)
    return value


def run_case(runner, rom, model, protocol, frames, timeout, boot=None, power_on_seed=None):
    command = [str(runner), "--model", model, "--protocol", protocol, "--frames", str(frames)]
    if boot:
        command += ["--boot", str(boot)]
    if power_on_seed is not None:
        command += ["--power-on-seed", str(power_on_seed)]
    command.append(str(rom))
    record = {"rom": str(rom), "sha256": hashlib.sha256(rom.read_bytes()).hexdigest()}
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return record | {"status": "timeout", "timeout_seconds": timeout}
    except OSError as error:
        return record | {"status": "launch_error", "error": str(error)}
    output = completed.stdout.decode("utf-8", errors="replace")
    summaries = [line.removeprefix("CUPIDGB_RESULT ") for line in output.splitlines()
                 if line.startswith("CUPIDGB_RESULT ")]
    if len(summaries) == 1:
        try:
            record.update(json.loads(summaries[0]))
        except json.JSONDecodeError:
            record["status"] = "invalid_result"
    else:
        record["status"] = "missing_result"
    record["exit_code"] = completed.returncode
    record["stdout"] = output[-16384:]
    record["stderr"] = completed.stderr.decode("utf-8", errors="replace")[-16384:]
    allowed_codes = {"pass": (0,), "bounded": (0, 3), "step_limit": (0, 3), "fail": (1,),
                     "illegal_opcode": (1,), "core_error": (2,), "output_error": (2,)}
    if (record.get("status") in allowed_codes
            and completed.returncode not in allowed_codes[record["status"]]):
        record["status"] = "runner_error"
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", type=Path)
    parser.add_argument("roms", type=Path, nargs="+")
    parser.add_argument("--model", choices=("dmg", "mgb", "cgb", "sgb", "sgb2"), default="dmg")
    parser.add_argument("--protocol", choices=("serial", "register", "fibonacci", "memory", "none"),
                        default="serial")
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--boot", type=Path)
    parser.add_argument("--power-on-seed", type=parse_power_on_seed, metavar="auto|UINT64",
                        help="Cold-start seed in decimal or 0x hexadecimal; defaults to auto")
    parser.add_argument("--output", type=Path, required=True,
                        help="Private JSON report path; records ROM paths and captured output")
    args = parser.parse_args()
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("Frame and timeout limits must be positive.")
    runner = args.runner.resolve(strict=True)
    roms = []
    for path in args.roms:
        if path.is_dir():
            roms.extend(sorted(p for p in path.rglob("*") if p.suffix.lower() in (".gb", ".gbc")))
        else:
            roms.append(path.resolve(strict=True))
    if not roms:
        parser.error("No GB/GBC ROMs found.")
    results = []
    for rom in dict.fromkeys(roms):
        result = run_case(runner, rom, args.model, args.protocol, args.frames, args.timeout,
                          args.boot, args.power_on_seed)
        results.append(result)
        print(f"{result['status']}: {rom.name}", flush=True)
    counts = {status: sum(r["status"] == status for r in results)
              for status in sorted({r["status"] for r in results})}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"counts": counts, "results": results}, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps(counts))
    # A measurement run cannot establish a passing test verdict.
    return 0 if all(r["status"] == "pass" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
