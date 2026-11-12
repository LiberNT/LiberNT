
spec2def(lnttts.dll lnttts.spec)

list(APPEND SOURCE
    engine.c
    lts.c
    main.c
    synth.c)

set_source_files_properties(lnttts.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lnttts.rgs)

add_library(lnttts MODULE
    ${SOURCE}
    lnttts.rc
    ${CMAKE_CURRENT_BINARY_DIR}/lnttts.def)

add_dependencies(lnttts psdk)
set_module_type(lnttts win32dll)
target_link_libraries(lnttts uuid wine wine_dll_register)
add_importlibs(lnttts ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lnttts DESTINATION reactos/system32/Speech/Engines/TTS FOR all)
