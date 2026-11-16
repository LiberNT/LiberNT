file(GLOB_RECURSE NETCX_ADAPTER_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/adapter/*.cpp)
list(FILTER NETCX_ADAPTER_SOURCE EXCLUDE REGEX "/km-redirector/")
file(GLOB_RECURSE NETCX_EC_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/ec/*.cpp)
file(GLOB_RECURSE NETCX_STATISTICS_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/statistics/*.cpp)
file(GLOB_RECURSE NETCX_TRANSLATOR_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/translator/*.cpp)
file(GLOB_RECURSE NETCX_BM_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/bm/*.cpp)
file(GLOB NETCX_LIB_SOURCE CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} lib/*.cpp)

set(SOURCE
    ${NETCX_ADAPTER_SOURCE}
    ${NETCX_EC_SOURCE}
    ${NETCX_STATISTICS_SOURCE}
    ${NETCX_TRANSLATOR_SOURCE}
    ${NETCX_BM_SOURCE}
    ${NETCX_LIB_SOURCE})

set(NETCX_TMH_DIR ${CMAKE_CURRENT_BINARY_DIR}/tmh)
file(MAKE_DIRECTORY ${NETCX_TMH_DIR})
file(GLOB_RECURSE NETCX_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} netcx/*.hpp netcx/*.h)
foreach(_source ${SOURCE} ${NETCX_HEADERS})
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${_source} _tmh_lines REGEX "#include \"[A-Za-z0-9_]+\\.tmh\"")
    foreach(_line ${_tmh_lines})
        string(REGEX REPLACE ".*\"([A-Za-z0-9_]+\\.tmh)\".*" "\\1" _tmh ${_line})
        if(NOT EXISTS ${NETCX_TMH_DIR}/${_tmh})
            file(WRITE ${NETCX_TMH_DIR}/${_tmh} "")
        endif()
    endforeach()
endforeach()

set_source_files_properties(${NETCX_ADAPTER_SOURCE} PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/netcx/adapter/ndisminiport/km)
set_source_files_properties(${NETCX_EC_SOURCE} PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/netcx/ec/lib/km)
set_source_files_properties(${NETCX_STATISTICS_SOURCE} PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/netcx/statistics/km)

file(GLOB_RECURSE _netcx_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/netcx/* ${CMAKE_CURRENT_SOURCE_DIR}/rtl/*)
set(NETCX_INCLUDE_DIRS)
foreach(_dir ${_netcx_dirs})
    if(IS_DIRECTORY ${_dir} AND
       NOT _dir MATCHES "/km-redirector$" AND
       NOT _dir MATCHES "/netcx/adapter/ndisminiport/km$" AND
       NOT _dir MATCHES "/netcx/ec/lib/km$" AND
       NOT _dir MATCHES "/netcx/statistics/km$")
        list(APPEND NETCX_INCLUDE_DIRS ${_dir})
    endif()
endforeach()

add_library(netadaptercx MODULE ${SOURCE} netadaptercx.rc)
set_module_type(netadaptercx kmdfdriver KMDF_VERSION 1.33)
set_target_properties(netadaptercx PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS ON)

target_include_directories(netadaptercx BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/ndl
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers/ndis
    ${CMAKE_CURRENT_SOURCE_DIR}/netcx
    ${CMAKE_CURRENT_SOURCE_DIR}/rtl
    ${NETCX_INCLUDE_DIRS}
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/wil
    ${REACTOS_SOURCE_DIR}/sdk/include/ddk/netcx/kmdf/adapter/2.6
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk/netcx/shared/1.0)
target_include_directories(netadaptercx AFTER PRIVATE ${NETCX_TMH_DIR} ${REACTOS_SOURCE_DIR}/sdk/include/c++)

target_compile_definitions(netadaptercx PRIVATE
    NETCX_KMDF=1
    _KERNEL_MODE=1
    NET_VERSION_MAJOR=2
    NET_VERSION_MINOR=6)

target_compile_options(netadaptercx PRIVATE
    -U_WIN32_WINNT -D_WIN32_WINNT=0xA00
    -UNTDDI_VERSION -DNTDDI_VERSION=0x0A000011)

if(MSVC)
    target_compile_options(netadaptercx PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/inc/netcx_prelude.h)
else()
    target_compile_options(netadaptercx PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/inc/netcx_prelude.h)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(netadaptercx PRIVATE
        -Wno-unused-variable
        -Wno-unused-value
        -Wno-unused-function
        -Wno-unused-private-field
        -Wno-unused-but-set-variable
        -Wno-switch
        -Wno-missing-braces
        -Wno-invalid-offsetof
        -Wno-reorder
        -Wno-ignored-attributes
        -Wno-unknown-pragmas)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(netadaptercx PRIVATE
        -Wno-new-returns-null
        -Wno-tautological-constant-out-of-range-compare
        -Wno-tautological-compare
        -Wno-pragma-once-outside-header
        -Wno-macro-redefined
        -Wno-deprecated-enum-compare
        -Wno-inconsistent-missing-override
        -Wno-delete-non-abstract-non-virtual-dtor
        -Wno-delete-abstract-non-virtual-dtor
        -Wno-pragma-pack
        -Wno-microsoft-extra-qualification
        -Wno-enum-compare-switch
        -Wno-braced-scalar-init
        -Wno-unused-lambda-capture
        -Wno-logical-op-parentheses
        -Wno-void-pointer-to-int-cast)
endif()

target_link_libraries(netadaptercx uuid libcntpr ${PSEH_LIB})
add_importlibs(netadaptercx ndis ntoskrnl hal)
add_cd_file(TARGET netadaptercx DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(netadaptercx_reg.inf)
