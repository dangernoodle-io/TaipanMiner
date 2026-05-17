cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS
    "components"  # Local components take precedence
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/components"
    # ESP-IDF platform impls split out of portable BB components (breadboard PR
    # #252 / #257): the espidf-only sources for bb_event_ring and
    # bb_event_routes now live as standalone components under platform/espidf/.
    # Without these, link fails with "undefined reference to bb_event_ring_now_us".
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/platform/espidf/bb_event_ring_espidf"
    "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/platform/espidf/bb_event_routes_espidf"
)

# Resolve FIRMWARE_BOARD early so per-board EXCLUDE_COMPONENTS can branch on it.
# (Final assignment, including project()-name use, happens further down.)
if(NOT DEFINED FIRMWARE_BOARD)
    if(DEFINED ENV{PIOENV})
        string(REGEX REPLACE "-debug$" "" FIRMWARE_BOARD "$ENV{PIOENV}")
    else()
        set(FIRMWARE_BOARD "unknown")
    endif()
endif()

# bb_display_ek79007 (esp32p4-only) and bb_display_ili9341 are excluded to
# prevent IDF Component Manager from resolving their platform-specific deps
# on bitaxe/tdongle-s3 targets. Per-board excludes also drop the unused
# panel backend so its constructor doesn't race the right one in
# bb_display_init's first-success-wins loop (st77xx has no probe and
# accepts junk pins, masquerading as the active backend on bitaxe boards).
set(EXCLUDE_COMPONENTS
    bb_display_ek79007
    bb_display_ili9341
)
if(FIRMWARE_BOARD MATCHES "^bitaxe-")
    list(APPEND EXCLUDE_COMPONENTS bb_display_st77xx)
elseif(FIRMWARE_BOARD MATCHES "^tdongle-")
    list(APPEND EXCLUDE_COMPONENTS bb_display_ssd1306)
endif()

add_compile_definitions(FIRMWARE_BOARD="${FIRMWARE_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# bb_hw (from breadboard v0.11.0+) resolves BB_HW_BOARD_HEADER relative to the
# compiling TU's include path. Inject TM's per-board headers globally so that
# bb_mdns/bb_wifi/etc. (in breadboard) can pick them up when expanding bb_hw.h.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}/components/board/include" APPEND)

if(DEFINED ENV{CI})
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT PROJECT_VER)
        set(PROJECT_VER "dev")
    endif()
else()
    string(TIMESTAMP BUILD_TS "%Y%m%d%H%M%S" UTC)
    set(PROJECT_VER "dev-${BUILD_TS}")
endif()

# Board-specific project name for OTA validation
if(NOT DEFINED FIRMWARE_BOARD)
    if(DEFINED ENV{PIOENV})
        string(REGEX REPLACE "-debug$" "" FIRMWARE_BOARD "$ENV{PIOENV}")
    else()
        set(FIRMWARE_BOARD "unknown")
    endif()
endif()
project(taipanminer-${FIRMWARE_BOARD})
