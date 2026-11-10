
spec2def(user32.dll user32.spec ADD_IMPORTLIB)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys
    include
    ${REACTOS_SOURCE_DIR}/sdk/include/wine
    ${REACTOS_SOURCE_DIR}/subsystems/win/win32k/include
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libpng)

list(APPEND SOURCE
    controls/appswitch.c
    controls/button.c
    controls/combo.c
    controls/edit.c
    controls/ghost.c
    controls/icontitle.c
    controls/listbox.c
    controls/regcontrol.c
    controls/scrollbar.c
    controls/static.c
    misc/dde.c
    misc/ddeclient.c
    misc/ddemisc.c
    misc/ddeserver.c
    misc/desktop.c
    misc/display.c
    misc/dllmain.c
    misc/exit.c
    misc/exticon.c
    misc/imm.c
    misc/logon.c
    misc/misc.c
    misc/object.c
    misc/resources.c
    misc/rtlstr.c
    misc/stubs.c
    misc/timer.c
    misc/usrapihk.c
    misc/winhelp.c
    misc/winsta.c
    misc/wallpaper.c
    windows/accel.c
    windows/caret.c
    windows/class.c
    windows/clipboard.c
    windows/cursoricon.c
    windows/dc.c
    windows/defwnd.c
    windows/dialog.c
    windows/draw.c
    windows/font.c
    windows/hook.c
    windows/input.c
    windows/mdi.c
    windows/menu.c
    windows/messagebox.c
    windows/message.c
    windows/nonclient.c
    windows/paint.c
    windows/prop.c
    windows/rect.c
    windows/spy.c
    windows/text.c
    windows/window.c
    windows/winpos.c
    ${REACTOS_SOURCE_DIR}/subsystems/win/win32k/user/rtl/image.c
    ${REACTOS_SOURCE_DIR}/subsystems/win/win32k/user/rtl/text.c
    ${CMAKE_CURRENT_BINARY_DIR}/user32_stubs.c
    include/user32.h)

if(ARCH STREQUAL "i386")
    list(APPEND ASM_SOURCE
        windows/wndproc_fixup.S)
endif()

add_asm_files(user32_asm ${ASM_SOURCE})

add_library(user32 MODULE
    ${SOURCE}
    ${user32_asm}
    user32.rc
    ${CMAKE_CURRENT_BINARY_DIR}/user32.def)

find_program(CURRES_PYTHON_EXECUTABLE NAMES python3 python REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cursors.in)
file(STRINGS cursors.in _user32_cursor_lines REGEX "CURSOR")
set(_user32_cursor_files "")
foreach(_user32_cursor_line IN LISTS _user32_cursor_lines)
    if(_user32_cursor_line MATCHES "\"([^\"]+)\"")
        list(APPEND _user32_cursor_files ${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_MATCH_1})
    endif()
endforeach()
list(REMOVE_DUPLICATES _user32_cursor_files)
set(_user32_cursors_res "")
set(_user32_curres_args "")
if(MSVC)
    # rc.exe takes no resource of a predefined type from a file, so the linker gets them compiled
    set(_user32_cursors_res ${CMAKE_CURRENT_BINARY_DIR}/user32_cursors.res)
    set(_user32_curres_args ${_user32_cursors_res} ${REACTOS_SOURCE_DIR}/sdk/include/psdk/winuser.h)
endif()
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/user32_cursors.rc ${_user32_cursors_res}
    COMMAND ${CURRES_PYTHON_EXECUTABLE} ${REACTOS_SOURCE_DIR}/sdk/tools/curres.py ${CMAKE_CURRENT_SOURCE_DIR}/cursors.in ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/user32_cursors.rc
        ${_user32_curres_args}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cursors.in ${REACTOS_SOURCE_DIR}/sdk/tools/curres.py ${_user32_cursor_files}
    VERBATIM)
set_source_files_properties(user32.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/user32_cursors.rc)
if(MSVC)
    set_source_files_properties(${_user32_cursors_res} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(user32 PRIVATE ${_user32_cursors_res})
endif()

if(WOW64_I386_RUNTIME AND ARCH STREQUAL "i386")
    target_compile_definitions(user32 PRIVATE WOW64_I386_RUNTIME)
endif()

set_module_type(user32 win32dll UNICODE ENTRYPOINT DllMain 12)
if((ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64") AND CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT USE_DUMMY_PSEH)
    target_compile_options(user32 PRIVATE -Xclang -fasync-exceptions)
endif()
if(NOT ARM64EC_RUNTIME)
    target_link_libraries(user32 user32_vista_static user32_optional_vista user32_wsprintf wine ${PSEH_LIB})
    add_importlibs(user32 win32u)
else()
    target_link_libraries(user32 user32_vista_static user32_optional_vista user32_wsprintf wine win32ksys ${PSEH_LIB})
endif()
add_dependencies(user32 asm)

add_delay_importlibs(user32 usp10 libpng)
add_importlibs(user32 gdi32 advapi32 kernel32 ntdll)
add_pch(user32 include/user32.h SOURCE)
add_cd_file(TARGET user32 DESTINATION reactos/system32 FOR all)
