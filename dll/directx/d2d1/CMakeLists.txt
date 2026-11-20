spec2def(d2d1.dll d2d1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    bitmap.c
    bitmap_render_target.c
    brush.c
    command_list.c
    coverage.c
    dc_render_target.c
    device.c
    device_context.c
    device_draw.c
    effect.c
    effect_builtins.c
    effect_context.c
    effect_draw_info.c
    effect_properties.c
    effect_transform.c
    factory.c
    factory_effects.c
    geometry.c
    geometry_cache.c
    geometry_ops.c
    geometry_shapes.c
    hwnd_render_target.c
    layer.c
    mesh.c
    shader.c
    state_block.c
    stroke.c
    svg.c
    text.c
    wic_render_target.c
    d2d1_command_list.h
    d2d1_common.h
    d2d1_device.h
    d2d1_effect.h
    d2d1_factory.h
    d2d1_geometry.h
    d2d1_private.h
    d2d1_resources.h
    device_private.h
    effect_private.h
    factory_private.h
    geometry_private.h)

add_library(d2d1 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d2d1.def
    ${CMAKE_CURRENT_BINARY_DIR}/d2d1_stubs.c)

set_module_type(d2d1 win32dll)
target_compile_definitions(d2d1 PRIVATE __WINESRC__ _USE_MATH_DEFINES)
target_link_libraries(d2d1 dx10guid uuid wine)
add_importlibs(d2d1 d3d10_1 d3dcompiler_47 gdi32 user32 advapi32 msvcrt ucrtbase kernel32 ntdll)
add_delay_importlibs(d2d1 dwrite xmllite ole32)
add_dependencies(d2d1 d3d_idl_headers psdk)
add_cd_file(TARGET d2d1 DESTINATION reactos/system32 FOR all)
set_wine_module(d2d1)

if(MSVC)
    target_compile_options(d2d1 PRIVATE /wd4090 /wd4146)
    remove_target_compile_option(d2d1 "/we4098")
else()
    target_compile_definitions(d2d1 PRIVATE isinf=__builtin_isinf)
endif()
