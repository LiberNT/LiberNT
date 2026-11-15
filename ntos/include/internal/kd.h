
#include <cportlib/cportlib.h>




BOOLEAN
NTAPI
KdPortGetByteEx(
    PCPPORT PortInformation,
    PUCHAR ByteReceived);

NTAPI
    PCPPORT PortInformation,

VOID
NTAPI
KdPortPutBufferEx(
    PCPPORT PortInformation,
    PCCH Buffer,

