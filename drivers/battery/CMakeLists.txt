
add_subdirectory(battc)
add_subdirectory(compbatt)
if(ARCH STREQUAL "arm64")
    add_subdirectory(applebatt)
endif()
