# PROJECT:     ReactOS Direct3D 12
# LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
# PURPOSE:     Build the vkd3d-backed Direct3D 12 frontend
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>

spec2def(d3d12.dll d3d12.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d12_main.c)

if(REACTOS_USE_WDDM)
    remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
    add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)
    remove_definitions(-DD3D_UMD_INTERFACE_VERSION=${REACTOS_WDDM_EFFECTIVE_UMD_INTERFACE_VERSION})
    add_definitions(-DD3D_UMD_INTERFACE_VERSION=0x11000)
    list(APPEND NATIVE_SOURCE
        native12.cpp
        native12_device5.cpp
        native12_device10.cpp
        native12_objects.cpp
        native12_pipeline.cpp
        native12_queue.cpp
        native12_resource.cpp
        native12_swapchain.cpp)
    set_source_files_properties(${NATIVE_SOURCE} PROPERTIES
        COMPILE_DEFINITIONS "WIDL_EXPLICIT_AGGREGATE_RETURNS"
        INCLUDE_DIRECTORIES "${REACTOS_SOURCE_DIR}/sdk/include/ddk;${REACTOS_SOURCE_DIR}/sdk/include/psdk")
    list(APPEND SOURCE ${NATIVE_SOURCE})
endif()

add_library(d3d12 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12.def
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12_stubs.c)

set_module_type(d3d12 win32dll ENTRYPOINT 0)
target_include_directories(d3d12 PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/vkd3d/include)
if(REACTOS_USE_WDDM)
    target_compile_definitions(d3d12 PRIVATE REACTOS_D3D12_NATIVE_UMD __WINESRC__)
    target_link_libraries(d3d12 cpprt)
endif()
target_link_libraries(d3d12 dx10guid uuid wine)
if(REACTOS_USE_WDDM)
    add_importlibs(d3d12 win32u)
endif()
add_importlibs(d3d12 dxgi d3dwine gdi32 user32 ole32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(d3d12 d3d_idl_headers wineheaders)
add_cd_file(TARGET d3d12 DESTINATION reactos/system32 FOR all)

include(${REACTOS_SOURCE_DIR}/submodules/dxilconv.cmake)
