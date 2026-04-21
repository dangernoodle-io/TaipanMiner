cmake_minimum_required(VERSION 3.16.0)

set(_BB_COMPONENTS "${CMAKE_CURRENT_SOURCE_DIR}/.breadboard/components")
list(APPEND EXTRA_COMPONENT_DIRS
    "${_BB_COMPONENTS}/log_stream"
    "${_BB_COMPONENTS}/ota_pull"
    "${_BB_COMPONENTS}/ota_push"
)

# Override breadboard's nv_config namespace to "taipanminer" for on-device compatibility
# (compile-time define; breadboard's nv_config.c checks `#ifndef BB_NV_CONFIG_NAMESPACE`)
add_compile_definitions(BB_NV_CONFIG_NAMESPACE="taipanminer")
add_compile_definitions(FIRMWARE_BOARD="${FIRMWARE_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

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
