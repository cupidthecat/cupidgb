#!/usr/bin/env sh
# @file    setup_dev_env.sh
# @brief   Inspect or install Linux development dependencies for CupidGB.
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
set -eu

INSTALL=0

if [ "${1-}" = "--install" ]; then
    INSTALL=1
fi

PACKAGES_APT="build-essential python3 cmake qt6-base-dev qt6-multimedia-dev libsdl2-dev gdb clangd clang-format cppcheck valgrind"
PACKAGES_FEDORA="gcc gcc-c++ make python3 cmake qt6-qtbase-devel qt6-qtmultimedia-devel pkgconfig(sdl2) gdb clang-tools-extra clang-format cppcheck valgrind"
PACKAGES_ARCH="gcc make python cmake qt6-base qt6-multimedia sdl2-compat gdb clang clang-format cppcheck valgrind"
PACKAGES_OPENSUSE="gcc gcc-c++ make python3 cmake qt6-base-devel qt6-multimedia-devel pkgconfig(sdl2) gdb clang-tools clang-format cppcheck valgrind"

print_plan() {
    printf 'cupidgb development environment helper\n\n'
    printf 'This script can install recommended packages for Linux development, including Qt 6 Widgets, Qt Multimedia, and SDL2.\n'
    printf 'Run with --install to perform installation.\n\n'
}

run_install() {
    manager="$1"
    packages="$2"

    case "$manager" in
        apt-get)
            sudo apt-get update
            sudo apt-get install -y $packages
            ;;
        dnf)
            sudo dnf install -y $packages
            ;;
        pacman)
            sudo pacman -Sy --needed $packages
            ;;
        zypper)
            sudo zypper install -y $packages
            ;;
        *)
            printf 'Unsupported package manager: %s\n' "$manager" >&2
            exit 1
            ;;
    esac
}

print_plan

if command -v apt-get >/dev/null 2>&1; then
    printf 'Detected apt-based system.\n'
    printf 'Packages: %s\n' "$PACKAGES_APT"
    [ "$INSTALL" -eq 1 ] && run_install apt-get "$PACKAGES_APT"
    exit 0
fi

if command -v dnf >/dev/null 2>&1; then
    printf 'Detected dnf-based system.\n'
    printf 'Packages: %s\n' "$PACKAGES_FEDORA"
    [ "$INSTALL" -eq 1 ] && run_install dnf "$PACKAGES_FEDORA"
    exit 0
fi

if command -v pacman >/dev/null 2>&1; then
    printf 'Detected pacman-based system.\n'
    printf 'Packages: %s\n' "$PACKAGES_ARCH"
    [ "$INSTALL" -eq 1 ] && run_install pacman "$PACKAGES_ARCH"
    exit 0
fi

if command -v zypper >/dev/null 2>&1; then
    printf 'Detected zypper-based system.\n'
    printf 'Packages: %s\n' "$PACKAGES_OPENSUSE"
    [ "$INSTALL" -eq 1 ] && run_install zypper "$PACKAGES_OPENSUSE"
    exit 0
fi

printf 'No supported package manager detected. Install gcc, make, cmake, gdb, clangd, and clang-format manually.\n' >&2
exit 1
