
list(APPEND SOURCE
    CompileBuffer.c
    testlist.c)

add_executable(mofd_apitest ${SOURCE})
set_module_type(mofd_apitest win32cui)
target_link_libraries(mofd_apitest uuid)
add_importlibs(mofd_apitest ole32 oleaut32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET mofd_apitest)
