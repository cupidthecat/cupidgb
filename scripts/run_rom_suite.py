#!/usr/bin/env python3
"""Run externally supplied GB/GBC test ROMs and keep explicit test verdicts."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def run_case(runner, rom, model, protocol, frames, timeout, boot=None):
    command = [str(runner), "--model", model, "--protocol", protocol, "--frames", str(frames)]
    if boot:
        command += ["--boot", str(boot)]
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
    allowed_codes = {"pass": (0,), "bounded": (0, 3), "step_limit": (0, 3), "fail": (1,)}
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
        result = run_case(runner, rom, args.model, args.protocol, args.frames, args.timeout, args.boot)
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
