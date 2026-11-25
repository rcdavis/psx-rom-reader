cmake_minimum_required(VERSION 3.14)
project(psxview-cli LANGUAGES C)

if(NOT TARGET psxrom::psxrom)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../lib"
                     "${CMAKE_CURRENT_BINARY_DIR}/psxrom")
endif()

add_executable(psxview-cli
    main.c
)

target_link_libraries(psxview-cli PRIVATE
    psxrom::psxrom
)

if(UNIX)
    target_link_libraries(psxview-cli PRIVATE m)
endif()

target_compile_features(psxview-cli PRIVATE c_std_99)
