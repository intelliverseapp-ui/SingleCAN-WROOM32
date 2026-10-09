cmake_minimum_required(VERSION 3.16)

set(SINGLECAN_REQUIRED_IDF_VERSION "5.3.1")

include(
    $ENV{IDF_PATH}/tools/cmake/version.cmake
)

set(
    SINGLECAN_ACTIVE_IDF_VERSION
    "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}"
)

if(
    NOT SINGLECAN_ACTIVE_IDF_VERSION
        VERSION_EQUAL
    SINGLECAN_REQUIRED_IDF_VERSION
)
    message(
        FATAL_ERROR
        "SingleCAN protocol tests require ESP-IDF "
        "${SINGLECAN_REQUIRED_IDF_VERSION}, but the active version is "
        "${SINGLECAN_ACTIVE_IDF_VERSION}."
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(SingleCAN-protocol-tests)
