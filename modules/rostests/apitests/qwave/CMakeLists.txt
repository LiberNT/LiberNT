
list(APPEND SOURCE
    QosAdaptive.c
    QosFlows.c
    QosMarking.c
    testlist.c)

add_executable(qwave_apitest ${SOURCE})
set_module_type(qwave_apitest win32cui)
add_importlibs(qwave_apitest qwave ws2_32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET qwave_apitest)
