
list(APPEND SOURCE
    MissingSecurity.c
    IndexMetadata.c
    testlist.c)

add_executable(ntfs_apitest ${SOURCE})
target_link_libraries(ntfs_apitest wine)
set_module_type(ntfs_apitest win32cui)
add_importlibs(ntfs_apitest advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET ntfs_apitest)
