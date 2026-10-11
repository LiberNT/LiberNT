spec2def(windows.storage.dll windows.storage.spec)
add_idl_headers(windows_storage_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    inetfolder.c
    main.c
    streams.c)

add_library(windows_storage MODULE
    ${SOURCE}
    windows.storage.rc
    $<TARGET_OBJECTS:windows_storage_sublib>
    ${CMAKE_CURRENT_BINARY_DIR}/windows.storage.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.storage_stubs.c)
set_target_properties(windows_storage PROPERTIES OUTPUT_NAME "windows.storage")
set_source_files_properties(windows.storage.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/storage.rgs)
add_idl_reg_scripts(windows_storage winrt classes.idl)
add_dependencies(windows_storage windows_storage_private_idl)
set_module_type(windows_storage win32dll)
target_compile_definitions(windows_storage PRIVATE __WINESRC__)
target_link_libraries(windows_storage cpprt uuid wine wine_dll_canunload)
add_delay_importlibs(windows_storage shdocvw)
add_importlibs(windows_storage shell32 shlwapi ole32 oleaut32 user32 advapi32 combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_storage DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(windows_storage)

add_library(windows_storage_sublib OBJECT
    CShellTaskScheduler.cpp
    mrulist.cpp
    objects.cpp)
get_target_property(windows_storage_sublib_includes windows_storage_sublib INCLUDE_DIRECTORIES)
list(REMOVE_ITEM windows_storage_sublib_includes ${REACTOS_SOURCE_DIR}/sdk/include/wine ${REACTOS_BINARY_DIR}/sdk/include/wine)
set_target_properties(windows_storage_sublib PROPERTIES INCLUDE_DIRECTORIES "${windows_storage_sublib_includes}")
target_link_libraries(windows_storage_sublib PRIVATE atl_classes)
target_compile_definitions(windows_storage_sublib PRIVATE UNICODE _UNICODE)
target_compile_options(windows_storage_sublib PRIVATE $<TARGET_PROPERTY:windows_storage,COMPILE_OPTIONS>)
add_dependencies(windows_storage_sublib psdk)
