cmake_minimum_required(VERSION 3.14)
project(psx_rom_reader VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(_psxrom_viewer_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_psxrom_viewer_default ON)
endif()
option(PSXROM_BUILD_VIEWER "Build the psxview command line viewer" ${_psxrom_viewer_default})

add_subdirectory(lib)

if(PSXROM_BUILD_VIEWER)
    add_subdirectory(viewer)
endif()
