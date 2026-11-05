
list(APPEND SOURCE
    allocator.cpp
    barrier.cpp
    budget.cpp
    bgwork.cpp
    blend.cpp
    bundle.cpp
    capsuse.cpp
    ddibuild.cpp
    device.cpp
    dred.cpp
    factory.cpp
    fencekill.cpp
    mesh.cpp
    feedback.cpp
    features.cpp
    formatsupport.cpp
    heaps.cpp
    interfaces.cpp
    pipeline.cpp
    pipelib.cpp
    queue.cpp
    raster.cpp
    relaxed.cpp
    renderpass.cpp
    rootsig.cpp
    rootstate.cpp
    sharedfence.cpp
    sampler.cpp
    shaders.cpp
    stablepower.cpp
    swapchain.cpp
    swapformat.cpp
    swizzle.cpp
    topology.cpp
    uav.cpp
    util.cpp
    versions.cpp
    video.cpp
    viewinst.cpp
    vrs.cpp
    testlist.c)

set_source_files_properties(ddibuild.cpp PROPERTIES COMPILE_FLAGS "-UD3D_UMD_INTERFACE_VERSION -DD3D_UMD_INTERFACE_VERSION=0x11000")
add_executable(d3d12_apitest ${SOURCE})
set_module_type(d3d12_apitest win32cui)
target_compile_definitions(d3d12_apitest PRIVATE WIDL_EXPLICIT_AGGREGATE_RETURNS)
target_link_libraries(d3d12_apitest uuid dxguid)
add_importlibs(d3d12_apitest d3d12 dxgi user32 gdi32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET d3d12_apitest)
