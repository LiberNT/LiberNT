add_subdirectory(bdasup)
if(ARCH STREQUAL "arm64")
    add_subdirectory(appleisp)
endif()
