# @file    check_source_headers.py
# @brief   Check owned source inventory, purpose and license headers, and file sizes.
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

"""Validate source prologues from the current checkout, including new files."""
import argparse
from datetime import date
from pathlib import Path
import re
import sys


C_SUFFIXES = {".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".inc", ".rc"}
SCRIPT_SUFFIXES = {".py", ".sh", ".ps1", ".psm1", ".cmake"}
SOURCE_ROOTS = ("src", "include", "tests", "tools", "scripts", "packaging")
ROOT_FILES = ("CMakeLists.txt", "Makefile", "cupidgb")
LICENSE_TEXT = (
    "This program is free software: you can redistribute it and/or modify "
    "it under the terms of the GNU General Public License as published by "
    "the Free Software Foundation, either version 3 of the License, or "
    "(at your option) any later version.",
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
    "See the GNU General Public License for more details.",
    "You should have received a copy of the GNU General Public License "
    "along with this program. If not, see <https://gnu.org>.",
)


def source_kind(path):
    name = path.name.lower()
    if path.suffix.lower() in C_SUFFIXES or name.endswith(".rc.in"):
        return "c"
    if path.suffix.lower() in SCRIPT_SUFFIXES or name == "cmakelists.txt":
        return "script"
    return None


def inventory(root):
    """Scan owned directories without depending on Git or a stored file list."""
    found = {}
    for directory in SOURCE_ROOTS:
        for path in (root / directory).rglob("*"):
            kind = source_kind(path)
            if kind and path.is_file():
                found[path] = kind
    for name in ROOT_FILES:
        found[root / name] = "script"
    for suffix in ("*.yml", "*.yaml"):
        for path in (root / ".github" / "workflows").glob(suffix):
            found[path] = "script"
    return sorted(found.items(), key=lambda item: item[0].relative_to(root).as_posix())


def leading_header(text, kind):
    if kind == "c":
        match = re.match(r"/\*\*(.*?)\*/", text, re.DOTALL)
        if not match:
            return ""
        return "\n".join(re.sub(r"^\s*\* ?", "", line)
                         for line in match.group(1).splitlines())
    lines = text.splitlines()
    if lines and lines[0].startswith("#!"):
        lines = lines[1:]
    result = []
    for line in lines:
        if not line.startswith("#"):
            break
        result.append(re.sub(r"^# ?", "", line))
    return "\n".join(result)


def check(path, kind):
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        return [f"cannot read source: {error}"]
    header = leading_header(text, kind)
    if not header:
        return ["missing leading purpose and license header"]
    errors = []
    file_field = re.search(r"^@file[ \t]+(\S+)[ \t]*$", header, re.MULTILINE)
    if not file_field or file_field.group(1) != path.name:
        errors.append("@file must name this file")
    brief = re.search(r"^@brief[ \t]+(.+\S)[ \t]*$", header, re.MULTILINE)
    if not brief:
        errors.append("missing @brief purpose")
    if not re.search(r"^@author[ \t]+cupidthecat[ \t]*$", header, re.MULTILINE):
        errors.append("missing project @author")
    created = re.search(r"^@date[ \t]+(\d{4}-\d{2}-\d{2})[ \t]*$", header, re.MULTILINE)
    try:
        if not created:
            raise ValueError
        date.fromisoformat(created.group(1))
    except ValueError:
        errors.append("@date must be a valid YYYY-MM-DD date")
    normalized = " ".join(header.split())
    if not re.search(r"Copyright \(C\) \d{4}(?:-\d{4})? cupidthecat", normalized):
        errors.append("missing project copyright")
    for paragraph in LICENSE_TEXT:
        if paragraph not in normalized:
            errors.append("incomplete GPL notice: " + paragraph.split(". ", 1)[0])
    if kind == "c" and len(text.splitlines()) > 2000:
        errors.append("exceeds 2,000 lines; split by subsystem")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--list", action="store_true", help="list every checked source")
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        parser.error("source root must be a directory")
    files = inventory(root)
    failures = []
    for path, kind in files:
        relative = path.relative_to(root).as_posix()
        if args.list:
            print(relative)
        failures.extend(f"{relative}: {error}" for error in check(path, kind))
    for failure in failures:
        print(failure, file=sys.stderr)
    c_count = sum(kind == "c" for _, kind in files)
    print(f"Checked {len(files)} source files ({c_count} C/C++/resource sources, "
          f"{len(files) - c_count} scripts/build files); {len(failures)} errors.")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
