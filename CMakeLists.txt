file(GLOB SRCS *.cpp *.h)

add_executable(myshell ${SRCS})

target_link_libraries(myshell PRIVATE mylibc)

target_link_options(myshell PRIVATE
    -nostdlib
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

set_target_properties(myshell PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

add_custom_command(TARGET myshell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOT_OUT_DIR}
    COMMAND objcopy -O binary
        --only-section=.text --only-section=.rodata --only-section=.data
        $<TARGET_FILE:myshell>
        ${BOOT_OUT_DIR}/myshell.o
    VERBATIM
)
