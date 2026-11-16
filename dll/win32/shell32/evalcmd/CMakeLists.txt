
list(APPEND SOURCE
    evalcmd.cpp
    elements.cpp)

add_library(evalcmd STATIC ${SOURCE})
add_dependencies(evalcmd psdk)
target_link_libraries(evalcmd PRIVATE wine uuid pathcch_static cppstl)
target_compile_definitions(evalcmd PRIVATE STATIC_PATHCCH)

target_compile_definitions(evalcmd PRIVATE _SHELL32_)
