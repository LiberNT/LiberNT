
add_executable(rdp_apitest ClearCodec.c ${REACTOS_SOURCE_DIR}/base/services/rdpserver/clearenc.c ControllerMode.c
               FrameMove.c ${REACTOS_SOURCE_DIR}/base/services/rdpserver/framemove.c RemoteDesktop.c Zgfx.c testlist.c)
target_include_directories(rdp_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/services/rdpserver)
target_compile_definitions(rdp_apitest PRIVATE __STDC_NO_THREADS__=1)
target_link_libraries(rdp_apitest freerdp winpr libssl libcrypto uuid oldnames)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(rdp_apitest libgcc_eh)
endif()
set_module_type(rdp_apitest win32cui)
add_importlibs(rdp_apitest advapi32 crypt32 dbghelp ncrypt ole32 rpcrt4 secur32 shell32 shlwapi user32 ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET rdp_apitest)
