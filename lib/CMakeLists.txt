cmake_minimum_required(VERSION 3.14)
project(psxrom VERSION 0.1.0 LANGUAGES C)

add_library(psxrom
    src/disc.c
    src/iso9660.c
    src/tim.c
    src/xa.c
    src/str.c src/tmd.c src/mmd.c
)

add_library(psxrom::psxrom ALIAS psxrom)

target_include_directories(psxrom PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(psxrom PUBLIC c_std_99)

if(NOT MSVC)
    target_compile_options(psxrom PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    target_link_libraries(psxrom PRIVATE m)
endif()
