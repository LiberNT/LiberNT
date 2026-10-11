
spec2def(certmgr.dll certmgr.spec)

list(APPEND SOURCE
    certmgr.cpp
    CCertMgrAbout.cpp
    CCertMgrSnapin.cpp
    precomp.h)

add_library(certmgr_snapin MODULE
    ${SOURCE}
    certmgr.rc
    ${CMAKE_CURRENT_BINARY_DIR}/certmgr.def)

set_target_properties(certmgr_snapin PROPERTIES OUTPUT_NAME "certmgr" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_module_type(certmgr_snapin win32dll UNICODE)
target_link_libraries(certmgr_snapin uuid mmc_uuids cpprt atl_classes)
add_importlibs(certmgr_snapin cryptui crypt32 version ole32 oleaut32 shlwapi user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(certmgr_snapin psdk)
add_pch(certmgr_snapin precomp.h SOURCE)
add_cd_file(TARGET certmgr_snapin DESTINATION reactos/system32 FOR all)
