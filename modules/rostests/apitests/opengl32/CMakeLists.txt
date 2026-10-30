
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)
add_executable(opengl32_apitest sw_extensions.c sw_pixelformat.c wgl_swap_control.c wgl_desktop.c wgl_streaming.c wgl_fence_readback.c wgl_hidden_present.c wgl_context_rebind.c wgl_icd_adapter.c wgl_flat_shading.c wgl_close_midrender.c wgl_front_facing.c wgl_scaled_attrib.c wgl_depth_blit.c wgl_render.c wgl_gears.c testlist.c)
target_link_libraries(opengl32_apitest wine)
set_module_type(opengl32_apitest win32cui)
add_importlibs(opengl32_apitest opengl32 gdi32 user32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET opengl32_apitest)
