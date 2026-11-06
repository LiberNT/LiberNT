
add_definitions(-DWINETEST_USE_DBGSTR_LONGLONG)
add_executable(mp3dmod_winetest mp3dmod.c testlist.c)
target_link_libraries(mp3dmod_winetest dmoguids strmiids uuid wmcodecdspuuid)
set_module_type(mp3dmod_winetest win32cui)
add_importlibs(mp3dmod_winetest msdmo ole32 msvcrt kernel32)
add_rostests_file(TARGET mp3dmod_winetest)
