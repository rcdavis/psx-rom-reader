add_executable(test_disc test_disc.c)
target_link_libraries(test_disc PRIVATE psxrom::psxrom)

add_test(NAME disc_cue COMMAND test_disc ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(disc_cue PROPERTIES FIXTURES_SETUP disc_images)

if(TARGET psxview-gui)
    add_test(NAME gui_arguments
        COMMAND ${CMAKE_COMMAND}
            -DGUI_EXECUTABLE=$<TARGET_FILE:psxview-gui>
            -DMISSING_IMAGE=${CMAKE_CURRENT_BINARY_DIR}/missing-gui-image.iso
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_gui_arguments.cmake)
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
