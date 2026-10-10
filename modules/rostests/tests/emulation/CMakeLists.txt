
add_executable(emulation_test EXCLUDE_FROM_ALL main.c cxx.cpp)
set_target_cpp_properties(emulation_test WITH_EXCEPTIONS WITH_RTTI)
set_module_type(emulation_test win32cui)
target_link_libraries(emulation_test cpprt ${PSEH_LIB})
add_importlibs(emulation_test msvcrt kernel32 ntdll)
