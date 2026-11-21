
if(ARCH STREQUAL "amd64")
    add_library(ndk_tests win11_x64.c)
elseif(ARCH STREQUAL "i386")
    add_library(ndk_tests win10_x86.c)
endif()

if(TARGET ndk_tests)
    add_dependencies(ndk_tests xdk psdk bugcodes)
endif()
