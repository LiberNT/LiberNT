
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
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/crash.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/crash.c APPEND PROPERTY COMPILE_OPTIONS -Xclang -fasync-exceptions)
endif()

if(ARCH STREQUAL "arm64")
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/irql.c ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/spinlock.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/irql.c ${REACTOS_SOURCE_DIR}/ntos/ke/arm64/spinlock.c APPEND PROPERTY COMPILE_DEFINITIONS _NTHALDLL_)
endif()

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/ntos/config/cmboot.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmtrans.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmapphive.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmbackend.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmbigdata.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmcompact.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmcontrolset.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmdelayclose.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmfilter.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmflusher.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmhiveload.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmhivemgr.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmhwtree.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkcbpool.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkeyapi.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkeycache.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkeyobject.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmkeysecurity.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmmerge.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmnamespace.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmregistry.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmsecurecache.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmselfheal.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmstartup.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmsubkey.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmsystemcalls.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmvaluecache.c
    ${REACTOS_SOURCE_DIR}/ntos/config/cmwatch.c
    ${REACTOS_SOURCE_DIR}/ntos/dbgk/dbgobject.c
    ${REACTOS_SOURCE_DIR}/ntos/dbgk/dbgreport.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/license.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/partition.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/reserve.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/spinlock.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/wddm.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/wnf.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exatom.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/excallback.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exdbgctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exevent.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exfastmutex.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exfastresource.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exfirmware.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exhandle.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exharderror.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exheadless.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exinit.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exinterlocked.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exintrinsics.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exlocale.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exlookaside.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exprofile.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/expushlock.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exresource.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exrundown.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsemaphore.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exshutdown.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsyshandle.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsysinfo.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsysperf.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsysproc.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exsysset.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/extime.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/extimer.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exuuid.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exwin32.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exworker.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exxip.c
    ${REACTOS_SOURCE_DIR}/ntos/ex/exzone.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/bytelock.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/cancelwait.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/largemcb.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/ansiname.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/charclass.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/datascan.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/deepstack.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/dirnotify.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/ecp.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/ecpirp.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/exception.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fastpath.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/filesize.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fsfilterctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/fsinit.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/ftvolume.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/lockquery.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/mupreg.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/oplockex.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/oplockpkg.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/pathdots.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/poolsup.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/reparse.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/runmap.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/streamctx.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/tunnelcache.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/unicodename.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/volnotify.c
    ${REACTOS_SOURCE_DIR}/ntos/fsrtl/volstartup.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/bootdisk.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/bustranslate.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/diskio.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/disklayout.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/gpt.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/haldispatch.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/mbr.c
    ${REACTOS_SOURCE_DIR}/ntos/fstub/mbrlegacy.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/inbvgop.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/bootlogo.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/display.c
    ${REACTOS_SOURCE_DIR}/ntos/inbv/serialport.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/bootcb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/waitcomp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iommu.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ksr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/devobj.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/devqueue.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/devtimer.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/drvinit.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/drvobj.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/fileobj.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioadapter.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioarcname.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iobootlog.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iobuild.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocall.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocancel.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocomplete.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocompport.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iocreate.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iodump.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioecp.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioerrlog.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioevents.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iofileinfo.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioinit.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iointr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iomisc.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iopacket.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioparse.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioramdisk.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iorawfs.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iordwr.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iorsrc.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/ioshutdown.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iosymlink.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/iovolume.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/removelock.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/shareacc.c
    ${REACTOS_SOURCE_DIR}/ntos/io/iomgr/workitem.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/rootarb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/dif.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnppar.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpresdb.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/devinterface.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/devprop.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpdetect.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpevent.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpnode.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpnotif.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpquery.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpregutil.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpresource.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnprootbus.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpstart.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpmgr/pnpstate.c
    ${REACTOS_SOURCE_DIR}/ntos/io/pnpdbg.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdbreakpoint.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdcontrolspace.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdenable.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdengine.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdexception.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdfilter.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdglobals.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdmemory.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdoutput.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdport.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdregisters.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdstartup.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kddump.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdfile.c
    ${REACTOS_SOURCE_DIR}/ntos/kd64/kdlog.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/passint.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procgrp.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procinfo.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/smpdbg.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/apcobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/arcconfig.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/balance.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/clockint.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/cpuctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/crash.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/dispatch.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/dpcobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/exceptsvc.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/freezecpu.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/ioqueue.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/kernmain.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/ktimer.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/lockprim.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/procctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/profile.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/schedule.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/syncobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/thrdctrl.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/waitobj.c
    ${REACTOS_SOURCE_DIR}/ntos/ke/workqueue.c
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
    ${REACTOS_SOURCE_DIR}/ntos/po/pep.c
    ${REACTOS_SOURCE_DIR}/ntos/po/pofx.c
    ${REACTOS_SOURCE_DIR}/ntos/po/powerlimit.c
    ${REACTOS_SOURCE_DIR}/ntos/po/hiber.c
    ${REACTOS_SOURCE_DIR}/ntos/po/sleep.c
    ${REACTOS_SOURCE_DIR}/ntos/po/standby.c
    ${REACTOS_SOURCE_DIR}/ntos/po/thermal.c
    ${REACTOS_SOURCE_DIR}/ntos/po/ttm.c
    ${REACTOS_SOURCE_DIR}/ntos/po/podevice.c
    ${REACTOS_SOURCE_DIR}/ntos/po/poshutdown.c
    ${REACTOS_SOURCE_DIR}/ntos/po/posystem.c
    ${REACTOS_SOURCE_DIR}/ntos/po/povolumes.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/apiset.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/energy.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pico.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/property.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/tls.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psaccessors.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pscallback.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pscid.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pscpuset.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psexit.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pshandletrace.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psinfoprocess.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psinfoprocessset.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psinfothread.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psinfothreadset.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psjob.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pslivedump.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psprocess.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psquota.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/pssecurity.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psshimcache.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psstartup.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psstate.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psthread.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psthreadctx.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psuserprocess.c
    ${REACTOS_SOURCE_DIR}/ntos/ps/psw32callout.c
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
    ${REACTOS_SOURCE_DIR}/ntos/se/seclient.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seobjtype.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seprivilege.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesd.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesddl.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seaudit.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seinit.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seobject.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesid.c
    ${REACTOS_SOURCE_DIR}/ntos/se/seqos.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesrm.c
    ${REACTOS_SOURCE_DIR}/ntos/se/sesubject.c
    ${REACTOS_SOURCE_DIR}/ntos/se/setoken.c
    ${REACTOS_SOURCE_DIR}/ntos/se/setokenadj.c
    ${REACTOS_SOURCE_DIR}/ntos/se/setokenquery.c
    ${REACTOS_SOURCE_DIR}/ntos/se/setokenbind.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/enlist.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/rm.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/tm.c
    ${REACTOS_SOURCE_DIR}/ntos/tm/tx.c
    ${REACTOS_SOURCE_DIR}/ntos/vf/vfddi.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/etwlog.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/etwreg.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/pcw.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/provider.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/etwprovider.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/guidobject.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/smbiostable.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/tracecontrol.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/wmiblock.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/wmidevice.c
    ${REACTOS_SOURCE_DIR}/ntos/wmi/wmiguids.c)

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
    # TODO: i386 has no interprocessor request file; the other targets use ke/<arch>/ipi.c
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
        ${REACTOS_SOURCE_DIR}/ntos/ex/i386/exinit.c
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
        ${REACTOS_SOURCE_DIR}/ntos/ex/amd64/exinit.c
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
    ${REACTOS_SOURCE_DIR}/ntos/ps/amd64/psarchctx.c
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
    ${REACTOS_SOURCE_DIR}/ntos/ex/arm64/exioport.c
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
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/i386-dis.c ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/kdbregs.c)
        endif()
    elseif(ARCH STREQUAL "amd64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/i386/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/amd64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/i386/i386-dis.c ${REACTOS_SOURCE_DIR}/ntos/kdbg/amd64/kdbregs.c)
        endif()
    elseif(ARCH STREQUAL "arm")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/arm/kdserial.c)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/arm64/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/arm64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/arm64/arm64-dis.c ${REACTOS_SOURCE_DIR}/ntos/kdbg/arm64/kdbregs.c)
        endif()
    elseif(ARCH STREQUAL "ppc")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/ppc/kdserial.c)
    elseif(ARCH STREQUAL "riscv64")
        list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kd/riscv64/kdserial.c)
        if(KDBG)
            list(APPEND ASM_SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/riscv64/kdb_help.S)
            list(APPEND SOURCE ${REACTOS_SOURCE_DIR}/ntos/kdbg/riscv64/riscv64-dis.c ${REACTOS_SOURCE_DIR}/ntos/kdbg/riscv64/kdbregs.c)
        endif()
    endif()

    if(KDBG)
        list(APPEND SOURCE
            ${REACTOS_SOURCE_DIR}/ntos/mm/pool/debug/poolkdbg.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbcli.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbcore.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbexpr.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbhistory.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbio.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbshim.c
    ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdbsymbols.c
            ${REACTOS_SOURCE_DIR}/ntos/kdbg/kdb_symz.c)
    endif()

    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdinfo.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdnetio.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdcomport.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdcomterm.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdconsole.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdkeyboard.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdlifecycle.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdlineedit.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdproviders.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdscreen.c
    ${REACTOS_SOURCE_DIR}/ntos/kd/kdtransport.c
        ${REACTOS_SOURCE_DIR}/ntos/kd/kdusbkbd.c)

else()
    add_definitions(-D_WINKD_)
endif()
