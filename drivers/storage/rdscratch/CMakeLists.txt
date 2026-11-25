
set(EXPAND_SOURCE_DIR ${REACTOS_SOURCE_DIR}/base/applications/cmdutils/lntexpandstorage)

add_library(rdscratch MODULE rdscratch.c rdscore.c ${EXPAND_SOURCE_DIR}/expandcore.c rdscratch.rc)
set_module_type(rdscratch kernelmodedriver)
target_include_directories(rdscratch PRIVATE ${EXPAND_SOURCE_DIR})
set_source_files_properties(${EXPAND_SOURCE_DIR}/expandcore.c PROPERTIES COMPILE_DEFINITIONS EXPAND_VOLUME_ONLY)
target_link_libraries(rdscratch memcmp)
add_importlibs(rdscratch ntoskrnl hal)
add_cd_file(TARGET rdscratch DESTINATION reactos/system32/drivers FOR all)
