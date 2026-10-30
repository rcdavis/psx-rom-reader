cmake_minimum_required(VERSION 3.14)
project(psxrom VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(PSXROM_BUILD_VIEWER "Build the psxview command line viewer" ${PROJECT_IS_TOP_LEVEL})

add_library(psxrom src/disc.c src/iso9660.c src/tim.c)
add_library(psxrom::psxrom ALIAS psxrom)
target_include_directories(psxrom PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(NOT MSVC)
    target_compile_options(psxrom PRIVATE -Wall -Wextra)
endif()

if(PSXROM_BUILD_VIEWER)
    add_executable(psxview viewer/main.c)
    target_link_libraries(psxview PRIVATE psxrom)
endif()
