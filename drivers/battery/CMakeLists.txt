
add_subdirectory(battc)
add_subdirectory(compbatt)
if((ARCH STREQUAL "arm64") AND APPLE_SILICON_SUPPORT)
    add_subdirectory(applebatt)
endif()
