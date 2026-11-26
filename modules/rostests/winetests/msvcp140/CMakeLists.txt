
spec2def(threaddll.dll threaddll.spec)
add_library(msvcp140_threaddll SHARED
    threaddll.c
    threaddll.def)
set_target_properties(msvcp140_threaddll PROPERTIES OUTPUT_NAME "threaddll")
set_entrypoint(msvcp140_threaddll DllMain 12)
add_dependencies(msvcp140_threaddll psdk)
add_importlibs(msvcp140_threaddll kernel32)

get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

add_definitions(-DWINETEST_USE_DBGSTR_LONGLONG)
add_compile_options(-include ${REACTOS_SOURCE_DIR}/dll/win32/msvcp140/msvcp140_crt.h)

list(APPEND SOURCE
    msvcp140.c
    testlist.c
    msvcp140_winetest.rc)

set_source_files_properties(msvcp140_winetest.rc OBJECT_DEPENDS msvcp140_threaddll)

add_executable(msvcp140_winetest ${SOURCE})
set_module_type(msvcp140_winetest win32cui)
target_link_libraries(msvcp140_winetest wine ucrtoldnames ${PSEH_LIB} chkstk)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(msvcp140_winetest -lgcc)
endif()
add_importlibs(msvcp140_winetest ucrtbase kernel32 ntdll)
add_dependencies(msvcp140_winetest msvcp140_threaddll)
add_rostests_file(TARGET msvcp140_winetest)
