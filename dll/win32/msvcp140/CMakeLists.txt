
set(MSVCP90_DIR ${REACTOS_SOURCE_DIR}/dll/win32/msvcp90)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
include_directories(${MSVCP90_DIR})
get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

add_definitions(-D_MSVCP_VER=140 -D__WINESRC__ -D_CRT_DECLARE_NONSTDC_NAMES=1)
add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/msvcp140_crt.h)

spec2def(msvcp140.dll msvcp140.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${MSVCP90_DIR}/details.c
    ${MSVCP90_DIR}/exception.c
    ${MSVCP90_DIR}/ios.c
    ${MSVCP90_DIR}/locale.c
    ${MSVCP90_DIR}/math.c
    ${MSVCP90_DIR}/memory.c
    ${MSVCP90_DIR}/misc.c
    ${MSVCP90_DIR}/msvcp_main.c
    ${MSVCP90_DIR}/string.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp140_stubs.c)

add_library(msvcp140 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp140.def)

set_module_type(msvcp140 win32dll ENTRYPOINT DllMain 12)
target_link_libraries(msvcp140 wine pseh)
add_importlibs(msvcp140 vcruntime140 ucrtbase kernel32 ntdll)
add_dependencies(msvcp140 psdk)
add_cd_file(TARGET msvcp140 DESTINATION reactos/system32 FOR all)
