cmake_minimum_required(VERSION 3.16.0)
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
if(DEFINED ENV{PIOENV})
    string(REGEX REPLACE "-debug$" "" FIRMWARE_BOARD "$ENV{PIOENV}")
else()
    set(FIRMWARE_BOARD "unknown")
endif()
project(taipanminer-${FIRMWARE_BOARD})
