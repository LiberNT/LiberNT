
list(APPEND SOURCE
    source/lunasvg.cpp
    source/graphics.cpp
    source/svgelement.cpp
    source/svggeometryelement.cpp
    source/svglayoutstate.cpp
    source/svgpaintelement.cpp
    source/svgparser.cpp
    source/svgproperty.cpp
    source/svgrenderstate.cpp
    source/svgtextelement.cpp)

add_library(lunasvg STATIC ${SOURCE})
set_property(TARGET lunasvg PROPERTY CXX_STANDARD 17)
set_property(TARGET lunasvg PROPERTY CXX_STANDARD_REQUIRED ON)
set_target_cpp_properties(lunasvg WITH_EXCEPTIONS WITH_RTTI)
target_include_directories(lunasvg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(lunasvg PUBLIC LUNASVG_BUILD_STATIC
                                   PRIVATE LUNASVG_BUILD LUNASVG_DISABLE_LOAD_SYSTEM_FONTS)
target_compile_options(lunasvg PRIVATE -w)
target_link_libraries(lunasvg PUBLIC plutovg cppstl)
add_dependencies(lunasvg psdk)
