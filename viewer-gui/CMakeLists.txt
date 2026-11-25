cmake_minimum_required(VERSION 3.14)
project(psxview-gui LANGUAGES C CXX)

if(NOT TARGET imgui::imgui)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../imgui"
                     "${CMAKE_CURRENT_BINARY_DIR}/imgui")
endif()

if(NOT TARGET psxrom::psxrom)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../lib"
                     "${CMAKE_CURRENT_BINARY_DIR}/psxrom")
endif()

find_package(OpenGL REQUIRED)

add_executable(psxview-gui
    src/main.cpp
    src/resource_entries.cpp
)

target_link_libraries(psxview-gui PRIVATE
    imgui::imgui
    psxrom::psxrom
    OpenGL::GL
)

target_compile_features(psxview-gui PRIVATE cxx_std_20)
