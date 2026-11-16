
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include)

list(APPEND SOURCE
    pathcch_compile.c
    pathcch_compile.cpp
    precomp.h)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(pathcch_unittest ${SOURCE} ${PCH_SKIP_SOURCE})
add_pch(pathcch_unittest precomp.h ${PCH_SKIP_SOURCE})
set_module_type(pathcch_unittest win32cui)
add_dependencies(pathcch_unittest psdk)
target_link_libraries(pathcch_unittest pathcch_kernelbase)
add_importlibs(pathcch_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET pathcch_unittest)

add_executable(pathcch_dyn2_unittest ${SOURCE} ${PCH_SKIP_SOURCE})
add_pch(pathcch_dyn2_unittest precomp.h ${PCH_SKIP_SOURCE})
set_module_type(pathcch_dyn2_unittest win32cui)
add_dependencies(pathcch_dyn2_unittest psdk)
target_link_libraries(pathcch_dyn2_unittest pathcch)
add_importlibs(pathcch_dyn2_unittest msvcrt kernel32 ntdll)
## add_rostests_file(TARGET pathcch_dyn2_unittest) ## TODO: Requires api-ms-win-core-path-l1-1-0.dll
