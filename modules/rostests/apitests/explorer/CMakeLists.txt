
list(APPEND SOURCE
    SecondaryTaskbars.c
    StartSearchCommand.c
    VolumeOsd.c
    testlist.c)

add_executable(explorer_apitest ${SOURCE})
target_link_libraries(explorer_apitest wine)
set_module_type(explorer_apitest win32cui)
add_importlibs(explorer_apitest ole32 user32 gdi32 msvcrt kernel32)
add_rostests_file(TARGET explorer_apitest)
