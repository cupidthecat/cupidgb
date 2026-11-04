cmake_minimum_required(VERSION 3.21)
project(CupidGB VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUPIDGB_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(CUPIDGB_ENABLE_TESTS "Build regression tests" ON)
option(CUPIDGB_BUILD_QT "Build the Qt desktop frontend" ON)
option(CUPIDGB_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

add_library(cupidgb_core STATIC
    src/common/log.c src/core/emulator.c src/core/system.c
    src/gb/gb.c src/gb/cpu.c src/gb/ppu.c src/gb/apu.c
    src/gb/timer.c src/gb/cartridge.c src/gb/sgb.c src/gbc/cgb.c
)
target_include_directories(cupidgb_core PUBLIC include)
if(NOT MSVC)
    target_link_libraries(cupidgb_core PUBLIC m)
endif()
if(CUPIDGB_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(cupidgb_core PRIVATE /W4)
    else()
        target_compile_options(cupidgb_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endif()
if(CUPIDGB_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Sanitizers require GCC or Clang with GNU command-line options")
    endif()
    target_compile_options(cupidgb_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cupidgb_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(cupidgb_rom_runner tools/rom_runner.c)
target_link_libraries(cupidgb_rom_runner PRIVATE cupidgb_core)
if(CUPIDGB_BUILD_QT)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)
    add_executable(cupidgb
        src/frontend/qt/main.cpp
        src/frontend/qt/main_window.cpp
        src/frontend/qt/main_window.h
        src/frontend/qt/audio_converter.cpp
    )
    target_link_libraries(cupidgb PRIVATE cupidgb_core Qt6::Widgets Qt6::Multimedia)
    install(TARGETS cupidgb RUNTIME DESTINATION bin)
    install(FILES LICENSE LICENSES/hardware.txt DESTINATION share/licenses/cupidgb)
endif()
if(CUPIDGB_ENABLE_TESTS)
    enable_testing()
    foreach(test_name IN ITEMS smoke gb serial ppu apu cpu_timing)
        add_executable(cupidgb_${test_name} tests/test_${test_name}.c)
        target_link_libraries(cupidgb_${test_name} PRIVATE cupidgb_core)
        # Tests use assert for setup and checks, including in Release builds.
        if(MSVC)
            target_compile_options(cupidgb_${test_name} PRIVATE /UNDEBUG)
            target_link_options(cupidgb_${test_name} PRIVATE /STACK:8388608)
        else()
            target_compile_options(cupidgb_${test_name} PRIVATE -UNDEBUG)
            target_compile_definitions(cupidgb_${test_name} PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
        add_test(NAME cupidgb_${test_name} COMMAND cupidgb_${test_name})
        set_tests_properties(cupidgb_${test_name} PROPERTIES TIMEOUT 120)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(TARGET cupidgb_rom_runner)
        add_test(NAME cupidgb_runner
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_rom_runner.py
                    $<TARGET_FILE:cupidgb_rom_runner>)
        set_tests_properties(cupidgb_runner PROPERTIES TIMEOUT 120)
    endif()
    if(TARGET cupidgb)
        add_executable(cupidgb_audio_converter
            tests/frontend/test_audio_converter.cpp src/frontend/qt/audio_converter.cpp)
        target_include_directories(cupidgb_audio_converter PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_audio_converter PRIVATE Qt6::Multimedia)
        if(MSVC)
            target_compile_options(cupidgb_audio_converter PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_audio_converter PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_audio_converter COMMAND cupidgb_audio_converter)
        add_test(NAME cupidgb_qt_workflows COMMAND cupidgb --smoke-test)
        set_tests_properties(cupidgb_qt_workflows PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()
endif()
