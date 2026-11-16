add_definitions(-D_KERNEL32_)
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys ..)

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
    modern.c)

set_source_files_properties(firmware.c PROPERTIES COMPILE_DEFINITIONS NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
add_library(kernel32_modern ${SOURCE})
target_link_libraries(kernel32_modern ${PSEH_LIB})
add_dependencies(kernel32_modern psdk)
