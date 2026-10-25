# Both compatibility runtimes share the portable DLL set. Only their
# architecture-specific loader and guest executables remain separate.
include("${CMAKE_CURRENT_LIST_DIR}/compat_runtime_targets.cmake")

set(WOW64_I386_MODULES ${COMPAT_RUNTIME_MODULES}
    activeds.tlb
    atl100
    d3d8thk
    dmusic
    dplayx
    dpwsockx
    iccvid
    mshtml.tlb
    msvcrt20
    msvcrt40
    ntdll
    olecli32
    olepro32
    olesvr32
    olethk32
    stdole2.tlb
    stdole32.tlb
    winegstreamer
)
set(WOW64_I386_AUXILIARY_MODULES ${COMPAT_RUNTIME_AUXILIARY_MODULES} fusion fusion_1_1 fusion_2_0 msvcm80 msvcm90 msvcp80 msvcp90 msvcr80 msvcr90)
set(WOW64_I386_ALIASES ${COMPAT_RUNTIME_ALIASES})

set(WOW64_I386_EXECUTABLES
    arp
    at
    attrib
    autochk
    cacls
    calc
    certutil
    charmap
    chcp
    chkdsk
    cleanmgr
    clip
    cmd
    cmd_help
    comp
    control
    cscript
    ctfmon
    diskpart
    dism
    dllhost
    doskey
    dxdiag
    eventcreate
    eventvwr
    expand
    extrac32
    fc
    find
    findstr
    finger
    fltmc
    fontview
    format
    fsutil
    ftp
    glgears
    glmark2
    glmark2_runner
    hostname
    infdefaultinstall
    ipconfig
    label
    lodctr
    magnify
    mmc
    mode
    more
    mshta
    msiexec
    msinfo32
    mspaint
    mstsc
    net
    netsh
    netstat
    notepad
    nslookup
    ping
    reg
    regedit
    regedt32
    regsvr32
    replace
    route
    runas
    rundll32
    runonce
    sc
    schtasks
    sdbinst
    shutdown
    sort
    subst
    svchost
    taskkill
    tasklist
    taskmgr
    tcpsvcs
    timeout
    tracert
    tree
    userinit
    utilman
    wglgears_runner
    where
    whoami
    winver
    wscript
    wusa
    xcopy)

if(ENABLE_ROSTESTS)
    list(APPEND WOW64_I386_EXECUTABLES
        user32_winetest
        win32u_apitest
        win32u_winetest)
endif()

if(ENABLE_ROSTESTS OR ENABLE_APPSMOKE_TESTS)
    list(APPEND WOW64_I386_EXECUTABLES appsmoke_apitest)
endif()

# Native ARM programs keep their canonical, unprefixed command names.  Give
# guest graphics programs architecture-qualified aliases so i386 (and AMD64
# payloads using the same convention) are unambiguous when launched manually.
set(WOW64_I386_ARCH_COMMAND_TARGETS)
foreach(_target IN ITEMS
    glgears
    glmark2
    glmark2_runner
    wglgears_runner)
    if(_target IN_LIST WOW64_I386_EXECUTABLES)
        list(APPEND WOW64_I386_ARCH_COMMAND_TARGETS "${_target}")
        list(APPEND WOW64_I386_ALIASES
            "${_target}=i386_${_target}${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
endforeach()
