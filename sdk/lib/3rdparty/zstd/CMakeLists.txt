list(APPEND SOURCE
    lib/common/debug.c
    lib/common/entropy_common.c
    lib/common/error_private.c
    lib/common/fse_decompress.c
    lib/common/xxhash.c
    lib/common/zstd_common.c
    lib/decompress/huf_decompress.c
    lib/decompress/zstd_ddict.c
    lib/decompress/zstd_decompress.c
    lib/decompress/zstd_decompress_block.c)

add_library(zstd_decompress ${SOURCE})
target_include_directories(zstd_decompress PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_compile_definitions(zstd_decompress PRIVATE ZSTD_DISABLE_ASM DYNAMIC_BMI2=0 ZSTD_LEGACY_SUPPORT=0)
if(MSVC)
    target_compile_options(zstd_decompress PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/zstd_libernt.h)
else()
    target_compile_options(zstd_decompress PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/zstd_libernt.h)
endif()
add_dependencies(zstd_decompress xdk)
