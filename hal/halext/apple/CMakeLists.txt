
list(APPEND SOURCE
    halextapple.c
    apple_aic.c
    apple_wdt.c
    apple_idle.c)

if(MSVC)
    add_asm_files(halextapple_asm apple_idle.S)
    list(APPEND SOURCE ${halextapple_asm})
else()
    list(APPEND SOURCE apple_idle.S)
endif()

add_hal_extension(halextapple ${SOURCE})
