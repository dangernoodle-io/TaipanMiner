cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS
    "components"  # Local components take precedence
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/platform/espidf/bb_event_ring_espidf"
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/platform/espidf/bb_event_routes_espidf"
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/platform/espidf/bb_ring_espidf"
)

if(NOT DEFINED FIRMWARE_BOARD)
    if(DEFINED ENV{PIOENV})
        set(FIRMWARE_BOARD "$ENV{PIOENV}")
    else()
        set(FIRMWARE_BOARD "unknown")
    endif()
endif()

# No display hardware on esp32-wroom32 (Phase 1, headless) — exclude every
# backend so IDF Component Manager never resolves their platform-specific deps.
set(EXCLUDE_COMPONENTS
    bb_display_ek79007
    bb_display_st77xx
    bb_display_ili9341
    bb_display_ssd1306
)

add_compile_definitions(FIRMWARE_BOARD="${FIRMWARE_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# bb_hw resolves BB_HW_BOARD_HEADER relative to the compiling TU's include
# path. Inject the board header dir globally so bb_wifi/bb_mdns/etc. (in
# breadboard) can find it when expanding bb_hw.h.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}/components/board/include" APPEND)

project(taipanminer-v2-${FIRMWARE_BOARD})

# Lint gate: TM is a downstream CONSUMER of breadboard, so run bb's conventions
# linter (consumer profile) at CMake configure time. bb_lint() FATAL_ERRORs on
# any violation, failing `pio run` before compilation — the rescaffold is
# lint-gated from day one. bbtool.cmake's auto-invoke is scoped to bb's own
# tree, so TM must call bb_lint explicitly.
include(${CMAKE_SOURCE_DIR}/.breadboard/cmake/bbtool.cmake)
bb_lint(ROOT "${CMAKE_SOURCE_DIR}" PROFILE consumer)
