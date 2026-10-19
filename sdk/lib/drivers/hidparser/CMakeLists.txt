
add_definitions(-DUNICODE -D_UNICODE)

list(APPEND SOURCE
    api.c
    hidparser.c
    hidp.c
    hidp.h)

add_library(hidparser_um ${SOURCE})
add_importlibs(hidparser_um ntdll)
add_dependencies(hidparser_um bugcodes xdk)

add_library(hidparser_km ${SOURCE} parser.c)
add_importlibs(hidparser_km ntoskrnl)
add_dependencies(hidparser_km bugcodes xdk)
