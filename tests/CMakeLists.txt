add_executable(test_disc test_disc.c)
target_link_libraries(test_disc PRIVATE psxrom::psxrom)

add_test(NAME disc_cue COMMAND test_disc ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(disc_cue PROPERTIES FIXTURES_SETUP disc_images)

if(TARGET psxview-gui)
    enable_language(CXX)
    add_test(NAME gui_arguments
        COMMAND ${CMAKE_COMMAND}
            -DGUI_EXECUTABLE=$<TARGET_FILE:psxview-gui>
            -DMISSING_IMAGE=${CMAKE_CURRENT_BINARY_DIR}/missing-gui-image.iso
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_gui_arguments.cmake)

    add_executable(test_resource_entries
        test_resource_entries.cpp
        ../viewer-gui/src/resource_entries.cpp)
    target_include_directories(test_resource_entries PRIVATE ../viewer-gui/include)
    target_link_libraries(test_resource_entries PRIVATE psxrom::psxrom)
    target_compile_features(test_resource_entries PRIVATE cxx_std_20)
    add_test(NAME gui_resource_entries
        COMMAND test_resource_entries ${CMAKE_CURRENT_BINARY_DIR}/gui-resources.iso)
endif()

if(PSXROM_ENABLE_CHD)
    find_program(PSXROM_CHDMAN chdman)
    if(PSXROM_CHDMAN)
        add_test(NAME disc_create_chd
            COMMAND ${PSXROM_CHDMAN} createcd -i ${CMAKE_CURRENT_BINARY_DIR}/whole.cue
                    -o ${CMAKE_CURRENT_BINARY_DIR}/test.chd -f)
        set_tests_properties(disc_create_chd PROPERTIES FIXTURES_REQUIRED disc_images FIXTURES_SETUP disc_chd_image)
        add_test(NAME disc_chd COMMAND test_disc ${CMAKE_CURRENT_BINARY_DIR} compare-chd)
        set_tests_properties(disc_chd PROPERTIES FIXTURES_REQUIRED disc_chd_image)
        add_test(NAME disc_create_offset_chd
            COMMAND ${PSXROM_CHDMAN} createcd -i ${CMAKE_CURRENT_BINARY_DIR}/offset.cue
                    -o ${CMAKE_CURRENT_BINARY_DIR}/offset.chd -f)
        set_tests_properties(disc_create_offset_chd PROPERTIES FIXTURES_REQUIRED disc_images FIXTURES_SETUP disc_offset_image)
        add_test(NAME disc_offset_chd COMMAND test_disc ${CMAKE_CURRENT_BINARY_DIR} compare-offset)
        set_tests_properties(disc_offset_chd PROPERTIES FIXTURES_REQUIRED disc_offset_image)
    endif()
endif()
