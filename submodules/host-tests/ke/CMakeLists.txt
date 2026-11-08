set(XSTATE_TEST_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/xstate-generated.c)
add_custom_command(OUTPUT ${XSTATE_TEST_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DXSTATE_SOURCE=${REACTOS_HOST_SOURCE_ROOT}/ntoskrnl/ke/amd64/xstate.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/xstate-fixture.c.in
            -DOUTPUT=${XSTATE_TEST_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-xstate.cmake
    DEPENDS ${REACTOS_HOST_SOURCE_ROOT}/ntoskrnl/ke/amd64/xstate.c
            ${CMAKE_CURRENT_SOURCE_DIR}/xstate-fixture.c.in ${CMAKE_CURRENT_SOURCE_DIR}/make-xstate.cmake
    VERBATIM)
add_executable(ke_xstate_layout ${XSTATE_TEST_SOURCE})
add_test(NAME ke.xstate_layout COMMAND ke_xstate_layout)
