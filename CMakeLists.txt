cmake_minimum_required(VERSION 3.20)

project(cupidgb VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUPIDGB_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(CUPIDGB_ENABLE_TESTS "Build tests" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(cupidgb_core STATIC
    src/common/log.c
    src/core/emulator.c
    src/core/system.c
    src/gb/gb.c
    src/gb/cpu.c
    src/gb/ppu.c
    src/gb/apu.c
    src/gb/timer.c
    src/gb/cartridge.c
    src/gb/sgb.c
    src/gbc/cgb.c
    src/platform/sdl/sdl_app.c
)

target_include_directories(cupidgb_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(cupidgb_core PUBLIC ${SDL2_LIBRARY_DIRS})
target_link_libraries(cupidgb_core PUBLIC ${SDL2_LIBRARIES} m)
target_compile_options(cupidgb_core PUBLIC ${SDL2_CFLAGS_OTHER})

if(CUPIDGB_ENABLE_WARNINGS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cupidgb_core PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wdouble-promotion
        )
    elseif(MSVC)
        target_compile_options(cupidgb_core PRIVATE /W4)
    endif()
endif()

add_executable(cupidgb src/main.c)
target_link_libraries(cupidgb PRIVATE cupidgb_core)

if(CUPIDGB_ENABLE_TESTS)
    enable_testing()

    add_executable(cupidgb_smoke tests/test_smoke.c)
    target_link_libraries(cupidgb_smoke PRIVATE cupidgb_core)
    add_test(NAME cupidgb_smoke COMMAND cupidgb_smoke)

    add_executable(cupidgb_gb tests/test_gb.c)
    target_link_libraries(cupidgb_gb PRIVATE cupidgb_core)
    add_test(NAME cupidgb_gb COMMAND cupidgb_gb)

endif()
