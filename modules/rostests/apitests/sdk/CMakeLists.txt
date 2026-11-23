
#
# Define three Delay-Import binary test programs:
# - "nohook": delayload is used, but neither of the hook/failurehook is defined;
# - "globalhook": delayload is used, and the hooks are assigned at global scope;
# - "runtimehook": delayload is used, and the hooks are assigned at runtime.
#
list(APPEND DELAYIMP_TEST_NAME nohook globalhook runtimehook)
list(APPEND DELAYIMP_TEST_INDEX 0 1 2)
foreach(testname testid IN ZIP_LISTS DELAYIMP_TEST_NAME DELAYIMP_TEST_INDEX)
    add_executable(delayimp_${testname}_apitest delayimp.cpp)
    target_compile_definitions(delayimp_${testname}_apitest PRIVATE DELAYIMP_TEST=${testid})
    set_module_type(delayimp_${testname}_apitest win32cui)
    target_link_libraries(delayimp_${testname}_apitest ${PSEH_LIB})
    add_importlibs(delayimp_${testname}_apitest msvcrt kernel32)
    add_delay_importlibs(delayimp_${testname}_apitest winmm version dbghelp shlwapi sfc_os imagehlp)
    add_rostests_file(TARGET delayimp_${testname}_apitest)
endforeach()

#add_executable(sdk_apitest testlist.c)
#set_module_type(sdk_apitest win32cui)
#target_link_libraries(sdk_apitest ${PSEH_LIB})
#add_importlibs(sdk_apitest msvcrt kernel32 ntdll)
#add_rostests_file(TARGET sdk_apitest)

foreach(target IN ITEMS sdk_header_abi sdk_header_abi_unicode sdk_header_abi_xp)
    add_library(${target} OBJECT header_abi.cpp)
    add_dependencies(${target} psdk xdk)
    target_compile_options(${target} PRIVATE -U_SETUPAPI_VER -U_WIN32_WINDOWS)
endforeach()
target_compile_definitions(sdk_header_abi_unicode PRIVATE
    UNICODE NOGDI USE_SP_INF_SIGNER_INFO_V1=1 USE_SP_ALTPLATFORM_INFO_V3=1)
target_compile_options(sdk_header_abi_xp PRIVATE
    -UWINVER -U_WIN32_WINNT -UNTDDI_VERSION
    -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000)
