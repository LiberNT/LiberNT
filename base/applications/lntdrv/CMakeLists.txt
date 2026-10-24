list(APPEND SOURCE
    catalog.cpp
    devices.cpp
    main.cpp
    text.cpp
    trust.cpp)

file(GLOB lntdrv_rc_deps lang/*.rc)
add_rc_deps(lntdrv.rc ${lntdrv_rc_deps}
    ${CMAKE_CURRENT_SOURCE_DIR}/lntdrv.exe.manifest
    ${CMAKE_CURRENT_SOURCE_DIR}/../mscutils/devmgmt/devmgmt.ico
    ${REACTOS_SOURCE_DIR}/base/setup/libernt/res/watermark.bmp)
add_executable(lntdrv ${SOURCE} lntdrv.rc)
set_property(TARGET lntdrv PROPERTY CXX_STANDARD 17)
set_property(TARGET lntdrv PROPERTY CXX_STANDARD_REQUIRED ON)
set_module_type(lntdrv win32gui UNICODE)
if(MSVC)
    set_target_cpp_properties(lntdrv WITH_EXCEPTIONS)
endif()
target_link_libraries(lntdrv cppstl cpprt uuid)
add_importlibs(lntdrv advapi32 bcrypt cfgmgr32 comctl32 crypt32 dwmapi gdi32 newdev setupapi shell32 user32 wininet wintrust msvcrt kernel32 ntdll)
add_cd_file(TARGET lntdrv DESTINATION reactos/system32 FOR all)
