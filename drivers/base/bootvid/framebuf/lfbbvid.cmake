
set(MODULE_HEADER ${CMAKE_CURRENT_LIST_DIR}/framebuf.h) # For precomp.h
list(APPEND SOURCE
    ${MODULE_HEADER}
    ${CMAKE_CURRENT_LIST_DIR}/bootvid.c
    ${CMAKE_CURRENT_LIST_DIR}/ftglue.c)
if(ARCH STREQUAL "riscv64")
    list(APPEND SOURCE ${CMAKE_CURRENT_LIST_DIR}/riscv64/flush.c)
else()
    list(APPEND SOURCE ${CMAKE_CURRENT_LIST_DIR}/flush.c)
endif()

set(COMPILE_DEFINITIONS _FRAMEBUF_BOOTVID_)
set(BOOTVID_LIBRARIES bootfont libcntpr setjmp ${PSEH_LIB})
set(REACTOS_STR_FILE_DESCRIPTION "Generic Linear FrameBuffer Boot Driver")
