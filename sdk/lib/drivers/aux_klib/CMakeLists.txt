
add_library(aux_klib aux_klib.c)
add_dependencies(aux_klib bugcodes xdk)
target_link_libraries(aux_klib ${PSEH_LIB})
add_importlibs(aux_klib ntoskrnl)
