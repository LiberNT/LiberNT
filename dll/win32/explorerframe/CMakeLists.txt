PROJECT(SHELL)

add_subdirectory(shellbars)
add_subdirectory(shellfind)

spec2def(explorerframe.dll explorerframe.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    addressband.cpp
    addresseditbox.cpp
    basebarsite.cpp
    dockingbarpropertybag.cpp
    brandband.cpp
    browseui.cpp
    browseuiord.cpp
    CTaskbarList.cpp
    CTaskbarList.h
    commonbrowser.cpp
    desktopipc.cpp
    executefolder.cpp
    globalfoldersettings.cpp
    internettoolbar.cpp
    parsecmdline.cpp
    settings.cpp
    toolsband.cpp
    travellog.cpp)

list(APPEND PCH_SKIP_SOURCE
    cabinet/bands.cpp
    cabinet/entry.cpp
    cabinet/site.cpp
    cabinet/window.cpp
    browseui_main.c
    compcatcachedaemon.c
    dllinstall.c)

set_source_files_properties(browseui_main.c PROPERTIES COMPILE_DEFINITIONS
    "DllMain=WineBrowseUI_DllMain;DllCanUnloadNow=WineBrowseUI_DllCanUnloadNow;DllGetClassObject=WineBrowseUI_DllGetClassObject;DllInstall=WineBrowseUI_DllInstall")
set_property(SOURCE
    browseui_main.c
    compcatcachedaemon.c
    APPEND PROPERTY COMPILE_DEFINITIONS __WINESRC__)

add_library(explorerframe MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    browseui.rc
    ${CMAKE_CURRENT_BINARY_DIR}/explorerframe.def)

add_rc_deps(browseui.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/cabinet/cabinet.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/01-back.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/02-forward.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/03-up.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/04-search.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/05-folders.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/06-move.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/07-copy.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/08-delete.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/09-undo.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/10-views.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/toolbar/11-dropdown.ico)

set_module_type(explorerframe win32dll UNICODE)
add_idl_reg_scripts(explorerframe registry browseui_classes.idl)
target_link_libraries(explorerframe shellbars shellfind shellframe uuid wine wine_dll_register cpprt atl_classes)
add_importlibs(explorerframe uxtheme shdocvw shlwapi shell32 comctl32 gdiplus dwmapi gdi32 ole32 oleaut32 user32 advapi32 mpr msvcrt kernel32 ntdll)
add_pch(explorerframe precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET explorerframe DESTINATION reactos/system32 FOR all)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(explorerframe PRIVATE -Wno-unused-but-set-variable)
endif()

if(NOT MSVC)
    # Binutils linker bug
    if(LTCG)
        target_link_options(explorerframe PRIVATE "-Wl,--allow-multiple-definition")
    endif()
endif()

add_custom_command(TARGET explorerframe POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
     "$<TARGET_FILE:explorerframe>"
     "$<TARGET_FILE_DIR:filebrowser>/$<TARGET_FILE_NAME:explorerframe>"
  COMMENT "Copying to output directory")
