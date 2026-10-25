PROJECT(SHELL)

add_subdirectory(evalcmd)
add_subdirectory(shelldesktop)
add_subdirectory(shellmenu)
add_subdirectory(shellrecyclebin)

spec2def(shell32.dll shell32.spec ADD_IMPORTLIB)

add_definitions(
    -D_SHELL32_
    -D_WINE)

if(DBG)
    add_definitions(-D_DEBUG=1) # CORE-17505
endif()

list(APPEND SOURCE
    brfolder.cpp
    CActiveDesktop.cpp
    CActiveDesktop.h
    CIDLDataObj.cpp
    CNamespaceTreeControl.cpp
    changenotify.cpp
    windowpropertystore.cpp
    dialogs/dialogs.cpp
    dialogs/drive.cpp
    dialogs/drvdefext.cpp
    dialogs/filedefext.cpp
    dialogs/filetypes.cpp
    dialogs/folder_options.cpp
    dialogs/item_prop.cpp
    dialogs/linkprop.cpp
    dialogs/general.cpp
    dialogs/recycler_prop.cpp
    dialogs/view.cpp
    CDropTargetHelper.cpp
    CExtractIcon.cpp
    CRecycleBinCleaner.cpp
    folders.cpp
    iconcache.cpp
    propsheet.cpp
    shell32.cpp
    utils.cpp
    CFolderOptions.cpp
    folders/CMyDocsFolder.cpp
    folders/CNetFolder.cpp
    folders/CFontsFolder.cpp
    folders/CControlPanelFolder.cpp
    folders/CRecycleBin.cpp
    folders/CRecycleBinFolderViewCB.cpp
    droptargets/CexeDropHandler.cpp
    droptargets/CFSDropTarget.cpp
    droptargets/CRecyclerDropTarget.cpp
    prop.cpp
    shlexec.cpp
    shlfileop.cpp
    CDefView.cpp
    CDefViewUtil.cpp
    CDefViewDual.cpp
    CDefViewBckgrndMenu.cpp
    stubs.cpp
    systray.cpp
    CUserNotification.cpp
    CUserAssist.cpp
    COperationsProgressDialog.cpp
    CISFBand.cpp
    autocomplete/ACLCustomMRU.cpp
    autocomplete/aclhistory.cpp
    autocomplete/aclistisf.cpp
    autocomplete/aclmulti.cpp
    autocomplete/CAutoComplete.cpp
    bandproxy.cpp
    regtreeoptions.cpp
    CDefaultContextMenu.cpp
    COpenWithMenu.cpp
    CNewMenu.cpp
    CSendToMenu.cpp
    CCopyMoveToMenu.cpp
    CCopyAsPathMenu.cpp
    CStartMenuPin.cpp
    CEnumerableObjectCollection.cpp
    CDestinationList.cpp
    CTextPreviewHandler.cpp
    CFileOperation.cpp
    CShellDispatch.cpp
    CFolder.cpp
    CFolderItems.cpp
    CAutomationCM.cpp
    CCDBurn.cpp
    CFadeTask.cpp
    CFolderShortcut.cpp
    CShellFolderViewOC.cpp
    CShellSearchExt.cpp
    CThumbnail.cpp
    CShellWindows.cpp
    CFolderItemVerbs.cpp)

list(APPEND WINE_SOURCE
    appbar.c
    appmodel.c
    wine/clipboard.c
    wine/control.c
    wine/ebrowser.c
    wine/shell32_main.c
    wine/shellole.c
    wine/shellord.c
    wine/appassociation.c
    wine/shellreg.c
    wine/shellstring.c
    wine/shlmenu.c)

list(APPEND PCH_SKIP_SOURCE
    ${WINE_SOURCE}
    network_address.c
    vista.c
    ${CMAKE_CURRENT_BINARY_DIR}/shell32_stubs.c)

file(GLOB_RECURSE shell32_rc_deps res/*.*)
list(APPEND shell32_rc_deps
    ${CMAKE_CURRENT_BINARY_DIR}/shell32_shldisp.tlb
    ${CMAKE_CURRENT_SOURCE_DIR}/rgs_res.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/operationsprogress.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shresdef.h)
add_rc_deps(shell32.rc ${shell32_rc_deps})

add_library(shell32 MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    shell32.rc
    ${CMAKE_CURRENT_BINARY_DIR}/shell32.def)

target_compile_definitions(shell32 PRIVATE CONST_VTABLE)

if(WOW64_I386_RUNTIME AND ARCH STREQUAL "i386")
    target_compile_definitions(shell32 PRIVATE BUILD_WOW6432)
endif()

if(ARCH STREQUAL "arm64" AND ENABLE_FEX_ARM64EC AND EXISTS "${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec/CMakeLists.txt")
    target_compile_definitions(shell32 PRIVATE ENABLE_FEX_ARM64EC)
endif()

if(ENABLE_MESA_LLVMPIPE AND MESA_LLVMPIPE_FROM_SOURCE)
    target_compile_definitions(shell32 PRIVATE ENABLE_MESA_LLVMPIPE)
endif()

if(ENABLE_FFMPEG)
    target_compile_definitions(shell32 PRIVATE ENABLE_FFMPEG)
endif()

if(MSVC)
    # error C4311: 'type cast': pointer truncation from 'HANDLE' to 'INT'
    remove_target_compile_option(shell32 "/we4311")
endif()

add_typelib(shell32_shldisp.idl)

add_library(shell32_shared STATIC
    CEnumIDListBase.cpp
    debughlp.cpp
    nsutils.cpp
    shldataobject.cpp
    shlfolder.cpp)
target_compile_definitions(shell32_shared PRIVATE UNICODE _UNICODE CONST_VTABLE)
target_link_libraries(shell32_shared PRIVATE atl_classes libmsvcrt oldnames)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(shell32_shared PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>")
endif()
add_dependencies(shell32_shared psdk)
add_pch(shell32_shared precomp.h "")

set_module_type(shell32 win32dll UNICODE)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # FIXME: This is some ATL related nonsense that triggers GCC 13+ warnings
    target_compile_options(shell32 PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>")
endif()
target_link_libraries(shell32 shellmenu shelldesktop shell32_shared wine uuid recyclebin cpprt atl_classes oldnames)
if(DLL_EXPORT_VERSION GREATER_EQUAL 0x600)
    target_link_libraries(shell32 evalcmd)
endif()
add_delay_importlibs(shell32 powrprof shdocvw devmgr winspool.drv winmm mpr uxtheme ole32 oleaut32 userenv explorerframe version fmifs netapi32 secur32 gdiplus iphlpapi propsys oleacc)
add_importlibs(shell32 windows.storage advapi32 gdi32 user32 comctl32 comdlg32 shlwapi shcore msvcrt kernel32 ntdll)
add_dependencies(shell32 stdole2) # shell32_shldisp.tlb needs stdole2.tlb
add_pch(shell32 precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET shell32 DESTINATION reactos/system32 FOR all)
add_neutral_resource_file(shell32 shell32.rc shell32_mun.rc)
