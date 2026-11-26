
list(APPEND GUMBO_SOURCE
    src/gumbo/attribute.c
    src/gumbo/char_ref.c
    src/gumbo/error.c
    src/gumbo/parser.c
    src/gumbo/string_buffer.c
    src/gumbo/string_piece.c
    src/gumbo/tag.c
    src/gumbo/tokenizer.c
    src/gumbo/utf8.c
    src/gumbo/util.c
    src/gumbo/vector.c)

add_library(gumbo STATIC ${GUMBO_SOURCE})
target_include_directories(gumbo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gumbo/include
                                        ${CMAKE_CURRENT_SOURCE_DIR}/src/gumbo/include/gumbo)
target_compile_definitions(gumbo PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS strcasecmp=_stricmp strncasecmp=_strnicmp)
target_compile_options(gumbo PRIVATE -w)
add_dependencies(gumbo psdk)

list(APPEND LITEHTML_SOURCE
    src/codepoint.cpp
    src/css_length.cpp
    src/css_selector.cpp
    src/css_tokenizer.cpp
    src/css_parser.cpp
    src/document.cpp
    src/document_container.cpp
    src/el_anchor.cpp
    src/el_base.cpp
    src/el_before_after.cpp
    src/el_body.cpp
    src/el_break.cpp
    src/el_cdata.cpp
    src/el_comment.cpp
    src/el_div.cpp
    src/element.cpp
    src/el_font.cpp
    src/el_image.cpp
    src/el_link.cpp
    src/el_para.cpp
    src/el_script.cpp
    src/el_space.cpp
    src/el_style.cpp
    src/el_table.cpp
    src/el_td.cpp
    src/el_text.cpp
    src/el_title.cpp
    src/el_tr.cpp
    src/encodings.cpp
    src/html.cpp
    src/html_tag.cpp
    src/html_microsyntaxes.cpp
    src/iterators.cpp
    src/media_query.cpp
    src/style.cpp
    src/stylesheet.cpp
    src/table.cpp
    src/url.cpp
    src/url_path.cpp
    src/utf8_strings.cpp
    src/web_color.cpp
    src/num_cvt.cpp
    src/strtod.cpp
    src/string_id.cpp
    src/css_properties.cpp
    src/line_box.cpp
    src/css_borders.cpp
    src/render_item.cpp
    src/render_block_context.cpp
    src/render_block.cpp
    src/render_inline_context.cpp
    src/render_table.cpp
    src/render_flex.cpp
    src/render_image.cpp
    src/formatting_context.cpp
    src/flex_item.cpp
    src/flex_line.cpp
    src/background.cpp
    src/gradient.cpp)

add_library(litehtml STATIC ${LITEHTML_SOURCE})
set_property(TARGET litehtml PROPERTY CXX_STANDARD 17)
set_property(TARGET litehtml PROPERTY CXX_STANDARD_REQUIRED ON)
set_target_cpp_properties(litehtml WITH_EXCEPTIONS WITH_RTTI)
target_include_directories(litehtml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                           ${CMAKE_CURRENT_SOURCE_DIR}/include/litehtml
                                    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(litehtml PRIVATE -w -include cstdlib)
target_link_libraries(litehtml PUBLIC gumbo cppstl)
add_dependencies(litehtml psdk)
