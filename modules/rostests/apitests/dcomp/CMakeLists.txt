add_executable(dcomp_apitest
    composition_present.c
    visual.c
    testlist.c)

set_module_type(dcomp_apitest win32cui)
target_link_libraries(dcomp_apitest uuid dxguid)
add_importlibs(dcomp_apitest user32 gdi32 msvcrt kernel32)
add_rostests_file(TARGET dcomp_apitest)
