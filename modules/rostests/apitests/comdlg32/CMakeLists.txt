
add_executable(comdlg32_apitest PickFolders.c testlist.c)
target_link_libraries(comdlg32_apitest uuid)
set_module_type(comdlg32_apitest win32cui)
add_importlibs(comdlg32_apitest ole32 shell32 shlwapi user32 msvcrt kernel32)
add_rostests_file(TARGET comdlg32_apitest)
