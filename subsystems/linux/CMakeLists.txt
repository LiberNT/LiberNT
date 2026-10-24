
if(ARCH STREQUAL "arm64" OR ARCH STREQUAL "riscv64")
    add_subdirectory(lxcore)
    add_subdirectory(lxlaunch)
    add_subdirectory(lxrun)
    add_subdirectory(lxterm)
    add_subdirectory(lxwayland)
    add_subdirectory(lxwinhost)
endif()
