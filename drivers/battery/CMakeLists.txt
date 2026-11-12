
add_subdirectory(battc)
if((ARCH STREQUAL "arm64") AND APPLE_SILICON_SUPPORT)
    add_subdirectory(applebatt)
endif()
