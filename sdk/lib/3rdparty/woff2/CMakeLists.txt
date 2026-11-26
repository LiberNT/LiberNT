
list(APPEND SOURCE
    src/table_tags.cc
    src/variable_length.cc
    src/woff2_common.cc
    src/woff2_dec.cc
    src/woff2_out.cc)

add_library(woff2dec STATIC ${SOURCE})
target_include_directories(woff2dec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_property(TARGET woff2dec PROPERTY CXX_STANDARD 17)
set_target_cpp_properties(woff2dec WITH_EXCEPTIONS WITH_RTTI)
target_compile_definitions(woff2dec PRIVATE _DLL __USE_CRTIMP)
target_compile_options(woff2dec PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/woff2_libernt.h)
target_link_libraries(woff2dec brotlidec cppstl)
add_dependencies(woff2dec psdk)
