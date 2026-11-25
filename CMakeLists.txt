cmake_minimum_required(VERSION 3.14)
project(psx_rom_reader VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(_psxrom_cli_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_psxrom_cli_default ON)
endif()
option(PSXROM_BUILD_CLI "Build the psxview-cli command line viewer" ${_psxrom_cli_default})

add_subdirectory(imgui)
add_subdirectory(lib)

if(PSXROM_BUILD_CLI)
    add_subdirectory(viewer-cli)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
