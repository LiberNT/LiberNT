spec2def(windows.storage.dll windows.storage.spec ADD_IMPORTLIB)
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
    $<TARGET_OBJECTS:windows_storage_shell>
    ${CMAKE_CURRENT_BINARY_DIR}/windows.storage.def
    ${CMAKE_CURRENT_BINARY_DIR}/windows.storage_stubs.c)
set_target_properties(windows_storage PROPERTIES OUTPUT_NAME "windows.storage")
file(GLOB windows_storage_rc_deps ${CMAKE_CURRENT_SOURCE_DIR}/lang/*.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/rgs/*.rgs)
list(APPEND windows_storage_rc_deps ${CMAKE_CURRENT_SOURCE_DIR}/storage.rgs ${REACTOS_SOURCE_DIR}/dll/win32/shell32/shresdef.h)
add_rc_deps(windows.storage.rc ${windows_storage_rc_deps})
set_source_files_properties(windows.storage.rc PROPERTIES INCLUDE_DIRECTORIES ${REACTOS_SOURCE_DIR}/dll/win32/shell32)
add_idl_reg_scripts(windows_storage winrt classes.idl)
add_dependencies(windows_storage windows_storage_private_idl)
set_module_type(windows_storage win32dll)
target_compile_definitions(windows_storage PRIVATE __WINESRC__)
target_link_libraries(windows_storage shell32_shared cpprt atl_classes uuid wine wine_dll_canunload oldnames)
add_delay_importlibs(windows_storage shell32 shdocvw comctl32 comdlg32 mpr netapi32 propsys userenv version)
add_importlibs(windows_storage shlwapi shcore gdi32 ole32 oleaut32 user32 advapi32 combase msvcrt kernel32 ntdll)
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

list(APPEND SHELL_SOURCE
    CFileSysBindData.cpp
    CKnownFolderManager.cpp
    CQueryAssociations.cpp
    CShellItem.cpp
    CShellLink.cpp
    folders/CDesktopFolder.cpp
    folders/CDrivesFolder.cpp
    folders/CFSFolder.cpp
    folders/CRegFolder.cpp
    shellmodule.cpp)

list(APPEND SHELL_WINE_SOURCE
    wine/classes.c
    wine/pidl.c
    wine/shellpath.c
    wine/shpolicy.c)

add_library(windows_storage_shell OBJECT ${SHELL_SOURCE} ${SHELL_WINE_SOURCE})
get_target_property(windows_storage_shell_includes windows_storage_shell INCLUDE_DIRECTORIES)
list(REMOVE_ITEM windows_storage_shell_includes ${REACTOS_SOURCE_DIR}/sdk/include/wine ${REACTOS_BINARY_DIR}/sdk/include/wine)
list(APPEND windows_storage_shell_includes
    ${REACTOS_SOURCE_DIR}/dll/win32/shell32
    ${REACTOS_SOURCE_DIR}/dll/win32/shell32/wine
    ${REACTOS_SOURCE_DIR}/dll/win32/shell32/folders
    ${REACTOS_BINARY_DIR}/dll/win32/shell32)
set_target_properties(windows_storage_shell PROPERTIES INCLUDE_DIRECTORIES "${windows_storage_shell_includes}")
target_link_libraries(windows_storage_shell PRIVATE atl_classes libmsvcrt oldnames)
target_compile_definitions(windows_storage_shell PRIVATE _SHELL32_ _WINE UNICODE _UNICODE CONST_VTABLE)
if(DBG)
    target_compile_definitions(windows_storage_shell PRIVATE _DEBUG=1)
endif()
target_compile_options(windows_storage_shell PRIVATE $<TARGET_PROPERTY:windows_storage,COMPILE_OPTIONS>)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(windows_storage_shell PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>")
endif()
add_dependencies(windows_storage_shell psdk)
add_pch(windows_storage_shell ${REACTOS_SOURCE_DIR}/dll/win32/shell32/precomp.h "${SHELL_WINE_SOURCE}")
