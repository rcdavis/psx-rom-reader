cmake_minimum_required(VERSION 3.14)
project(psxview LANGUAGES C)

if(NOT TARGET psxrom::psxrom)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../lib"
                     "${CMAKE_CURRENT_BINARY_DIR}/psxrom")
endif()

add_executable(psxview
    main.c
)

target_link_libraries(psxview PRIVATE
    psxrom::psxrom
)

target_compile_features(psxview PRIVATE c_std_99)
