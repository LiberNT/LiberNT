
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl ${CMAKE_CURRENT_BINARY_DIR})
add_rpc_files(client ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/seclogon.idl)

list(APPEND SOURCE
    BcdStoreAccess.c
    AddMandatoryAce.c
    ControlServiceEx.c
    CopySid.c
    CreateProcessWithToken.c
    CreateService.c
    CryptCreateHash.c
    DuplicateTokenEx.c
    EnableTraceEx.c
    EnumerateTraceGuidsEx.c
    eventlog.c
    Hash.c
    HKEY_CLASSES_ROOT.c
    InitiateShutdown.c
    IntegrityLabels.c
    LogonSession.c
    LogonSessionEnd.c
    LogonUserNetwork.c
    IsTextUnicode.c
    LocalSystemPrivileges.c
    LockServiceDatabase.c
    LookupAccountName.c
    LookupAccountSid.c
    MachineGuid.c
    OpenTrace.c
    PrivilegeNames.c
    QueryServiceConfig2.c
    RegCreateKeyEx.c
    RegCreateKeyTransacted.c
    RegEnumKey.c
    RegEnumValueW.c
    RegFlushKey.c
    RegCreateKeyLink.c
    RegNotifyThreadAgnostic.c
    RegNotifyThreadExit.c
    RegLoadAppKey.c
    RegOpenKeyExW.c
    RegOpenKeyTransacted.c
    RegQueryInfoKey.c
    RegQueryValueExW.c
    RtlEncryptMemory.c
    SaferIdentifyLevel.c
    SaferLevels.c
    SaferRules.c
    SeclogonCaller.c
    ServiceArgs.c
    ServiceCrash.c
    ServiceEnv.c
    ServiceNetwork.c
    ServicePowerSetting.c
    StartServiceCtrlDispatcher.c
    TriggerInfo.c
    TokenBnoIsolation.c
    TokenLinkedToken.c
    TraceEvents.c
    TraceGuids.c
    UserHiveAccess.c
    svchlp.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c
    ${CMAKE_CURRENT_BINARY_DIR}/seclogon_c.c)

add_executable(advapi32_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(advapi32_apitest wine ${PSEH_LIB})
set_module_type(advapi32_apitest win32cui)
add_importlibs(advapi32_apitest advapi32 iphlpapi ws2_32 user32 netapi32 userenv rpcrt4 msvcrt kernel32 ntdll)
add_pch(advapi32_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET advapi32_apitest)
