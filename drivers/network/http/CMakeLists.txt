
include_directories(
    BEFORE .
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/llhttp/include)

list(APPEND SOURCE
    http.c
    llhttp/src/api.c
    llhttp/src/http.c
    llhttp/src/llhttp.c
    http_sys.h
    request.h)

add_library(http MODULE ${SOURCE} http.rc)
target_link_libraries(http uuid)
set_source_files_properties(
    http.c
    PROPERTIES COMPILE_DEFINITIONS LIBERNT_KERNEL)
set_source_files_properties(
    llhttp/src/api.c
    llhttp/src/http.c
    llhttp/src/llhttp.c
    PROPERTIES COMPILE_DEFINITIONS "LIBERNT_KERNEL;NDEBUG")
set_module_type(http kernelmodedriver)
add_importlibs(http ntoskrnl netio hal)
add_cd_file(TARGET http DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(http_reg.inf)
