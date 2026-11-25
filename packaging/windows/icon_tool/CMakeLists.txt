# @file    CMakeLists.txt
# @brief   Build the Qt tool that generates CupidGB Windows icon resources.
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

cmake_minimum_required(VERSION 3.21)
project(CupidGBIconTool LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Svg)
add_executable(cupidgb_generate_icon generate_icon.cpp)
target_link_libraries(cupidgb_generate_icon PRIVATE Qt6::Gui Qt6::Svg)
