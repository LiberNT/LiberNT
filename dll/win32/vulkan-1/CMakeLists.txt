remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

spec2def(vulkan-1.dll vulkan-1.spec)

include_directories(${REACTOS_SOURCE_DIR}/submodules/mesa/include)

list(APPEND SOURCE
    loader.c
    loader_gen.c
    ${CMAKE_CURRENT_BINARY_DIR}/vulkan-1_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/vulkan-1.def)

add_library(vulkan-1 MODULE ${SOURCE})
set_module_type(vulkan-1 win32dll)
add_importlibs(vulkan-1 gdi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET vulkan-1 DESTINATION reactos/system32 FOR all)
