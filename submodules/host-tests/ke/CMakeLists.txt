set(XSTATE_TEST_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/xstate-generated.c)
add_custom_command(OUTPUT ${XSTATE_TEST_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DXSTATE_SOURCE=${REACTOS_HOST_SOURCE_ROOT}/ntos/ke/amd64/xstate.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/xstate-fixture.c.in
            -DOUTPUT=${XSTATE_TEST_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-xstate.cmake
    DEPENDS ${REACTOS_HOST_SOURCE_ROOT}/ntos/ke/amd64/xstate.c
            ${CMAKE_CURRENT_SOURCE_DIR}/xstate-fixture.c.in ${CMAKE_CURRENT_SOURCE_DIR}/make-xstate.cmake
    VERBATIM)
add_executable(ke_xstate_layout ${XSTATE_TEST_SOURCE})
add_test(NAME ke.xstate_layout COMMAND ke_xstate_layout)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ke.time_adjustment COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/time-adjustment-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.timer_expired_list COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/timer-expired-list-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.timer_insertion COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/timer-insertion-test.py
            --source ${REACTOS_HOST_SOURCE_ROOT}/ntos/include/internal/ke_x.h --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.timer_setter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/timer-setter-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.wait_timer_cleanup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wait-timer-cleanup-test.py
            --source ${REACTOS_HOST_SOURCE_ROOT}/ntos/ke/wait.c --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.amd64_interrupt COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/amd64-interrupt-test.py
            --source ${REACTOS_HOST_SOURCE_ROOT}/ntos/ke/amd64/interrupt.c --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.amd64_smp COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/amd64-smp-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.bugcheck_freeze_failure COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bugcheck-freeze-failure-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.amd64_freeze_timeout COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/amd64-freeze-timeout-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ke.amd64_freeze_publication COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/amd64-freeze-publication-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
        add_test(NAME ex.workqueue_startup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/workqueue-startup-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    endif()
    find_program(KE_ARM64_CLANG NAMES clang)
    find_program(KE_ARM64_LD NAMES ld.lld)
    find_program(KE_ARM64_QEMU NAMES qemu-aarch64)
    find_program(KE_ARM64_NM NAMES llvm-nm)
    find_program(KE_ARM64_OBJDUMP NAMES llvm-objdump)
    if(Python3_Interpreter_FOUND AND KE_ARM64_CLANG AND KE_ARM64_LD AND KE_ARM64_QEMU AND KE_ARM64_NM AND KE_ARM64_OBJDUMP)
        add_test(NAME ke.arm64_atomic COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/arm64-atomic-test.py
            --source ${REACTOS_HOST_SOURCE_ROOT}/ntos/ke/arm64/rtlshim.c
            --clang ${KE_ARM64_CLANG} --ld ${KE_ARM64_LD} --qemu ${KE_ARM64_QEMU}
            --nm ${KE_ARM64_NM} --objdump ${KE_ARM64_OBJDUMP})
    endif()
endif()
