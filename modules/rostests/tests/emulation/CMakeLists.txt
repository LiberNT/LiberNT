
add_executable(emulation_test EXCLUDE_FROM_ALL main.c cxx.cpp)
set_target_cpp_properties(emulation_test WITH_EXCEPTIONS WITH_RTTI)
set_module_type(emulation_test win32cui)
target_link_libraries(emulation_test cpprt ${PSEH_LIB})
add_importlibs(emulation_test msvcrt kernel32 ntdll)

add_executable(emulation_gui EXCLUDE_FROM_ALL gui.c)
set_module_type(emulation_gui win32cui)
add_importlibs(emulation_gui opengl32 gdi32 user32 msvcrt kernel32)

if(RV64X_COMPILER_LAUNCHER)
    spec2def(rv64xhost.dll rv64xhost.spec)
    add_library(rv64xhost MODULE rv64xhost.c ${CMAKE_CURRENT_BINARY_DIR}/rv64xhost.def)
    set_module_type(rv64xhost win32dll ENTRYPOINT 0)
    set_target_properties(rv64xhost PROPERTIES C_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER})
    if(NOT RV64X_SYSTEM)
        target_compile_options(rv64xhost PRIVATE -mllvm -riscv-rv64x)
    endif()
    add_importlibs(rv64xhost amd64emu ntdll)
    add_cd_file(TARGET rv64xhost DESTINATION reactos/system32 FOR all)

    add_executable(emulation_test_rv64x EXCLUDE_FROM_ALL main.c cxx.cpp)
    set_target_cpp_properties(emulation_test_rv64x WITH_EXCEPTIONS WITH_RTTI)
    set_module_type(emulation_test_rv64x win32cui)
    set_target_properties(emulation_test_rv64x PROPERTIES
        C_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER}
        CXX_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER})
    if(NOT RV64X_SYSTEM)
        target_compile_options(emulation_test_rv64x PRIVATE -mllvm -riscv-rv64x)
    endif()
    target_link_libraries(emulation_test_rv64x cpprt ${PSEH_LIB})
    add_importlibs(emulation_test_rv64x msvcrt kernel32 ntdll)
endif()
