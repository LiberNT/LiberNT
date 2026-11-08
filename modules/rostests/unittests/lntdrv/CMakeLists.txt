
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/applications/lntdrv)

list(APPEND SOURCE
    CatalogRules.cpp
    DeviceRules.cpp
    ${REACTOS_SOURCE_DIR}/base/applications/lntdrv/catalog.cpp
    ${REACTOS_SOURCE_DIR}/base/applications/lntdrv/devices.cpp
    ${REACTOS_SOURCE_DIR}/base/applications/lntdrv/text.cpp
    ${REACTOS_SOURCE_DIR}/base/applications/lntdrv/trust.cpp
    testlist.c)

add_executable(lntdrv_unittest ${SOURCE})
set_property(TARGET lntdrv_unittest PROPERTY CXX_STANDARD 17)
set_property(TARGET lntdrv_unittest PROPERTY CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set_target_cpp_properties(lntdrv_unittest WITH_EXCEPTIONS)
endif()
target_link_libraries(lntdrv_unittest cppstl cpprt uuid)
set_module_type(lntdrv_unittest win32cui)
target_compile_definitions(lntdrv_unittest PRIVATE UNICODE _UNICODE)
add_importlibs(lntdrv_unittest advapi32 bcrypt cfgmgr32 crypt32 newdev setupapi shell32 user32 wininet wintrust msvcrt kernel32 ntdll)
add_rostests_file(TARGET lntdrv_unittest)
