
list(APPEND SOURCE
    allocator.cpp
    budget.cpp
    blend.cpp
    bundle.cpp
    device.cpp
    features.cpp
    heaps.cpp
    interfaces.cpp
    pipeline.cpp
    queue.cpp
    relaxed.cpp
    renderpass.cpp
    rootsig.cpp
    sampler.cpp
    shaders.cpp
    topology.cpp
    uav.cpp
    util.cpp
    versions.cpp
    testlist.c)

add_executable(d3d12_apitest ${SOURCE})
set_module_type(d3d12_apitest win32cui)
target_link_libraries(d3d12_apitest uuid dxguid)
add_importlibs(d3d12_apitest d3d12 dxgi user32 msvcrt kernel32)
add_rostests_file(TARGET d3d12_apitest)
