find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FUSE3 QUIET IMPORTED_TARGET fuse3)
endif()

set(NTFSLIB_EXPAND_DIR "${NTFSLIB_ROOT}/../../../../base/applications/cmdutils/lntexpandstorage")

set(NTFSLIB_WIM_DIR "${NTFSLIB_ROOT}/../../libwim")

add_executable(ntfslib_fuse
    ntfslib_fuse.cpp
    "${NTFSLIB_WIM_DIR}/lzx.c"
    "${NTFSLIB_WIM_DIR}/sha1.c"
    "${NTFSLIB_WIM_DIR}/wimboot.c"
    "${NTFSLIB_WIM_DIR}/wimcore.c"
    "${NTFSLIB_WIM_DIR}/xpress_huff.c"
    "${NTFSLIB_EXPAND_DIR}/expandcore.c")
target_include_directories(ntfslib_fuse PRIVATE
    "${NTFSLIB_EXPAND_DIR}"
    "${NTFSLIB_ROOT}/../../../include/libernt/libs/libwim")
# The full resize core: disk-level expansion and offline system-file moves.
target_compile_definitions(ntfslib_fuse PRIVATE NTFSLIB_EXPAND_OFFLINE=1)
target_link_libraries(ntfslib_fuse PRIVATE ntfslib_linux ntfslib_format_host)

if(NTFSLIB_STATIC_CXX_RUNTIME AND
   CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(ntfslib_fuse PRIVATE
        -static-libstdc++
        -static-libgcc
    )
endif()

if(FUSE3_FOUND)
    find_package(Threads REQUIRED)
    target_compile_definitions(ntfslib_fuse PRIVATE NTFSLIB_HAVE_FUSE=1)
    target_link_libraries(ntfslib_fuse PRIVATE PkgConfig::FUSE3 Threads::Threads)
else()
    target_compile_definitions(ntfslib_fuse PRIVATE NTFSLIB_HAVE_FUSE=0)
    message(WARNING
        "libfuse3 development files were not found. ntfslib_fuse will "
        "support --probe/--list, but mounting requires libfuse3-dev.")
endif()
