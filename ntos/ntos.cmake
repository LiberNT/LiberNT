
include_directories(
    ${REACTOS_SOURCE_DIR}
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/arblib
    ${REACTOS_SOURCE_DIR}/sdk/lib/cmlib
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include/internal
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

add_definitions(
    -D_NTOSKRNL_
    -D_NTSYSTEM_
    -DEX_LEGACY_PUSH_LOCKS
    -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI})
set_source_files_properties(${REACTOS_SOURCE_DIR}/ntos/kd64/kdwatchdog.c PROPERTIES COMPILE_DEFINITIONS "KD_LOG_WATCHDOG_DEFAULT_SECONDS=${_KD_LOG_WATCHDOG_DEFAULT_SECONDS}")

# KiBugCheckDebugBreak relies on native SEH catching the deliberate breakpoint.
# Clang otherwise only models synchronous exceptions at selected instructions.
if((ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64") AND CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT USE_DUMMY_PSEH)
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/bug.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/bug.c APPEND PROPERTY COMPILE_OPTIONS -Xclang -fasync-exceptions)
endif()

if(ARCH STREQUAL "arm64")
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/irql.c ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/spinlock.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/irql.c ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/spinlock.c APPEND PROPERTY COMPILE_DEFINITIONS _NTHALDLL_)
endif()

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/ntos/config/cmalloc.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmapi.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmboot.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmconfig.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmcontrl.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmdata.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmdelay.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmhook.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmhvlist.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cminit.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkcbncb.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmlazy.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmmapvw.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmnotify.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmparse.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmquota.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmse.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmsecach.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmsysini.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmtrans.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmvalche.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmwraprs.c
    ${REACTOS_SOURCE_DIR}/ntos/config/ntapi.c
    ${REACTOS_SOURCE_DIR}/ntos/dbgk/dbgkobj.c
    ${REACTOS_SOURCE_DIR}/ntos/dbgk/dbgkutil.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/atom.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/callback.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/dbgctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/efi.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/event.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/evtpair.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exintrin.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/fmutex.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/handle.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/harderr.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/hdlsterm.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/init.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/interlocked.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/keyedevt.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/license.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/locale.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/lookas.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/mutant.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/partition.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/profile.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/pushlock.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/resource.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/reserve.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/rundown.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/sem.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/shutdown.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/spinlock.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/sysinfo.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/time.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/timer.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/uuid.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/wddm.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/win32k.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/wnf.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/work.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/xipdisp.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/zone.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/dbcsname.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fastio.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/faulttol.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/bytelock.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/cancelwait.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/filter.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/filtrctx.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fsfilter.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fsrtlpc.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/largemcb.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/mcb.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/name.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/notify.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/oplock.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/pnp.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/stackovf.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/tunnel.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/unc.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/disksup.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/fstubex.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/halstub.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/translate.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/bootanim.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/inbv.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/inbvgop.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/inbvport.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/adapter.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/arcname.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/bootlog.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/controller.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/device.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/deviface.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/bootcb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/driver.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/error.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/file.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocomp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/waitcomp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioevent.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iofunc.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iommu.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ksr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iomdl.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iomgr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iorsrce.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iotimer.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iowork.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/irp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/irq.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ramdisk.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/rawfs.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/remlock.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/symlink.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/util.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/volume.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/rootarb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/dif.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/devaction.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/devnode.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/plugplay.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpdma.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpinit.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpirp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpmap.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpmgr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpnotify.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpreport.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnppar.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpres.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpresdb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnproot.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnputil.c
    ${REACTOS_SOURCE_DIR}/ntos/io/debug.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdapi.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdbreak.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kddata.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kddump.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdfile.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdinit.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdlock.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdlog.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdprint.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdtrap.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/apc.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/balmgr.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/bug.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/clock.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/config.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/devqueue.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/dpc.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/eventobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/except.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/freeze.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/gate.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/gmutex.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/ipi.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/passint.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/krnlinit.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/mutex.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procgrp.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/processor.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procinfo.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/profobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/queue.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/semphobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/smpdbg.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/spinlock.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/thrdobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/thrdschd.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/time.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/timerobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/wait.c
    ${REACTOS_SOURCE_DIR}/ntos/kse/kse.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/va/varegion.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heapcore.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heapseg.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heaplfh.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heapvs.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heaplarge.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/heap/heapdebug.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/track/tagtable.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/ex/exinit.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/ex/exalloc.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/ex/exfree.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/ex/exquota.c
    ${REACTOS_SOURCE_DIR}/ntos/mm/pool/ex/exquery.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obcallback.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obdevmap.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obdirectory.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obhandles.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obstartup.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/oblifetime.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obsymlink.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obns.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obnamespace.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obreference.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obsdcache.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obsecurity.c
    ${REACTOS_SOURCE_DIR}/ntos/ob/obsync.c
    ${REACTOS_SOURCE_DIR}/ntos/po/events.c
    ${REACTOS_SOURCE_DIR}/ntos/po/poshtdwn.c
    ${REACTOS_SOURCE_DIR}/ntos/po/povolume.c
    ${REACTOS_SOURCE_DIR}/ntos/po/pep.c
    ${REACTOS_SOURCE_DIR}/ntos/po/pofx.c
    ${REACTOS_SOURCE_DIR}/ntos/po/power.c
    ${REACTOS_SOURCE_DIR}/ntos/po/powerlimit.c
    ${REACTOS_SOURCE_DIR}/ntos/po/hiber.c
    ${REACTOS_SOURCE_DIR}/ntos/po/sleep.c
    ${REACTOS_SOURCE_DIR}/ntos/po/standby.c
    ${REACTOS_SOURCE_DIR}/ntos/po/thermal.c
    ${REACTOS_SOURCE_DIR}/ntos/po/ttm.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/apiset.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/apphelp.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/debug.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/energy.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/job.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/kill.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pico.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/process.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/property.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psmgr.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psnotify.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/query.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/quota.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/security.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/state.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/thread.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/tls.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/win32.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/avltree.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/rbtree.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/cmresource.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/guid.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/libsupp.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/locale.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/misc.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/security.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/system.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/time.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/unicode.c
    ${REACTOS_SOURCE_DIR}/ntos/rtl/wddm.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seaccess.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seaccesschk.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seacl.c
    ${REACTOS_SOURCE_DIR}/ntos/se/audit.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seclient.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seobjtype.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seprivilege.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesd.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesddl.c
    ${REACTOS_SOURCE_DIR}/ntos/se/semgr.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesid.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seqos.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesrm.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesubject.c
    ${REACTOS_SOURCE_DIR}/ntos/se/token.c
    ${REACTOS_SOURCE_DIR}/ntos/se/setokenadj.c
    ${REACTOS_SOURCE_DIR}/ntos/se/tokencls.c
    ${REACTOS_SOURCE_DIR}/ntos/se/tokenlif.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/enlist.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/rm.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/tm.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/tx.c
    ${REACTOS_SOURCE_DIR}/ntos/vf/driver.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/etwlog.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/etwreg.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/guidobj.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/pcw.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/provider.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/smbios.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/wmi.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/wmidrv.c)

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpchelp.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpcinit.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpcport.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpcconn.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpcsend.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpcattr.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpccompl.c
    ${REACTOS_SOURCE_DIR}/ntos/alpc/alpclpc.c)

if(DBG)
    list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/se/sedebug.c)
    if(KDBG)
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd64/kdwatchdog.c)
    endif()
endif()

list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/ex/zw.S)

if(ARCH STREQUAL "i386")
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ex/i386/fastinterlck_asm.S
        ${REACTOS_SOURCE_DIR}/ntos/ex/i386/ioport.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/ctxswitch.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/trap.s
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/usercall_asm.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/zeropage.S
        ${REACTOS_SOURCE_DIR}/ntos/rtl/i386/prefetch.S)
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/i386/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/i386/kdx86.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/abios.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/context.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/exp.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/freeze.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/irqobj.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/ldt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/mtrr.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/patpge.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/traphdlr.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/i386/v86vdm.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/i386/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/i386/psldt.c
        ${REACTOS_SOURCE_DIR}/ntos/vdm/vdmmain.c
        ${REACTOS_SOURCE_DIR}/ntos/vdm/vdmexec.c)
elseif(ARCH STREQUAL "amd64")
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/boot.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/ctxswitch.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/trap.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/usercall_asm.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/zeropage.S)
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/i386/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/kdcontrol.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/amd64/kdx64.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/context.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/except.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/freeze.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/interrupt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/ipi.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/irql.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/krnlinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/spinlock.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/amd64/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/stubs.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/traphandler.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/amd64/xstate.c)
elseif(ARCH STREQUAL "arm")
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ex/arm/ioport.s
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/boot.s
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/ctxswtch.s
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/stubs_asm.s
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/trap.s)
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/arm/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/arm/kdarm.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/exp.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/interrupt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/trapc.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/arm/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/arm/rtlexcpt.c)
elseif(ARCH STREQUAL "arm64")
    include_directories(${REACTOS_SOURCE_DIR}/ntos/arch/arm64/include)
    list(REMOVE_ITEM SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/ipi.c)
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/chkstk.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/earlyvec.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/fpstate_asm.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/trapvec.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/trapret.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/ctxswitch.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/usercall_asm.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/bootstack.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/zeropage.S
        ${REACTOS_SOURCE_DIR}/ntos/rtl/arm64/ehandler_asm.S
        ${REACTOS_SOURCE_DIR}/sdk/lib/crt/string/arm64/strcpy.S
        ${REACTOS_SOURCE_DIR}/sdk/lib/crt/string/arm64/wcscmp.S)
    if(MSVC)
        list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/msvc.S)
    endif()
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/arm64/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/ex/arm64/init.c
        ${REACTOS_SOURCE_DIR}/ntos/ex/arm64/probe.c
        ${REACTOS_SOURCE_DIR}/ntos/ex/arm64/ioport.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/kdcontrol.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/arm64/kdarm64.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/atomics.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/boot.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/context.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/cpuaudit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/early_uart.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/hypervisor.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/exceptinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/exp.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/floatstubs.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/freeze.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/interrupt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/ipi.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/irql.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/rtlshim.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/smpdbg.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/spinlock.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/stubs.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/topology.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/trapc.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/trapdump.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/arm64/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/arm64/ehandler.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/arm64/rtlexcpt.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/arm64/rtlstubs.c)
elseif(ARCH STREQUAL "riscv64")
    list(REMOVE_ITEM SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/ipi.c)
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/ctxswitch.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/entry.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/float.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/stack.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/trap.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/usercall.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/usercopy.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/syscall.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/vector.S
        ${REACTOS_SOURCE_DIR}/ntos/rtl/riscv64/capture.S)
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/riscv64/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/riscv64/kdsup.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/cache.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/console.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/context.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/features.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/freeze.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/interrupt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/ipi.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/irql.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/pcr.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/spinlock.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/stack.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/stubs.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/syscall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/topology.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/tlb.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/trap.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/usercopy.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/vector.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/riscv64/xstate.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/riscv64/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/riscv64/rtlexcpt.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/riscv64/slist.c)
elseif(ARCH STREQUAL "ppc")
    list(REMOVE_ITEM SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/ipi.c)
    list(APPEND ASM_SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/ctxswitch.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/entry.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/stack.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/syscall.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/trap.S
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/usercall.S)
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/config/ppc/cmhardwr.c
        ${REACTOS_SOURCE_DIR}/ntos/ex/ppc/ioaccess.c
        ${REACTOS_SOURCE_DIR}/ntos/kd64/ppc/kdsup.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/atomic64.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/cache.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/console.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/context.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/cpu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/interrupt.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/irql.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/kiinit.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/mmu.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/mp.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/spinlock.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/stack.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/stubs.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/syscall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/thrdini.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/trap.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/usercall.c
        ${REACTOS_SOURCE_DIR}/ntos/ke/ppc/usercopy.c
        ${REACTOS_SOURCE_DIR}/ntos/ps/ppc/psctx.c
        ${REACTOS_SOURCE_DIR}/ntos/rtl/ppc/rtlexcpt.c)
endif()

if(NOT _WINKD_)
if(KDBG)
    add_definitions(-DKDBG)
endif()

    if(ARCH STREQUAL "i386")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/i386/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/i386-dis.c)
        endif()
    elseif(ARCH STREQUAL "amd64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/i386/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/amd64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/i386-dis.c)
        endif()
    elseif(ARCH STREQUAL "arm")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/arm/kdserial.c)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/arm64/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/arm64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/arm64/arm64-dis.c)
        endif()
    elseif(ARCH STREQUAL "ppc")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/ppc/kdserial.c)
    elseif(ARCH STREQUAL "riscv64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/riscv64/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/riscv64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/riscv64/riscv64-dis.c)
        endif()
    endif()

    if(KDBG)
        list(APPEND SOURCE
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbg.c
            ${REACTOS_SOURCE_DIR}/ntos/mm/pool/debug/poolkdbg.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_cli.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_cmdhist.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_expr.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_print.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_symbols.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_symz.c)
    endif()

    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdinfo.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdio.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdmain.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdnetio.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdprompt.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdps2kbd.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdserial.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdterminal.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdusbkbd.c)

else()
    add_definitions(-D_WINKD_)
endif()
