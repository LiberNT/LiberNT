
if(ARCH STREQUAL "amd64")
    add_library(ndk_tests win11_x64.c)
    add_dependencies(ndk_tests xdk psdk bugcodes)
endif()
# TODO: i386 layout test generated from the newest x86 kernel PDB
