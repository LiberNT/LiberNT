
# vista.c enforces _WIN32_WINNT == _WIN32_WINNT_VISTA via #error.
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x600 -DWINVER=0x600)

add_definitions(-D_KERNEL32_)
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys ..)
spec2def(kernel32_vista.dll kernel32_vista.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ApplicationRecovery.c
    ApplicationRestart.c
    CompareObjectHandles.c
    DepPolicy.c
    DiscardVirtualMemory.c
    DllDirectory.c
    DynamicTimeZone.c
    FirefoxCompat.c
    firmware.c
    GetActiveProcessorCount.c
    GetEnabledXStateFeatures.c
    GetOverlappedResultEx.c
    GetFileInformationByHandleEx.c
    GetFinalPathNameByHandle.c
    GetLogicalProcessorInformationEx.c
    GetNamedPipeClientProcessId.c
    GetProcessGroupAffinity.c
    GetTickCount64.c
    InitOnce.c
    Packages.c
    ProcThreadAttributeList.c
    PrefetchVirtualMemory.c
    ProcessInformation.c
    ProcessMitigationPolicy.c
    SetDefaultDllDirectories.c
    SetFileInformationByHandle.c
    SetProcessValidCallTargets.c
    sync.c
    ThreadDescription.c
    ThreadGroupAffinity.c
    threadpool.c
    vista.c)

set_source_files_properties(firmware.c PROPERTIES COMPILE_DEFINITIONS NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
add_library(kernel32_vista_static ${SOURCE})
target_link_libraries(kernel32_vista_static ${PSEH_LIB})
add_dependencies(kernel32_vista_static psdk)

add_library(kernel32_vista MODULE
    DllMain.c
    kernel32_vista.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kernel32_vista.def)
set_module_type(kernel32_vista win32dll ENTRYPOINT DllMain 12)
target_link_libraries(kernel32_vista kernel32_vista_static kernel32_shared)
add_importlibs(kernel32_vista kernel32 ntdll_vista ntdll)
add_dependencies(kernel32_vista psdk)
add_cd_file(TARGET kernel32_vista DESTINATION reactos/system32 FOR all)
