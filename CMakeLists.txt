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

# bb_display_ek79007 (esp32p4-only) is always excluded to prevent IDF Component
# Manager from resolving its platform-specific deps on non-P4 targets.
# Per-board excludes drop unused panel backends so their constructors don't race
# the active one in bb_display_init's first-success-wins loop (st77xx has no probe
# and accepts junk pins, masquerading as the active backend on bitaxe boards).
set(EXCLUDE_COMPONENTS
    bb_display_ek79007
)
if(FIRMWARE_BOARD MATCHES "^bitaxe-")
    list(APPEND EXCLUDE_COMPONENTS bb_display_st77xx bb_display_ili9341)
elseif(FIRMWARE_BOARD MATCHES "^tdongle-")
    list(APPEND EXCLUDE_COMPONENTS bb_display_ssd1306 bb_display_ili9341)
elseif(FIRMWARE_BOARD STREQUAL "esp32-wroom32-cyd")
    list(APPEND EXCLUDE_COMPONENTS bb_display_st77xx bb_display_ssd1306)
else()
    # esp32-wroom32, esp32-s2-mini, esp32-c3-supermini, unknown: no display;
    # include ssd1306 as a no-op (probe fails gracefully with -1 pins).
    list(APPEND EXCLUDE_COMPONENTS bb_display_st77xx bb_display_ili9341)
endif()

add_compile_definitions(FIRMWARE_BOARD="${FIRMWARE_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# bb_hw (from breadboard v0.11.0+) resolves BB_HW_BOARD_HEADER relative to the
# compiling TU's include path. Inject TM's per-board headers globally so that
# bb_mdns/bb_wifi/etc. (in breadboard) can pick them up when expanding bb_hw.h.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}/components/board/include" APPEND)

# Set PROJECT_VER using bb_version logic (BB_FW_VERSION override > exact tag > dev sha).
# In CI, git describe --tags --always --dirty is still the preferred version source
# (produces the tag or a tag-relative ref), so keep that path.
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
    # Use bb_version.cmake: BB_FW_VERSION env var > exact tag > dev-g<sha>-bb-g<sha>
    include("${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/cmake/bb_version.cmake")
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
