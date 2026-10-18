add_library(wimlib
    src/add_image.c
    src/avl_tree.c
    src/blob_table.c
    src/compress.c
    src/compress_common.c
    src/compress_parallel.c
    src/compress_serial.c
    src/cpu_features.c
    src/decompress.c
    src/decompress_common.c
    src/delete_image.c
    src/dentry.c
    src/divsufsort.c
    src/encoding.c
    src/error.c
    src/export_image.c
    src/extract.c
    src/file_io.c
    src/header.c
    src/inode.c
    src/inode_fixup.c
    src/inode_table.c
    src/integrity.c
    src/iterate_dir.c
    src/join.c
    src/lcpit_matchfinder.c
    src/lzms_common.c
    src/lzms_compress.c
    src/lzms_decompress.c
    src/lzx_common.c
    src/lzx_compress.c
    src/lzx_decompress.c
    src/metadata_resource.c
    src/mount_image.c
    src/pathlist.c
    src/paths.c
    src/pattern.c
    src/progress.c
    src/reference.c
    src/registry.c
    src/reparse.c
    src/resource.c
    src/scan.c
    src/security.c
    src/sha1.c
    src/solid.c
    src/split.c
    src/tagged_items.c
    src/template.c
    src/textfile.c
    src/threads.c
    src/timestamp.c
    src/update_image.c
    src/util.c
    src/verify.c
    src/wim.c
    src/wimboot.c
    src/win32_apply.c
    src/win32_capture.c
    src/win32_common.c
    src/win32_replacements.c
    src/win32_vss.c
    src/write.c
    src/xml.c
    src/xml_windows.c
    src/xmlproc.c
    src/xpress_compress.c
    src/xpress_decompress.c)

target_include_directories(wimlib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(wimlib PRIVATE
    HAVE_CONFIG_H
    _DLL
    __USE_CRTIMP
    _CRT_DECLARE_NONSTDC_NAMES=1
    _POSIX
    _POSIX_THREAD_SAFE_FUNCTIONS
    UNICODE
    _UNICODE
    _CRT_NON_CONFORMING_SWPRINTFS
    __MINGW_USE_VC2005_COMPAT)
set_property(TARGET wimlib PROPERTY C_STANDARD 99)
if(NOT MSVC)
    target_compile_options(wimlib PRIVATE -mno-ms-bitfields -fno-common)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(wimlib PRIVATE -Wno-gnu-pointer-arith -Wno-pointer-sign)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(wimlib PRIVATE -Wno-pointer-arith -Wno-pointer-sign)
endif()
add_dependencies(wimlib psdk)
