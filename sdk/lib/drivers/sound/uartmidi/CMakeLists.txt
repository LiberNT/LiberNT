
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/sound/soundblaster
    ${REACTOS_SOURCE_DIR}/sdk/include/libernt/libs/sound)

add_library(uartmidi midiuart.c)
add_dependencies(uartmidi bugcodes xdk)
