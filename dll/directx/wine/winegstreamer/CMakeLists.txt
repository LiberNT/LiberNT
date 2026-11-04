add_definitions(-D__WINESRC__)

spec2def(winegstreamer.dll winegstreamer.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    reactos/main.c)

add_library(winegstreamer MODULE
    ${SOURCE}
    reactos/rsrc.rc
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/winegstreamer.def)

set_source_files_properties(reactos/rsrc.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/winegstreamer.rgs")
set_module_type(winegstreamer win32dll)
target_link_libraries(winegstreamer strmiids dxguid uuid wine wine_dll_register ${PSEH_LIB})
add_importlibs(winegstreamer advapi32 msvfw32 ole32 oleaut32 user32 winmm msvcrt kernel32 ntdll)
add_dependencies(winegstreamer dxsdk)
add_cd_file(TARGET winegstreamer DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(winegstreamer)
