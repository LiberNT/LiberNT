
add_executable(dxgi_apitest
    adapter_removal.c
    dxgi.c
    format_support.cpp
    texture_dimensions.cpp
    composition.cpp
    composition_damage.cpp
    composition_source.cpp
    hwnd_source.cpp
    hwnd_damage.cpp
    composition_lifetime.cpp
    completion_event.cpp
    shared_fence.cpp
    shared_name.cpp
    gpu_priority.cpp
    kmt_outputdupl.cpp
    offer_resources.cpp
    subresource_surface.cpp
    feature_support.cpp
    shared_dimensions.cpp
    d3d10_bindings.cpp
    adapter_outputs.cpp
    counters.cpp
    query_order.cpp
    buffer_copy.cpp
    copy_depth_stencil.cpp
    volume_readback.cpp
    wait_vblank.cpp
    dwm_render.cpp
    viewport_levels.cpp
    testlist.c)

target_include_directories(dxgi_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/system/dwm)
set_module_type(dxgi_apitest win32cui)
add_importlibs(dxgi_apitest user32 gdi32 msvcrt kernel32)
add_rostests_file(TARGET dxgi_apitest)
