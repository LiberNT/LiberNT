
if(ARCH STREQUAL "arm64")
    add_subdirectory(lxcore)
    add_subdirectory(lxlaunch)
    add_subdirectory(lxrun)
    add_subdirectory(lxterm)
endif()
