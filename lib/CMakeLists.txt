cmake_minimum_required(VERSION 3.14)
project(psxrom VERSION 0.1.0 LANGUAGES C)

option(PSXROM_ENABLE_CHD "Enable CHD disc images using libchdr" OFF)

if(PSXROM_ENABLE_CHD)
    include(FetchContent)
    FetchContent_Declare(
        libchdr
        GIT_REPOSITORY https://github.com/rtissera/libchdr.git
        GIT_TAG v0.3.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libchdr)

    if(NOT CHDR_WANT_RAW_DATA_SECTOR OR NOT CHDR_WANT_SUBCODE)
        message(FATAL_ERROR "PSXROM_ENABLE_CHD requires libchdr raw sectors and subcode")
    endif()
endif()

add_library(psxrom
    src/disc.c
    src/iso9660.c
    src/tim.c
    src/xa.c
    src/str.c
    src/tmd.c
    src/mmd.c
    src/map.c
)

add_library(psxrom::psxrom ALIAS psxrom)

target_include_directories(psxrom PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(psxrom PUBLIC c_std_99)

if(PSXROM_ENABLE_CHD)
    target_compile_definitions(psxrom PRIVATE PSXROM_ENABLE_CHD)
    target_link_libraries(psxrom PRIVATE chdr-static)
endif()

if(NOT MSVC)
    target_compile_options(psxrom PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    target_link_libraries(psxrom PRIVATE m)
endif()
