
spec2def(ieframe.dll ieframe.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    classinfo.c
    client.c
    dochost.c
    events.c
    frame.c
    ie.c
    ieframe_main.c
    iehtmlwnd.c
    iexplore.c
    inetfolder.c
    intshcut.c
    intshcut_ext.c
    navigate.c
    oleobject.c
    persist.c
    shellbrowser.c
    shelluihelper.c
    urlhist.c
    urlsearchhook.c
    view.c
    webbrowser.c
    webbrowserext.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/ieframe_stubs.c)

set_source_files_properties(inetfolder.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_library(ieframe MODULE
    ${SOURCE}
    ieframe.rc
    $<TARGET_OBJECTS:ieframe_sublib>
    ${CMAKE_CURRENT_BINARY_DIR}/ieframe.def)

list(APPEND ieframe_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/ieframe.manifest
    ${CMAKE_CURRENT_SOURCE_DIR}/ieframe.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/intshcut.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/urlsearchhook.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/commband.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/shellnamespace.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/ietoolbar.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/pages.ico)

set_source_files_properties(ieframe.rc PROPERTIES OBJECT_DEPENDS "${ieframe_rc_deps}")
add_dependencies(ieframe stdole2)
set_module_type(ieframe win32dll)
add_idl_reg_scripts(ieframe registry ieframe_v1.idl)
add_idl_reg_scripts(ieframe regtypelib ieframe_v1.idl)
target_link_libraries(ieframe cpprt uuid wine wine_dll_register)
add_importlibs(ieframe urlmon shell32 comctl32 shlwapi oleaut32 ole32 user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_pch(ieframe precomp.h SOURCE)
add_cd_file(TARGET ieframe DESTINATION reactos/system32 FOR all)

set_wine_module_FIXME(ieframe) # CORE-5743: No CONST_VTABLE

add_library(ieframe_sublib OBJECT
    CCommBand.cpp
    CShellNameSpace.cpp
    objects.cpp)
get_target_property(ieframe_sublib_includes ieframe_sublib INCLUDE_DIRECTORIES)
list(REMOVE_ITEM ieframe_sublib_includes ${REACTOS_SOURCE_DIR}/sdk/include/wine ${REACTOS_BINARY_DIR}/sdk/include/wine)
set_target_properties(ieframe_sublib PROPERTIES INCLUDE_DIRECTORIES "${ieframe_sublib_includes}")
target_link_libraries(ieframe_sublib PRIVATE atl_classes)
target_compile_definitions(ieframe_sublib PRIVATE UNICODE _UNICODE)
target_compile_options(ieframe_sublib PRIVATE $<TARGET_PROPERTY:ieframe,COMPILE_OPTIONS>)
add_dependencies(ieframe_sublib psdk)
