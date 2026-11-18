# @file    CMakeLists.txt
# @brief   Configure the hardware core, Qt desktop, regression checks, and installation.
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
project(CupidGB VERSION 0.3.0 LANGUAGES C CXX)

include(GNUInstallDirs)

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
option(CUPIDGB_ENABLE_NATIVE_HASH "Use available platform SHA-256 acceleration" ON)
option(CUPIDGB_ENABLE_IPO "Use supported interprocedural optimization in Release builds" ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
if(CUPIDGB_ENABLE_IPO AND NOT CUPIDGB_ENABLE_SANITIZERS)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT cupidgb_ipo_supported OUTPUT cupidgb_ipo_output LANGUAGES C CXX)
    if(cupidgb_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        # Shared C state records cross the C++ frontend boundary. GCC's mixed-language
        # IPO can otherwise retain stale C++ field values after a valid C aggregate copy.
        add_compile_options(
            "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU>>:-fno-strict-aliasing>")
        message(STATUS "CupidGB Release interprocedural optimization enabled")
    else()
        message(STATUS "CupidGB Release interprocedural optimization unavailable")
    endif()
endif()


add_library(cupidgb_core STATIC
    src/common/log.c src/common/sha256.c src/core/emulator.c src/core/system.c
    src/compat/native_v1.c src/gb/battery.c
    src/gb/gb.c src/gb/startup.c src/gb/reset.c src/gb/rtc.c src/gb/state.c src/gb/cpu.c
    src/gb/oam_bus.c src/gb/ppu.c src/gb/pixels.c src/gb/apu.c src/gb/timer.c
    src/gb/cartridge.c src/gb/gbs.c src/gb/camera.c src/gb/mbc7.c src/gb/input.c
    src/gb/debugger.c src/gb/sgb.c
    src/gb/sgb_startup.c src/gb/sgb_startup_audio.c src/gb/sgb_startup_render.c src/gb/state_startup.c
    src/gb/state_sgb_protocol.c
    src/gbc/cgb.c src/gbc/cgb_palette.c src/gbc/cgb_dma.c src/gbc/cgb_render.c
    src/gb/printer.c src/gb/printer_state.c src/gb/link.c
    src/gb/cheats.c
    src/gb/tpp1.c
    src/gb/huc3.c
    src/gb/infrared.c src/gb/display_clock.c src/gb/vram_bus.c
    src/gb/apu_noise.c src/gb/audio_synthesis.c
    src/gb/audio_filter.c src/gb/audio_output.c src/gb/audio_state.c
)
target_include_directories(cupidgb_core PUBLIC include)
if(NOT MSVC)
    target_link_libraries(cupidgb_core PUBLIC m)
endif()
if(CUPIDGB_ENABLE_NATIVE_HASH)
    if(WIN32)
        target_compile_definitions(cupidgb_core PRIVATE CUPID_SHA256_CNG=1)
        target_link_libraries(cupidgb_core PRIVATE bcrypt)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(OpenSSL 3 QUIET COMPONENTS Crypto)
        if(TARGET OpenSSL::Crypto)
            target_compile_definitions(cupidgb_core PRIVATE CUPID_SHA256_OPENSSL=1)
            target_link_libraries(cupidgb_core PRIVATE OpenSSL::Crypto)
        endif()
    endif()
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
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia PrintSupport)
    message(STATUS "CupidGB Qt ${Qt6_VERSION} from ${Qt6_DIR}")
    find_package(SDL2 2.24 REQUIRED CONFIG)
    if(MSVC)
        get_target_property(cupidgb_sdl_target SDL2::SDL2 ALIASED_TARGET)
        if(NOT cupidgb_sdl_target)
            set(cupidgb_sdl_target SDL2::SDL2)
        endif()
        get_target_property(cupidgb_sdl_includes ${cupidgb_sdl_target} INTERFACE_INCLUDE_DIRECTORIES)
        foreach(cupidgb_sdl_include IN LISTS cupidgb_sdl_includes)
            # Keep the input SDK headers, without shadowing the compiler's C runtime headers.
            if(EXISTS "${cupidgb_sdl_include}/_mingw.h" AND
               EXISTS "${cupidgb_sdl_include}/SDL2/SDL.h")
                list(REMOVE_ITEM cupidgb_sdl_includes "${cupidgb_sdl_include}")
            endif()
        endforeach()
        set_property(TARGET ${cupidgb_sdl_target} PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES "${cupidgb_sdl_includes}")
    endif()
    add_library(cupidgb_controllers STATIC
        src/frontend/qt/controller_backend.cpp
        src/frontend/qt/controller_manager.cpp
        src/frontend/qt/controller_dialog.cpp)
    target_include_directories(cupidgb_controllers PUBLIC src/frontend/qt)
    target_link_libraries(cupidgb_controllers PUBLIC Qt6::Widgets SDL2::SDL2)
    if(CUPIDGB_ENABLE_SANITIZERS)
        target_compile_options(cupidgb_controllers PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(cupidgb_controllers PUBLIC -fsanitize=address,undefined)
    endif()
    add_executable(cupidgb
        src/frontend/qt/main.cpp
        src/frontend/qt/cartridge_file.cpp
        src/frontend/qt/cheat_search.cpp
        src/frontend/qt/cheats_window.cpp
        src/frontend/qt/color_pipeline.cpp
        src/frontend/qt/core_audio_collector.cpp
        src/frontend/qt/gbs_player.cpp
        src/frontend/qt/gbs_player.h
        src/frontend/qt/host_camera.cpp
        src/frontend/qt/main_window.cpp
        src/frontend/qt/main_window.h
        src/frontend/qt/main_window_cheats.cpp
        src/frontend/qt/main_window_cheat_tests.cpp
        src/frontend/qt/main_window_audio.cpp
        src/frontend/qt/main_window_camera.cpp
        src/frontend/qt/main_window_camera_tests.cpp
        src/frontend/qt/main_window_reset.cpp
        src/frontend/qt/main_window_reset_tests.cpp
        src/frontend/qt/main_window_ram_tests.cpp
        src/frontend/qt/main_window_reset_audio_tests.cpp
        src/frontend/qt/main_window_audio_policy_tests.cpp
        src/frontend/qt/main_window_input.cpp
        src/frontend/qt/main_window_motion.cpp
        src/frontend/qt/main_window_music.cpp
        src/frontend/qt/main_window_gbs_tests.cpp
        src/frontend/qt/main_window_session.cpp
        src/frontend/qt/main_window_display.cpp
        src/frontend/qt/main_window_display_files.cpp
        src/frontend/qt/main_window_display_file_tests.cpp
        src/frontend/qt/main_window_message_tests.cpp
        src/frontend/qt/main_window_playback_tests.cpp
        src/frontend/qt/screen_messages.cpp
        src/frontend/qt/display_files.cpp
        src/frontend/qt/palette_library.cpp
        src/frontend/qt/palette_editor.cpp
        src/frontend/qt/main_window_debugger.cpp
        src/frontend/qt/main_window_watchpoints.cpp
        src/frontend/qt/main_window_watchpoint_tests.cpp
        src/frontend/qt/main_window_workflow_tests.cpp
        src/frontend/qt/main_window_startup_tests.cpp
        src/frontend/qt/main_window_sgb_tests.cpp
        src/frontend/qt/main_window_rtc_tests.cpp
        src/frontend/qt/main_window_controller_tests.cpp
        src/frontend/qt/main_window_debugger_tests.cpp
        src/frontend/qt/main_window_session_tests.cpp
        src/frontend/qt/main_window_motion_tests.cpp
        src/frontend/qt/main_window_telemetry.cpp
        src/frontend/qt/main_window_printer.cpp
        src/frontend/qt/main_window_printer_tests.cpp
        src/frontend/qt/main_window_link.cpp
        src/frontend/qt/main_window_link_tests.cpp
        src/frontend/qt/printer_window.cpp
        src/frontend/qt/guest_clock_pacer.cpp
        src/frontend/qt/host_elapsed_timer.cpp
        src/frontend/qt/rewind_compression.cpp
        src/frontend/qt/audio_converter.cpp
        src/frontend/qt/turbo_audio_window.cpp
        src/frontend/qt/wave_recorder.cpp
        src/frontend/qt/display_pipeline.cpp
        src/frontend/qt/debugger_window.cpp
        src/frontend/qt/inspector_renderer.cpp
        src/frontend/qt/inspectors_window.cpp
    )
    target_link_libraries(cupidgb PRIVATE cupidgb_core cupidgb_controllers Qt6::Widgets
        Qt6::Multimedia Qt6::PrintSupport)
    target_compile_definitions(cupidgb PRIVATE CUPIDGB_VERSION="${PROJECT_VERSION}")
    qt_add_resources(cupidgb_icon_sources packaging/windows/cupidgb.qrc)
    target_sources(cupidgb PRIVATE ${cupidgb_icon_sources})
    if(WIN32)
        set_target_properties(cupidgb PROPERTIES WIN32_EXECUTABLE TRUE)
        enable_language(RC)
        configure_file(packaging/windows/cupidgb.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/generated/cupidgb.rc @ONLY)
        target_sources(cupidgb PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/cupidgb.rc)
        set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/generated/cupidgb.rc PROPERTIES
            OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/cupidgb.ico)
        add_custom_command(TARGET cupidgb POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:cupidgb>)
    endif()
    install(TARGETS cupidgb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cupidgb)
    install(FILES third_party/SDL2-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cupidgb)
    install(FILES third_party/SDL2-COPYING.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cupidgb)
    if(WIN32)
        install(FILES $<TARGET_FILE:SDL2::SDL2> DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES packaging/windows/Register-CupidGB.ps1 packaging/windows/Unregister-CupidGB.ps1
            packaging/windows/CupidGB.FileAssociations.psm1 DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(UNIX AND NOT APPLE)
        install(FILES packaging/linux/io.github.cupidthecat.CupidGB.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES packaging/linux/cupidgb.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
        install(FILES packaging/linux/cupidgb.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    endif()
endif()
if(CUPIDGB_ENABLE_TESTS)
    enable_testing()
    foreach(test_name IN ITEMS
        smoke gb gbs startup reset rtc rtc_timing rtc_dma rtc_state divider_phase
        serial serial_selection serial_state link printer printer_state ppu pixel_timing
        pixel_timing_state scy_latch stat apu wave_retrigger wave_history wave_history_state
        audio_synthesis audio_filter audio_state audio_publication audio_clock cpu_timing cpu_lockup
        dma oam_bus video_bus vram_bus vram_quiet bus state state_ram battery native_v1_recovery
        sgb sgb_protocol sgb_protocol_state sgb_startup sgb_startup_audio sgb_startup_state
        input input_sampling cartridge mmm01 mbc3_bus camera camera_host camera_state mbc7
        tpp1 rumble rumble_sampling presentation_observer huc3 infrared infrared_state
        infrared_guest cheats debugger debugger_watchpoints sha256)
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
    if(CUPIDGB_ENABLE_SANITIZERS AND NOT CUPIDGB_ENABLE_NATIVE_HASH)
        # Instrumented portable hashing processes the full snapshot matrix much
        # more slowly than platform hashing. Keep every case and allow runner margin.
        set_tests_properties(cupidgb_state PROPERTIES TIMEOUT 240)
    endif()
    target_sources(cupidgb_gb PRIVATE tests/test_gb_hardware.c)
    add_executable(cupidgb_core_interop tests/test_core_interop.cpp)
    target_link_libraries(cupidgb_core_interop PRIVATE cupidgb_core)
    if(MSVC)
        target_compile_options(cupidgb_core_interop PRIVATE /UNDEBUG)
    else()
        target_compile_options(cupidgb_core_interop PRIVATE -UNDEBUG)
    endif()
    add_test(NAME cupidgb_core_interop COMMAND cupidgb_core_interop)
    set_tests_properties(cupidgb_core_interop PROPERTIES TIMEOUT 120)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME cupidgb_source_headers
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_source_headers.py)
    set_tests_properties(cupidgb_source_headers PROPERTIES TIMEOUT 30)
    if(TARGET cupidgb_rom_runner)
        add_test(NAME cupidgb_runner
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_rom_runner.py
                    $<TARGET_FILE:cupidgb_rom_runner>)
        set_tests_properties(cupidgb_runner PROPERTIES TIMEOUT 120)
    endif()
    if(TARGET cupidgb)
        if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
            find_program(CUPIDGB_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
            add_test(NAME cupidgb_windows_associations
                COMMAND ${CUPIDGB_POWERSHELL_EXECUTABLE} -NoProfile -NonInteractive -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/test_file_associations.ps1)
            set_tests_properties(cupidgb_windows_associations PROPERTIES TIMEOUT 30)
            add_test(NAME cupidgb_windows_association_preservation
                COMMAND ${CUPIDGB_POWERSHELL_EXECUTABLE} -NoProfile -NonInteractive -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/test_file_association_preservation.ps1)
            set_tests_properties(cupidgb_windows_association_preservation PROPERTIES TIMEOUT 30)
        endif()
        add_executable(cupidgb_controller_backend tests/frontend/test_controller_backend.cpp)
        target_link_libraries(cupidgb_controller_backend PRIVATE cupidgb_controllers)
        add_test(NAME cupidgb_controller_backend COMMAND cupidgb_controller_backend)
        set_tests_properties(cupidgb_controller_backend PROPERTIES TIMEOUT 30)
        add_executable(cupidgb_controller_workflows tests/frontend/test_controller_workflows.cpp)
        target_link_libraries(cupidgb_controller_workflows PRIVATE cupidgb_controllers)
        add_test(NAME cupidgb_controller_workflows COMMAND cupidgb_controller_workflows)
        set_tests_properties(cupidgb_controller_workflows PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        add_executable(cupidgb_controller_dialog tests/frontend/test_controller_dialog.cpp)
        target_link_libraries(cupidgb_controller_dialog PRIVATE cupidgb_controllers)
        add_test(NAME cupidgb_controller_dialog COMMAND cupidgb_controller_dialog)
        set_tests_properties(cupidgb_controller_dialog PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        if(WIN32)
            foreach(controller_test IN ITEMS cupidgb_controller_backend cupidgb_controller_workflows
                                             cupidgb_controller_dialog)
                add_custom_command(TARGET ${controller_test} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${controller_test}>)
            endforeach()
        endif()
        add_executable(cupidgb_guest_clock_pacer
            tests/frontend/test_guest_clock_pacer.cpp src/frontend/qt/guest_clock_pacer.cpp)
        target_include_directories(cupidgb_guest_clock_pacer PRIVATE src/frontend/qt)
        if(MSVC)
            target_compile_options(cupidgb_guest_clock_pacer PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_guest_clock_pacer PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_guest_clock_pacer COMMAND cupidgb_guest_clock_pacer)
        add_executable(cupidgb_host_elapsed_timer
            tests/frontend/test_host_elapsed_timer.cpp src/frontend/qt/host_elapsed_timer.cpp)
        target_include_directories(cupidgb_host_elapsed_timer PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_host_elapsed_timer PRIVATE Qt6::Core)
        if(MSVC)
            target_compile_options(cupidgb_host_elapsed_timer PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_host_elapsed_timer PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_host_elapsed_timer COMMAND cupidgb_host_elapsed_timer)
        set_tests_properties(cupidgb_host_elapsed_timer PROPERTIES TIMEOUT 10)
        add_executable(cupidgb_rewind_compression
            tests/frontend/test_rewind_compression.cpp src/frontend/qt/rewind_compression.cpp)
        target_include_directories(cupidgb_rewind_compression PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_rewind_compression PRIVATE Qt6::Core)
        if(MSVC)
            target_compile_options(cupidgb_rewind_compression PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_rewind_compression PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_rewind_compression COMMAND cupidgb_rewind_compression)
        set_tests_properties(cupidgb_rewind_compression PROPERTIES TIMEOUT 30)
        add_executable(cupidgb_debugger_window
            tests/frontend/test_debugger_window.cpp src/frontend/qt/debugger_window.cpp)
        target_include_directories(cupidgb_debugger_window PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_debugger_window PRIVATE Qt6::Widgets)
        if(MSVC)
            target_compile_options(cupidgb_debugger_window PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_debugger_window PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_debugger_window COMMAND cupidgb_debugger_window)
        set_tests_properties(cupidgb_debugger_window PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        add_executable(cupidgb_inspectors
            tests/frontend/test_inspectors.cpp
            src/frontend/qt/inspector_renderer.cpp src/frontend/qt/inspectors_window.cpp)
        target_include_directories(cupidgb_inspectors PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_inspectors PRIVATE cupidgb_core Qt6::Widgets)
        if(MSVC)
            target_compile_options(cupidgb_inspectors PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_inspectors PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_inspectors COMMAND cupidgb_inspectors)
        set_tests_properties(cupidgb_inspectors PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        add_executable(cupidgb_screen_messages tests/frontend/test_screen_messages.cpp
            src/frontend/qt/screen_messages.cpp)
        target_include_directories(cupidgb_screen_messages PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_screen_messages PRIVATE Qt6::Gui)
        if(MSVC)
            target_compile_options(cupidgb_screen_messages PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_screen_messages PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_screen_messages COMMAND cupidgb_screen_messages)
        set_tests_properties(cupidgb_screen_messages PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        if(WIN32 AND DEFINED ENV{WINDIR})
            # The offscreen plugin does not discover installed Windows fonts itself.
            # Give this glyph regression the font files available to native Qt.
            file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" cupidgb_message_test_fonts)
            if(IS_DIRECTORY "${cupidgb_message_test_fonts}")
                set_property(TEST cupidgb_screen_messages APPEND PROPERTY ENVIRONMENT
                    "QT_QPA_FONTDIR=${cupidgb_message_test_fonts}")
            endif()
        endif()
        add_executable(cupidgb_display_pipeline
            tests/frontend/test_display_pipeline.cpp src/frontend/qt/display_pipeline.cpp)
        target_include_directories(cupidgb_display_pipeline PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_display_pipeline PRIVATE Qt6::Gui)
        if(MSVC)
            target_compile_options(cupidgb_display_pipeline PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_display_pipeline PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_display_pipeline COMMAND cupidgb_display_pipeline)
        add_executable(cupidgb_display_files
            tests/frontend/test_display_files.cpp src/frontend/qt/display_files.cpp)
        target_include_directories(cupidgb_display_files PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_display_files PRIVATE Qt6::Gui)
        if(MSVC)
            target_compile_options(cupidgb_display_files PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_display_files PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_display_files COMMAND cupidgb_display_files)
        add_executable(cupidgb_palette_library tests/frontend/test_palette_library.cpp
            src/frontend/qt/palette_library.cpp src/frontend/qt/display_files.cpp)
        target_include_directories(cupidgb_palette_library PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_palette_library PRIVATE Qt6::Gui)
        add_executable(cupidgb_palette_editor tests/frontend/test_palette_editor.cpp
            src/frontend/qt/palette_editor.cpp src/frontend/qt/palette_library.cpp
            src/frontend/qt/display_files.cpp)
        target_include_directories(cupidgb_palette_editor PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_palette_editor PRIVATE Qt6::Widgets)
        foreach(palette_test IN ITEMS cupidgb_palette_library cupidgb_palette_editor)
            if(MSVC)
                target_compile_options(${palette_test} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${palette_test} PRIVATE -UNDEBUG)
            endif()
            add_test(NAME ${palette_test} COMMAND ${palette_test})
            set_tests_properties(${palette_test} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
        endforeach()
        add_executable(cupidgb_color_pipeline
            tests/frontend/test_color_pipeline.cpp src/frontend/qt/color_pipeline.cpp)
        target_include_directories(cupidgb_color_pipeline PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_color_pipeline PRIVATE Qt6::Gui)
        if(MSVC)
            target_compile_options(cupidgb_color_pipeline PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_color_pipeline PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_color_pipeline COMMAND cupidgb_color_pipeline)
        add_executable(cupidgb_cheat_search
            tests/frontend/test_cheat_search.cpp src/frontend/qt/cheat_search.cpp)
        target_include_directories(cupidgb_cheat_search PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_cheat_search PRIVATE cupidgb_core)
        if(MSVC)
            target_compile_options(cupidgb_cheat_search PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_cheat_search PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_cheat_search COMMAND cupidgb_cheat_search)
        add_executable(cupidgb_audio_buffer tests/frontend/test_audio_buffer.cpp)
        target_include_directories(cupidgb_audio_buffer PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_audio_buffer PRIVATE Qt6::Core)
        if(MSVC)
            target_compile_options(cupidgb_audio_buffer PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_audio_buffer PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_audio_buffer COMMAND cupidgb_audio_buffer)
        set_tests_properties(cupidgb_audio_buffer PROPERTIES TIMEOUT 10)
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
        add_executable(cupidgb_host_camera
            tests/frontend/test_host_camera.cpp src/frontend/qt/host_camera.cpp)
        target_include_directories(cupidgb_host_camera PRIVATE include src/frontend/qt)
        target_link_libraries(cupidgb_host_camera PRIVATE Qt6::Gui Qt6::Multimedia)
        if(MSVC)
            target_compile_options(cupidgb_host_camera PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_host_camera PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_host_camera COMMAND cupidgb_host_camera)
        set_tests_properties(cupidgb_host_camera PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
        add_executable(cupidgb_turbo_audio_window
            tests/frontend/test_turbo_audio_window.cpp src/frontend/qt/turbo_audio_window.cpp)
        target_include_directories(cupidgb_turbo_audio_window PRIVATE src/frontend/qt)
        if(MSVC)
            target_compile_options(cupidgb_turbo_audio_window PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_turbo_audio_window PRIVATE -UNDEBUG)
        endif()
        if(CUPIDGB_ENABLE_SANITIZERS)
            target_compile_options(cupidgb_turbo_audio_window PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(cupidgb_turbo_audio_window PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME cupidgb_turbo_audio_window COMMAND cupidgb_turbo_audio_window)
        set_tests_properties(cupidgb_turbo_audio_window PROPERTIES TIMEOUT 30)
        add_executable(cupidgb_wave_recorder
            tests/frontend/test_wave_recorder.cpp src/frontend/qt/wave_recorder.cpp)
        target_include_directories(cupidgb_wave_recorder PRIVATE src/frontend/qt)
        target_link_libraries(cupidgb_wave_recorder PRIVATE Qt6::Core)
        if(MSVC)
            target_compile_options(cupidgb_wave_recorder PRIVATE /UNDEBUG)
        else()
            target_compile_options(cupidgb_wave_recorder PRIVATE -UNDEBUG)
        endif()
        add_test(NAME cupidgb_wave_recorder COMMAND cupidgb_wave_recorder)
        add_test(NAME cupidgb_qt_cli_selection
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_qt_cli.py
                    $<TARGET_FILE:cupidgb> --group selection)
        add_test(NAME cupidgb_qt_cli_cartridge
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_qt_cli.py
                    $<TARGET_FILE:cupidgb> --group cartridge)
        set_tests_properties(cupidgb_qt_cli_selection cupidgb_qt_cli_cartridge
            PROPERTIES TIMEOUT 90)
        add_test(NAME cupidgb_qt_workflows COMMAND cupidgb --smoke-test --smoke-workflow desktop)
        add_test(NAME cupidgb_qt_storage_workflows
            COMMAND cupidgb --smoke-test --smoke-workflow storage)
        add_test(NAME cupidgb_qt_sgb_workflows
            COMMAND cupidgb --smoke-test --smoke-workflow sgb)
        add_test(NAME cupidgb_qt_link_workflows
            COMMAND cupidgb --smoke-test --smoke-workflow link)
        add_test(NAME cupidgb_qt_gbs_workflows
            COMMAND cupidgb --smoke-test --smoke-workflow gbs)
        set_tests_properties(cupidgb_qt_workflows cupidgb_qt_storage_workflows
            cupidgb_qt_sgb_workflows cupidgb_qt_link_workflows cupidgb_qt_gbs_workflows PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()
endif()
