# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

set(LINUX_COMPAT_HEADERS
    asm/ioctl.h
asm/page.h
kunit/visibility.h
linux/align.h
linux/anon_inodes.h
linux/atomic.h
linux/average.h
linux/bitfield.h
linux/bitops.h
linux/bits.h
linux/bug.h
linux/build_bug.h
linux/byteorder/generic.h
linux/cleanup.h
linux/clk.h
linux/compiler_attributes.h
linux/compiler.h
linux/compiler_types.h
linux/completion.h
linux/const.h
linux/container_of.h
linux/cpumask.h
linux/dcache.h
linux/debugfs.h
linux/delay.h
linux/device.h
linux/dma-buf.h
linux/dma-buf-map.h
linux/dma-direction.h
linux/dma-fence-array.h
linux/dma-fence-chain.h
linux/dma-fence.h
linux/dma-fence-unwrap.h
linux/dma-mapping.h
linux/dma-resv.h
linux/elf.h
linux/err.h
linux/errno.h
linux/eventfd.h
linux/export.h
linux/file.h
linux/firmware.h
linux/fs.h
linux/gfp.h
linux/gfp_types.h
linux/highmem.h
linux/hrtimer.h
linux/idr.h
linux/interrupt.h
linux/interval_tree_generic.h
linux/interval_tree.h
linux/ioctl.h
linux/io.h
linux/iopoll.h
linux/iosys-map.h
linux/irq.h
linux/jiffies.h
linux/kconfig.h
linux/kernel.h
linux/kmemleak.h
linux/kobject.h
linux/kref.h
linux/kthread.h
linux/ktime_api.h
linux/ktime.h
linux/limits.h
linux/list.h
linux/llist.h
linux/lockdep.h
linux/log2.h
linux/math64.h
linux/math.h
linux/memory.h
linux/minmax.h
linux/mm.h
linux/module.h
linux/moduleparam.h
linux/mutex.h
linux/nospec.h
linux/of_address.h
linux/of_device.h
linux/of.h
linux/of_irq.h
linux/of_platform.h
linux/overflow.h
linux/pagemap.h
linux/platform_device.h
linux/pm_domain.h
linux/pm.h
linux/pm_runtime.h
linux/poll.h
linux/preempt.h
linux/printk.h
linux/property.h
linux/pwrseq/consumer.h
linux/random.h
linux/rbtree_augmented.h
linux/rbtree.h
linux/rcupdate.h
linux/refcount.h
linux/reset.h
linux/rwsem.h
linux/scatterlist.h
linux/sched/clock.h
linux/sched.h
linux/sched/signal.h
linux/seq_file.h
linux/shmem_fs.h
linux/sizes.h
linux/slab.h
linux/sort.h
linux/spinlock.h
linux/spinlock_types.h
linux/srcu.h
linux/stat.h
linux/stddef.h
linux/string.h
linux/string_helpers.h
linux/swab.h
linux/sync_file.h
linux/sysfs.h
linux/timer.h
linux/types.h
linux/uaccess.h
linux/vmalloc.h
linux/wait_bit.h
linux/wait.h
linux/workqueue.h
linux/ww_mutex.h
linux/xarray.h
uapi/linux/sched/types.h
)

set(LINUX_COMPAT_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
foreach(_header ${LINUX_COMPAT_HEADERS})
    set(_target ${LINUX_COMPAT_GENERATED_INCLUDE}/${_header})
    if(NOT EXISTS ${_target})
        file(WRITE ${_target} "#include <linux_compat.h>\n")
    endif()
endforeach()

list(APPEND SOURCE
    lc_device.c
    lc_fence.c
    lc_mem.c
    lc_nt.c
    lc_print.c
    lc_resv.c
    lc_sync.c
    lc_tree.c
    lc_work.c)

add_library(linux_compat ${SOURCE})
add_dependencies(linux_compat bugcodes xdk)
target_include_directories(linux_compat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LINUX_COMPAT_GENERATED_INCLUDE})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(linux_compat PRIVATE -Wno-unused-function)
endif()
