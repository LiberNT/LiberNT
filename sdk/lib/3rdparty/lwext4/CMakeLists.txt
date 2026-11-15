
list(APPEND SOURCE
    src/ext4_balloc.c
    src/ext4_bcache.c
    src/ext4_bitmap.c
    src/ext4_block_group.c
    src/ext4_blockdev.c
    src/ext4_crc32.c
    src/ext4_debug.c
    src/ext4_dir.c
    src/ext4_dir_idx.c
    src/ext4_extent.c
    src/ext4_fs.c
    src/ext4_hash.c
    src/ext4_ialloc.c
    src/ext4_inode.c
    src/ext4_journal.c
    src/ext4_super.c
    src/ext4_trans.c
    src/ext4_xattr.c)

# The port supplies ext4_user_malloc and friends, ext4_port_dbg and ext4_port_assert_fail
add_library(lwext4 ${SOURCE})
target_include_directories(lwext4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(lwext4 PUBLIC
    CONFIG_USE_DEFAULT_CFG=1
    CONFIG_HAVE_PORT_DEBUG=1
    CONFIG_HAVE_OWN_ERRNO=0
    CONFIG_USE_USER_MALLOC=1
    CONFIG_UNALIGNED_ACCESS=1)
if(NOT MSVC)
    target_compile_options(lwext4 PRIVATE -w)
endif()
add_dependencies(lwext4 xdk)
