/*
 * PROJECT:     LiberNT Intel GPIO Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for the GPIO communities of Intel platform controller hubs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define NDEBUG
#include <debug.h>

#define INTELGPIO_TAG 'oGpI'
#define INTELGPIO_MAX_COMMUNITIES 5
#define INTELGPIO_MAX_GROUPS 20
#define INTELGPIO_PINS_PER_BANK 32

#define INTELGPIO_REVID 0x000
#define INTELGPIO_PADBAR 0x00c
#define INTELGPIO_REVISION_DEBOUNCE 0x92

#define INTELGPIO_PADCFG0 0x0
#define INTELGPIO_PADCFG1 0x4
#define INTELGPIO_PADCFG2 0x8

#define INTELGPIO_PADCFG0_RXEVCFG_MASK 0x06000000
#define INTELGPIO_PADCFG0_RXEVCFG_LEVEL 0x00000000
#define INTELGPIO_PADCFG0_RXEVCFG_EDGE 0x02000000
#define INTELGPIO_PADCFG0_RXEVCFG_EDGE_BOTH 0x06000000
#define INTELGPIO_PADCFG0_PREGFRXSEL 0x01000000
#define INTELGPIO_PADCFG0_RXINV 0x00800000
#define INTELGPIO_PADCFG0_ROUTE_MASK 0x001e0000
#define INTELGPIO_PADCFG0_PMODE_MASK 0x00003c00
#define INTELGPIO_PADCFG0_RXDIS 0x00000200
#define INTELGPIO_PADCFG0_TXDIS 0x00000100
#define INTELGPIO_PADCFG0_RXSTATE 0x00000002
#define INTELGPIO_PADCFG0_TXSTATE 0x00000001

#define INTELGPIO_PADCFG1_TERM_UP 0x00002000
#define INTELGPIO_PADCFG1_TERM_MASK 0x00001c00
#define INTELGPIO_PADCFG1_TERM_20K 0x00001000

#define INTELGPIO_PADCFG2_DEBOUNCE_MASK 0x0000001e
#define INTELGPIO_PADCFG2_DEBOUNCE_ENABLE 0x00000001
#define INTELGPIO_DEBOUNCE_PERIOD_NS 31250
#define INTELGPIO_DEBOUNCE_MINIMUM 3
#define INTELGPIO_DEBOUNCE_MAXIMUM 15

#define INTELGPIO_LOCK_CONFIGURATION 0x1
#define INTELGPIO_LOCK_TRANSMIT 0x2

typedef struct _INTELGPIO_GROUP
{
    UCHAR Community;
    UCHAR RegisterNumber;
    UCHAR PadOwnNumber;
    UCHAR PinCount;
    USHORT FirstPin;
    SHORT GpioBase;
} INTELGPIO_GROUP;

typedef struct _INTELGPIO_SOC
{
    USHORT PadOwnOffset;
    USHORT PadConfigLockOffset;
    USHORT HostSoftwareOwnOffset;
    USHORT InterruptStatusOffset;
    USHORT InterruptEnableOffset;
    UCHAR CommunityCount;
    UCHAR GroupCount;
    USHORT CommunityFirstPin[INTELGPIO_MAX_COMMUNITIES];
    USHORT CommunityPinCount[INTELGPIO_MAX_COMMUNITIES];
    const INTELGPIO_GROUP *Groups;
} INTELGPIO_SOC;

typedef struct _INTELGPIO_ACPI_ID
{
    PCWSTR HardwareId;
    const INTELGPIO_SOC *Soc;
} INTELGPIO_ACPI_ID;

typedef struct _INTELGPIO_COMMUNITY
{
    PUCHAR RegisterBase;
    ULONG RegisterLength;
    ULONG PadOffset;
    ULONG PadStride;
} INTELGPIO_COMMUNITY, *PINTELGPIO_COMMUNITY;

typedef struct _INTELGPIO_CONTEXT
{
    const INTELGPIO_SOC *Soc;
    INTELGPIO_COMMUNITY Communities[INTELGPIO_MAX_COMMUNITIES];
    ULONG EnabledInterrupts[INTELGPIO_MAX_GROUPS];
    USHORT TotalPins;
} INTELGPIO_CONTEXT, *PINTELGPIO_CONTEXT;

static const INTELGPIO_GROUP IntelGpioSptLpGroups[] =
{
    {0, 0, 0, 24, 0, 0},
    {0, 1, 4, 24, 24, 24},
    {1, 0, 0, 24, 48, 48},
    {1, 1, 4, 24, 72, 72},
    {1, 2, 8, 24, 96, 96},
    {2, 0, 0, 24, 120, 120},
    {2, 1, 4, 8, 144, 144},
};

static const INTELGPIO_SOC IntelGpioSptLpSoc =
{
    0x020, 0x0a0, 0x0d0, 0x100, 0x120,
    3, RTL_NUMBER_OF(IntelGpioSptLpGroups),
    {0, 48, 120},
    {48, 72, 32},
    IntelGpioSptLpGroups
};

static const INTELGPIO_GROUP IntelGpioSptHGroups[] =
{
    {0, 0, 0, 24, 0, 0},
    {0, 1, 3, 24, 24, 24},
    {1, 0, 0, 24, 48, 48},
    {1, 1, 3, 24, 72, 72},
    {1, 2, 6, 13, 96, 96},
    {1, 3, 8, 24, 109, 120},
    {1, 4, 11, 24, 133, 144},
    {1, 5, 14, 24, 157, 168},
    {2, 0, 0, 11, 181, 192},
};

static const INTELGPIO_SOC IntelGpioSptHSoc =
{
    0x020, 0x090, 0x0d0, 0x100, 0x120,
    3, RTL_NUMBER_OF(IntelGpioSptHGroups),
    {0, 48, 181},
    {48, 133, 11},
    IntelGpioSptHGroups
};

static const INTELGPIO_GROUP IntelGpioCnlHGroups[] =
{
    {0, 0, 0, 25, 0, 0},
    {0, 1, 4, 26, 25, 32},
    {1, 0, 0, 24, 51, 64},
    {1, 1, 3, 24, 75, 96},
    {1, 2, 6, 8, 99, 128},
    {1, 3, 7, 8, 107, -1},
    {1, 4, 8, 32, 115, 160},
    {1, 5, 12, 8, 147, -1},
    {2, 0, 0, 24, 155, 192},
    {2, 1, 3, 24, 179, 224},
    {2, 2, 6, 13, 203, 256},
    {2, 3, 8, 24, 216, 288},
    {2, 4, 11, 9, 240, -1},
    {3, 0, 0, 11, 249, -1},
    {3, 1, 2, 9, 260, -1},
    {3, 2, 4, 18, 269, 320},
    {3, 3, 7, 12, 287, 352},
};

static const INTELGPIO_SOC IntelGpioCnlHSoc =
{
    0x020, 0x080, 0x0c0, 0x100, 0x120,
    4, RTL_NUMBER_OF(IntelGpioCnlHGroups),
    {0, 51, 155, 249},
    {51, 104, 94, 50},
    IntelGpioCnlHGroups
};

static const INTELGPIO_GROUP IntelGpioCnlLpGroups[] =
{
    {0, 0, 0, 25, 0, 0},
    {0, 1, 4, 26, 25, 32},
    {0, 2, 8, 8, 51, 64},
    {0, 3, 9, 9, 59, -1},
    {1, 0, 0, 25, 68, 96},
    {1, 1, 4, 24, 93, 128},
    {1, 2, 7, 24, 117, 160},
    {1, 3, 10, 32, 141, 192},
    {1, 4, 14, 8, 173, 224},
    {2, 0, 0, 24, 181, 256},
    {2, 1, 3, 24, 205, 288},
    {2, 2, 6, 9, 229, -1},
    {2, 3, 8, 6, 238, -1},
};

static const INTELGPIO_SOC IntelGpioCnlLpSoc =
{
    0x020, 0x080, 0x0b0, 0x100, 0x120,
    3, RTL_NUMBER_OF(IntelGpioCnlLpGroups),
    {0, 68, 181},
    {68, 113, 63},
    IntelGpioCnlLpGroups
};

static const INTELGPIO_GROUP IntelGpioIclLpGroups[] =
{
    {0, 0, 0, 8, 0, 0},
    {0, 1, 1, 26, 8, 32},
    {0, 2, 5, 25, 34, 64},
    {1, 0, 0, 24, 59, 96},
    {1, 1, 3, 21, 83, 128},
    {1, 2, 6, 20, 104, 160},
    {1, 3, 9, 29, 124, 192},
    {2, 0, 0, 24, 153, 224},
    {2, 1, 3, 6, 177, -1},
    {2, 2, 4, 24, 183, 256},
    {2, 3, 7, 9, 207, -1},
    {3, 0, 0, 8, 216, 288},
    {3, 1, 1, 8, 224, 320},
    {3, 2, 2, 9, 232, -1},
};

static const INTELGPIO_SOC IntelGpioIclLpSoc =
{
    0x020, 0x080, 0x0b0, 0x100, 0x110,
    4, RTL_NUMBER_OF(IntelGpioIclLpGroups),
    {0, 59, 153, 216},
    {59, 94, 63, 25},
    IntelGpioIclLpGroups
};

static const INTELGPIO_GROUP IntelGpioIclNGroups[] =
{
    {0, 0, 0, 9, 0, -1},
    {0, 1, 2, 26, 9, 32},
    {0, 2, 6, 21, 35, 64},
    {0, 3, 9, 8, 56, 96},
    {0, 4, 10, 8, 64, 128},
    {1, 0, 0, 24, 72, 160},
    {1, 1, 3, 26, 96, 192},
    {1, 2, 7, 29, 122, 224},
    {1, 3, 11, 24, 151, 256},
    {2, 0, 0, 6, 175, -1},
    {2, 1, 1, 24, 181, 288},
    {3, 0, 0, 8, 205, 0},
};

static const INTELGPIO_SOC IntelGpioIclNSoc =
{
    0x020, 0x080, 0x0b0, 0x100, 0x120,
    4, RTL_NUMBER_OF(IntelGpioIclNGroups),
    {0, 72, 175, 205},
    {72, 103, 30, 8},
    IntelGpioIclNGroups
};

static const INTELGPIO_GROUP IntelGpioTglLpGroups[] =
{
    {0, 0, 0, 26, 0, 0},
    {0, 1, 4, 16, 26, 32},
    {0, 2, 6, 25, 42, 64},
    {1, 0, 0, 8, 67, 96},
    {1, 1, 1, 24, 75, 128},
    {1, 2, 4, 21, 99, 160},
    {1, 3, 7, 24, 120, 192},
    {1, 4, 10, 27, 144, 224},
    {2, 0, 0, 24, 171, 256},
    {2, 1, 3, 25, 195, 288},
    {2, 2, 7, 6, 220, -1},
    {2, 3, 8, 25, 226, 320},
    {2, 4, 12, 9, 251, -1},
    {3, 0, 0, 8, 260, 352},
    {3, 1, 1, 9, 268, -1},
};

static const INTELGPIO_SOC IntelGpioTglLpSoc =
{
    0x020, 0x080, 0x0b0, 0x100, 0x120,
    4, RTL_NUMBER_OF(IntelGpioTglLpGroups),
    {0, 67, 171, 260},
    {67, 104, 89, 17},
    IntelGpioTglLpGroups
};

static const INTELGPIO_GROUP IntelGpioTglHGroups[] =
{
    {0, 0, 0, 25, 0, 0},
    {0, 1, 4, 20, 25, 32},
    {0, 2, 7, 26, 45, 64},
    {0, 3, 11, 8, 71, 96},
    {1, 0, 0, 26, 79, 128},
    {1, 1, 4, 24, 105, 160},
    {1, 2, 7, 8, 129, 192},
    {1, 3, 8, 17, 137, 224},
    {1, 4, 11, 27, 154, 256},
    {2, 0, 0, 13, 181, 288},
    {2, 1, 2, 24, 194, 320},
    {3, 0, 0, 24, 218, 352},
    {3, 1, 3, 10, 242, 384},
    {3, 2, 5, 15, 252, 416},
    {4, 0, 0, 15, 267, 448},
    {4, 1, 2, 9, 282, -1},
};

static const INTELGPIO_SOC IntelGpioTglHSoc =
{
    0x020, 0x090, 0x0c0, 0x100, 0x120,
    5, RTL_NUMBER_OF(IntelGpioTglHGroups),
    {0, 79, 181, 218, 267},
    {79, 102, 37, 49, 24},
    IntelGpioTglHGroups
};

static const INTELGPIO_GROUP IntelGpioJslGroups[] =
{
    {0, 0, 0, 20, 0, 320},
    {0, 1, 3, 9, 20, -1},
    {0, 2, 5, 26, 29, 32},
    {0, 3, 9, 21, 55, 64},
    {0, 4, 12, 8, 76, 96},
    {0, 5, 13, 8, 84, 128},
    {1, 0, 0, 24, 92, 160},
    {1, 1, 3, 26, 116, 192},
    {1, 2, 7, 29, 142, 224},
    {1, 3, 11, 24, 171, 256},
    {2, 0, 0, 6, 195, -1},
    {2, 1, 1, 24, 201, 288},
    {3, 0, 0, 8, 225, 0},
};

static const INTELGPIO_SOC IntelGpioJslSoc =
{
    0x020, 0x080, 0x0c0, 0x100, 0x120,
    4, RTL_NUMBER_OF(IntelGpioJslGroups),
    {0, 92, 195, 225},
    {92, 103, 30, 8},
    IntelGpioJslGroups
};

static const INTELGPIO_GROUP IntelGpioAdlNGroups[] =
{
    {0, 0, 0, 26, 0, 0},
    {0, 1, 4, 16, 26, 32},
    {0, 2, 6, 25, 42, 64},
    {1, 0, 0, 8, 67, 96},
    {1, 1, 1, 20, 75, 128},
    {1, 2, 4, 24, 95, 160},
    {1, 3, 7, 21, 119, 192},
    {1, 4, 10, 29, 140, 224},
    {2, 0, 0, 24, 169, 256},
    {2, 1, 3, 25, 193, 288},
    {2, 2, 7, 6, 218, -1},
    {2, 3, 8, 25, 224, 320},
    {3, 0, 0, 8, 249, 352},
};

static const INTELGPIO_SOC IntelGpioAdlNSoc =
{
    0x020, 0x080, 0x0b0, 0x100, 0x120,
    4, RTL_NUMBER_OF(IntelGpioAdlNGroups),
    {0, 67, 169, 249},
    {67, 102, 80, 8},
    IntelGpioAdlNGroups
};

static const INTELGPIO_GROUP IntelGpioAdlSGroups[] =
{
    {0, 0, 0, 25, 0, 0},
    {0, 1, 4, 23, 25, 32},
    {0, 2, 7, 12, 48, 64},
    {0, 3, 9, 27, 60, 96},
    {0, 4, 13, 8, 87, 128},
    {1, 0, 0, 24, 95, 160},
    {1, 1, 3, 8, 119, 192},
    {1, 2, 4, 24, 127, 224},
    {2, 0, 0, 9, 151, -1},
    {2, 1, 2, 16, 160, 256},
    {2, 2, 4, 24, 176, 288},
    {3, 0, 0, 8, 200, 320},
    {3, 1, 1, 23, 208, 352},
    {3, 2, 4, 15, 231, 384},
    {3, 3, 6, 24, 246, 416},
    {4, 0, 0, 25, 270, 448},
    {4, 1, 4, 9, 295, -1},
};

static const INTELGPIO_SOC IntelGpioAdlSSoc =
{
    0x0a0, 0x110, 0x150, 0x200, 0x220,
    5, RTL_NUMBER_OF(IntelGpioAdlSGroups),
    {0, 95, 151, 200, 270},
    {95, 56, 49, 70, 34},
    IntelGpioAdlSGroups
};

static const INTELGPIO_GROUP IntelGpioMtlPGroups[] =
{
    {0, 0, 0, 5, 0, 0},
    {0, 1, 1, 24, 5, 32},
    {0, 2, 4, 24, 29, 64},
    {1, 0, 0, 25, 53, 96},
    {1, 1, 4, 25, 78, 128},
    {2, 0, 0, 26, 103, 160},
    {2, 1, 4, 26, 129, 192},
    {2, 2, 8, 15, 155, 224},
    {2, 3, 10, 14, 170, 256},
    {3, 0, 0, 8, 184, 288},
    {3, 1, 1, 12, 192, 320},
    {4, 0, 0, 25, 204, 352},
    {4, 1, 4, 25, 229, 384},
    {4, 2, 8, 32, 254, 416},
    {4, 3, 12, 3, 286, 448},
};

static const INTELGPIO_SOC IntelGpioMtlPSoc =
{
    0x0b0, 0x110, 0x140, 0x200, 0x210,
    5, RTL_NUMBER_OF(IntelGpioMtlPGroups),
    {0, 53, 103, 184, 204},
    {53, 50, 81, 20, 85},
    IntelGpioMtlPGroups
};

static const INTELGPIO_GROUP IntelGpioMtlSGroups[] =
{
    {0, 0, 0, 28, 0, 0},
    {0, 1, 4, 19, 28, 32},
    {0, 2, 7, 27, 47, 64},
    {1, 0, 0, 20, 74, 96},
    {1, 1, 3, 2, 94, 128},
    {1, 2, 4, 24, 96, 160},
    {2, 0, 0, 16, 120, 192},
    {2, 1, 2, 12, 136, 224},
};

static const INTELGPIO_SOC IntelGpioMtlSSoc =
{
    0x0b0, 0x0f0, 0x110, 0x200, 0x210,
    3, RTL_NUMBER_OF(IntelGpioMtlSGroups),
    {0, 74, 120},
    {74, 46, 28},
    IntelGpioMtlSGroups
};

static const INTELGPIO_GROUP IntelGpioMtpSGroups[] =
{
    {0, 0, 0, 25, 0, 0},
    {0, 1, 4, 14, 25, 32},
    {0, 2, 6, 18, 39, 64},
    {0, 3, 9, 31, 57, 96},
    {1, 0, 0, 15, 88, 128},
    {1, 1, 2, 12, 103, 160},
    {1, 2, 4, 22, 115, 192},
    {2, 0, 0, 9, 137, 224},
    {2, 1, 2, 24, 146, 256},
    {2, 2, 5, 20, 170, 288},
    {2, 3, 8, 4, 190, 320},
    {2, 4, 9, 8, 194, 352},
    {2, 5, 10, 31, 202, 384},
    {3, 0, 0, 8, 233, 416},
    {3, 1, 1, 23, 241, 448},
    {3, 2, 4, 14, 264, 480},
    {3, 3, 6, 24, 278, 512},
    {4, 0, 0, 21, 302, 544},
    {4, 1, 3, 16, 323, 576},
};

static const INTELGPIO_SOC IntelGpioMtpSSoc =
{
    0x0b0, 0x110, 0x150, 0x200, 0x220,
    5, RTL_NUMBER_OF(IntelGpioMtpSGroups),
    {0, 88, 137, 233, 302},
    {88, 49, 96, 69, 37},
    IntelGpioMtpSGroups
};

static const INTELGPIO_GROUP IntelGpioLkfGroups[] =
{
    {0, 0, 0, 32, 0, 0},
    {0, 1, 4, 28, 32, 32},
    {1, 0, 0, 32, 60, 64},
    {1, 1, 4, 32, 92, 96},
    {1, 2, 8, 25, 124, 128},
    {2, 0, 0, 32, 149, 160},
    {2, 1, 4, 32, 181, 192},
    {2, 2, 8, 25, 213, 224},
    {3, 0, 0, 29, 238, 256},
};

static const INTELGPIO_SOC IntelGpioLkfSoc =
{
    0x020, 0x070, 0x090, 0x100, 0x110,
    4, RTL_NUMBER_OF(IntelGpioLkfGroups),
    {0, 60, 149, 238},
    {60, 89, 89, 29},
    IntelGpioLkfGroups
};

static const INTELGPIO_ACPI_ID IntelGpioAcpiIds[] =
{
    {L"INT344B", &IntelGpioSptLpSoc},
    {L"INT3451", &IntelGpioSptHSoc},
    {L"INT345D", &IntelGpioSptHSoc},
    {L"INT3450", &IntelGpioCnlHSoc},
    {L"INT34BB", &IntelGpioCnlLpSoc},
    {L"INT3455", &IntelGpioIclLpSoc},
    {L"INT34C3", &IntelGpioIclNSoc},
    {L"INT34C5", &IntelGpioTglLpSoc},
    {L"INT34C6", &IntelGpioTglHSoc},
    {L"INTC1055", &IntelGpioTglLpSoc},
    {L"INT34C8", &IntelGpioJslSoc},
    {L"INTC1056", &IntelGpioAdlSSoc},
    {L"INTC1057", &IntelGpioAdlNSoc},
    {L"INTC1085", &IntelGpioAdlSSoc},
    {L"INTC105E", &IntelGpioMtlPSoc},
    {L"INTC1083", &IntelGpioMtlPSoc},
    {L"INTC1082", &IntelGpioMtlSSoc},
    {L"INTC1084", &IntelGpioMtpSSoc},
    {L"INT34C4", &IntelGpioLkfSoc},
};

C_ASSERT(RTL_NUMBER_OF(IntelGpioMtpSGroups) <= INTELGPIO_MAX_GROUPS);

DRIVER_INITIALIZE DriverEntry;

static
ULONG
IntelGpioRead(
    _In_ PINTELGPIO_COMMUNITY Community,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Community->RegisterBase + Offset));
}

static
VOID
IntelGpioWrite(
    _In_ PINTELGPIO_COMMUNITY Community,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Community->RegisterBase + Offset), Value);
}

static
const INTELGPIO_GROUP *
IntelGpioResolvePin(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ ULONG GpioNumber,
    _Out_ PULONG GroupOffset)
{
    const INTELGPIO_SOC *Soc = Context->Soc;
    ULONG Index;

    for (Index = 0; Index < Soc->GroupCount; Index++)
    {
        const INTELGPIO_GROUP *Group = &Soc->Groups[Index];

        if (Group->GpioBase >= 0 && GpioNumber >= (ULONG)Group->GpioBase && GpioNumber < (ULONG)Group->GpioBase + Group->PinCount)
        {
            *GroupOffset = GpioNumber - Group->GpioBase;
            return Context->Communities[Group->Community].RegisterBase ? Group : NULL;
        }
    }
    return NULL;
}

static
const INTELGPIO_GROUP *
IntelGpioResolveBankPin(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ BANK_ID BankId,
    _In_ ULONG PinNumber,
    _Out_ PULONG GroupOffset)
{
    return IntelGpioResolvePin(Context, (ULONG)BankId * INTELGPIO_PINS_PER_BANK + PinNumber, GroupOffset);
}

static
ULONG
IntelGpioPadOffset(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset,
    _In_ ULONG Register)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG PadNumber = Group->FirstPin + GroupOffset - Context->Soc->CommunityFirstPin[Group->Community];

    return Community->PadOffset + PadNumber * Community->PadStride + Register;
}

static
BOOLEAN
IntelGpioIsHostOwned(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Value;

    Value = IntelGpioRead(Community, Context->Soc->PadOwnOffset + (Group->PadOwnNumber + GroupOffset / 8) * sizeof(ULONG));
    return (Value & (0xfUL << ((GroupOffset % 8) * 4))) == 0;
}

static
BOOLEAN
IntelGpioIsAcpiMode(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Value;

    Value = IntelGpioRead(Community, Context->Soc->HostSoftwareOwnOffset + Group->RegisterNumber * sizeof(ULONG));
    return (Value & (1UL << GroupOffset)) == 0;
}

static
ULONG
IntelGpioGetLockState(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Offset = Context->Soc->PadConfigLockOffset + Group->RegisterNumber * 2 * sizeof(ULONG);
    ULONG State = 0;

    if (IntelGpioRead(Community, Offset) & (1UL << GroupOffset))
        State |= INTELGPIO_LOCK_CONFIGURATION;
    if (IntelGpioRead(Community, Offset + sizeof(ULONG)) & (1UL << GroupOffset))
        State |= INTELGPIO_LOCK_TRANSMIT;
    return State;
}

static
ULONG
IntelGpioGroupIndex(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group)
{
    return (ULONG)(Group - Context->Soc->Groups);
}

static
VOID
IntelGpioSetInterruptEnable(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset,
    _In_ BOOLEAN Enable)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Offset = Context->Soc->InterruptEnableOffset + Group->RegisterNumber * sizeof(ULONG);
    ULONG Value = IntelGpioRead(Community, Offset);

    if (Enable)
        Value |= 1UL << GroupOffset;
    else
        Value &= ~(1UL << GroupOffset);
    IntelGpioWrite(Community, Offset, Value);
}

static
VOID
IntelGpioClearInterruptStatus(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];

    IntelGpioWrite(Community, Context->Soc->InterruptStatusOffset + Group->RegisterNumber * sizeof(ULONG), 1UL << GroupOffset);
}

static
VOID
IntelGpioApplyPull(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset,
    _In_ UCHAR PullConfiguration)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Offset = IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG1);
    ULONG Value;
    ULONG NewValue;

    if (PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_DEFAULT)
        return;
    Value = IntelGpioRead(Community, Offset);
    NewValue = Value & ~(INTELGPIO_PADCFG1_TERM_UP | INTELGPIO_PADCFG1_TERM_MASK);
    if (PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLUP)
        NewValue |= INTELGPIO_PADCFG1_TERM_UP | INTELGPIO_PADCFG1_TERM_20K;
    else if (PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLDOWN)
        NewValue |= INTELGPIO_PADCFG1_TERM_20K;
    if (NewValue != Value)
        IntelGpioWrite(Community, Offset, NewValue);
}

static
ULONG
IntelGpioApplyDebounce(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ const INTELGPIO_GROUP *Group,
    _In_ ULONG GroupOffset,
    _In_ USHORT DebounceTimeout,
    _In_ ULONG PadConfiguration0)
{
    PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
    ULONG Offset = IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG2);
    ULONGLONG Periods;
    ULONG Exponent = 0;
    ULONG Value;
    ULONG NewValue;

    if (!DebounceTimeout || Community->PadStride <= INTELGPIO_PADCFG2)
        return PadConfiguration0;
    Periods = ((ULONGLONG)DebounceTimeout * 10000 + INTELGPIO_DEBOUNCE_PERIOD_NS - 1) / INTELGPIO_DEBOUNCE_PERIOD_NS;
    while ((1ULL << Exponent) < Periods)
        Exponent++;
    if (Exponent < INTELGPIO_DEBOUNCE_MINIMUM || Exponent > INTELGPIO_DEBOUNCE_MAXIMUM)
        return PadConfiguration0;
    Value = IntelGpioRead(Community, Offset);
    NewValue = (Value & ~(INTELGPIO_PADCFG2_DEBOUNCE_MASK | INTELGPIO_PADCFG2_DEBOUNCE_ENABLE)) | (Exponent << 1) | INTELGPIO_PADCFG2_DEBOUNCE_ENABLE;
    if (NewValue != Value)
        IntelGpioWrite(Community, Offset, NewValue);
    return PadConfiguration0 | INTELGPIO_PADCFG0_PREGFRXSEL;
}

static
VOID
IntelGpioQuiesceInterrupts(
    _In_ PINTELGPIO_CONTEXT Context,
    _In_ BOOLEAN Restore)
{
    const INTELGPIO_SOC *Soc = Context->Soc;
    ULONG Index;

    for (Index = 0; Index < Soc->GroupCount; Index++)
    {
        const INTELGPIO_GROUP *Group = &Soc->Groups[Index];
        PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];

        if (!Community->RegisterBase)
            continue;
        IntelGpioWrite(Community, Soc->InterruptEnableOffset + Group->RegisterNumber * sizeof(ULONG), 0);
        IntelGpioWrite(Community, Soc->InterruptStatusOffset + Group->RegisterNumber * sizeof(ULONG), MAXULONG);
        if (Restore)
            IntelGpioWrite(Community, Soc->InterruptEnableOffset + Group->RegisterNumber * sizeof(ULONG), Context->EnabledInterrupts[Index]);
    }
}

static
const INTELGPIO_SOC *
IntelGpioIdentify(
    _In_ WDFDEVICE Device)
{
    WCHAR HardwareIds[256];
    ULONG Length = 0;
    ULONG Index;
    PCWSTR Current;

    if (!NT_SUCCESS(IoGetDeviceProperty(WdfDeviceWdmGetPhysicalDevice(Device), DevicePropertyHardwareID, sizeof(HardwareIds) - 2 * sizeof(WCHAR), HardwareIds, &Length)))
        return NULL;
    Length = min(Length, (ULONG)(sizeof(HardwareIds) - 2 * sizeof(WCHAR)));
    HardwareIds[Length / sizeof(WCHAR)] = UNICODE_NULL;
    HardwareIds[Length / sizeof(WCHAR) + 1] = UNICODE_NULL;
    for (Current = HardwareIds; *Current; Current += wcslen(Current) + 1)
    {
        PCWSTR Name = wcsrchr(Current, L'\\');

        Name = Name ? Name + 1 : Current;
        if (*Name == L'*')
            Name++;
        for (Index = 0; Index < RTL_NUMBER_OF(IntelGpioAcpiIds); Index++)
        {
            if (!_wcsicmp(Name, IntelGpioAcpiIds[Index].HardwareId))
                return IntelGpioAcpiIds[Index].Soc;
        }
    }
    return NULL;
}

static
VOID
IntelGpioUnmap(
    _Inout_ PINTELGPIO_CONTEXT Context)
{
    ULONG Index;

    for (Index = 0; Index < INTELGPIO_MAX_COMMUNITIES; Index++)
    {
        if (Context->Communities[Index].RegisterBase)
            MmUnmapIoSpace(Context->Communities[Index].RegisterBase, Context->Communities[Index].RegisterLength);
        RtlZeroMemory(&Context->Communities[Index], sizeof(Context->Communities[Index]));
    }
}

static
NTSTATUS
NTAPI
IntelGpioPrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    const INTELGPIO_SOC *Soc;
    ULONG CommunityIndex = 0;
    ULONG Index;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    RtlZeroMemory(Context, sizeof(*Context));
    Soc = IntelGpioIdentify(Device);
    if (!Soc)
        return STATUS_NOT_SUPPORTED;
    Context->Soc = Soc;
    for (Index = 0; Index < Soc->GroupCount; Index++)
    {
        const INTELGPIO_GROUP *Group = &Soc->Groups[Index];

        if (Group->GpioBase >= 0)
            Context->TotalPins = max(Context->TotalPins, (USHORT)(Group->GpioBase + Group->PinCount));
    }
    Context->TotalPins = (USHORT)(((Context->TotalPins + INTELGPIO_PINS_PER_BANK - 1) / INTELGPIO_PINS_PER_BANK) * INTELGPIO_PINS_PER_BANK);

    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated) && CommunityIndex < Soc->CommunityCount; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        PINTELGPIO_COMMUNITY Community = &Context->Communities[CommunityIndex];
        ULONG Revision;
        ULONG PadCount = Soc->CommunityPinCount[CommunityIndex];

        if (!Resource || Resource->Type != CmResourceTypeMemory)
            continue;
        if (Resource->u.Memory.Length < (ULONG)Soc->InterruptEnableOffset + INTELGPIO_MAX_GROUPS * sizeof(ULONG))
            break;
        Community->RegisterBase = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!Community->RegisterBase)
            break;
        Community->RegisterLength = Resource->u.Memory.Length;
        Revision = IntelGpioRead(Community, INTELGPIO_REVID);
        if (Revision == MAXULONG)
            break;
        Community->PadStride = (Revision >> 16) >= INTELGPIO_REVISION_DEBOUNCE ? 4 * sizeof(ULONG) : 2 * sizeof(ULONG);
        Community->PadOffset = IntelGpioRead(Community, INTELGPIO_PADBAR);
        if (Community->PadOffset >= Community->RegisterLength || PadCount * Community->PadStride > Community->RegisterLength - Community->PadOffset)
            break;
        DPRINT("INTELGPIO: community %lu revision=0x%lx padbar=0x%lx pads=%lu\n", CommunityIndex, Revision >> 16, Community->PadOffset, PadCount);
        CommunityIndex++;
    }
    if (CommunityIndex != Soc->CommunityCount)
    {
        DPRINT1("INTELGPIO: mapped %lu of %u communities\n", CommunityIndex, Soc->CommunityCount);
        IntelGpioUnmap(Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);
    if (Context->Soc)
        IntelGpioQuiesceInterrupts(Context, FALSE);
    IntelGpioUnmap(Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioStartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(PreviousPowerState);
    if (!RestoreContext)
        RtlZeroMemory(Context->EnabledInterrupts, sizeof(Context->EnabledInterrupts));
    IntelGpioQuiesceInterrupts(Context, RestoreContext);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioStopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    IntelGpioQuiesceInterrupts(Context, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioQueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;

    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = Context->TotalPins;
    Information->NumberOfPinsPerBank = INTELGPIO_PINS_PER_BANK;
    Information->Flags.MemoryMappedController = TRUE;
    Information->Flags.ActiveInterruptsAutoClearOnRead = FALSE;
    Information->Flags.FormatIoRequestsAsMasks = TRUE;
    Information->Flags.DeviceIdlePowerMgmtSupported = FALSE;
    Information->Flags.BankIdlePowerMgmtSupported = FALSE;
    Information->Flags.EmulateDebouncing = FALSE;
    Information->Flags.EmulateActiveBoth = FALSE;
    Information->Flags.IndependentIoHwSupported = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioQuerySetControllerInformation(
    _In_ PVOID ContextPointer,
    _In_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_INPUT Input,
    _Out_opt_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT Output)
{
    ULONG RequiredSize;
    ULONG InterruptIndex = MAXULONG;
    ULONG Index;

    UNREFERENCED_PARAMETER(ContextPointer);
    if (!Input || !Output || Input->RequestType != QueryBankInterruptBindingInformation)
        return STATUS_NOT_SUPPORTED;
    RequiredSize = FIELD_OFFSET(CLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT, BankInterruptBinding.ResourceMapping) + Input->BankInterruptBinding.TotalBanks * sizeof(ULONG);
    if (Output->Size < RequiredSize)
    {
        Output->Size = (USHORT)RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }
    for (Index = 0; Index < WdfCmResourceListGetCount(Input->BankInterruptBinding.ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(Input->BankInterruptBinding.ResourcesTranslated, Index);

        if (Resource && Resource->Type == CmResourceTypeInterrupt)
        {
            InterruptIndex = Index;
            break;
        }
    }
    if (InterruptIndex == MAXULONG)
        return STATUS_NOT_FOUND;
    Output->Version = GPIO_BANK_INTERRUPT_BINDING_INFORMATION_OUTPUT_VERSION;
    Output->Size = (USHORT)RequiredSize;
    for (Index = 0; Index < Input->BankInterruptBinding.TotalBanks; Index++)
        Output->BankInterruptBinding.ResourceMapping[Index] = InterruptIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioEnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    PINTELGPIO_COMMUNITY Community;
    const INTELGPIO_GROUP *Group;
    ULONG GroupOffset;
    ULONG Offset;
    ULONG Value;
    ULONG NewValue;

    Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Parameters->PinNumber, &GroupOffset);
    if (!Group)
        return STATUS_INVALID_PARAMETER;
    if (Parameters->InterruptMode == LevelSensitive && Parameters->Polarity == InterruptActiveBoth)
        return STATUS_NOT_SUPPORTED;
    if (!IntelGpioIsHostOwned(Context, Group, GroupOffset) || IntelGpioIsAcpiMode(Context, Group, GroupOffset))
        return STATUS_ACCESS_DENIED;
    Community = &Context->Communities[Group->Community];
    Offset = IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG0);

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    if (!(IntelGpioGetLockState(Context, Group, GroupOffset) & INTELGPIO_LOCK_CONFIGURATION))
    {
        Value = IntelGpioRead(Community, Offset);
        NewValue = Value & ~(INTELGPIO_PADCFG0_PMODE_MASK | INTELGPIO_PADCFG0_RXDIS | INTELGPIO_PADCFG0_ROUTE_MASK | INTELGPIO_PADCFG0_RXEVCFG_MASK | INTELGPIO_PADCFG0_RXINV);
        NewValue |= INTELGPIO_PADCFG0_TXDIS;
        if (Parameters->InterruptMode == LevelSensitive)
            NewValue |= INTELGPIO_PADCFG0_RXEVCFG_LEVEL;
        else if (Parameters->Polarity == InterruptActiveBoth)
            NewValue |= INTELGPIO_PADCFG0_RXEVCFG_EDGE_BOTH;
        else
            NewValue |= INTELGPIO_PADCFG0_RXEVCFG_EDGE;
        if (Parameters->Polarity == InterruptActiveLow)
            NewValue |= INTELGPIO_PADCFG0_RXINV;
        NewValue = IntelGpioApplyDebounce(Context, Group, GroupOffset, Parameters->DebounceTimeout, NewValue);
        if (NewValue != Value)
            IntelGpioWrite(Community, Offset, NewValue);
        IntelGpioApplyPull(Context, Group, GroupOffset, Parameters->PullConfiguration);
    }
    IntelGpioClearInterruptStatus(Context, Group, GroupOffset);
    Context->EnabledInterrupts[IntelGpioGroupIndex(Context, Group)] |= 1UL << GroupOffset;
    IntelGpioSetInterruptEnable(Context, Group, GroupOffset, TRUE);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioDisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    const INTELGPIO_GROUP *Group;
    ULONG GroupOffset;

    Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Parameters->PinNumber, &GroupOffset);
    if (!Group)
        return STATUS_INVALID_PARAMETER;
    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    Context->EnabledInterrupts[IntelGpioGroupIndex(Context, Group)] &= ~(1UL << GroupOffset);
    IntelGpioSetInterruptEnable(Context, Group, GroupOffset, FALSE);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioUnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    const INTELGPIO_GROUP *Group;
    ULONG GroupOffset;

    Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Parameters->PinNumber, &GroupOffset);
    if (!Group)
        return STATUS_INVALID_PARAMETER;
    IntelGpioSetInterruptEnable(Context, Group, GroupOffset, TRUE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioMaskInterrupts(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Remaining = Parameters->PinMask;

    Parameters->FailedMask = 0;
    while (Remaining)
    {
        ULONG Pin = (ULONG)RtlFindLeastSignificantBit(Remaining);
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;

        Remaining &= ~(1ULL << Pin);
        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (Group)
            IntelGpioSetInterruptEnable(Context, Group, GroupOffset, FALSE);
        else
            Parameters->FailedMask |= 1ULL << Pin;
    }
    return Parameters->FailedMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioQueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    const INTELGPIO_SOC *Soc = Context->Soc;
    ULONG64 Remaining = Parameters->EnabledMask;

    Parameters->ActiveMask = 0;
    while (Remaining)
    {
        ULONG Pin = (ULONG)RtlFindLeastSignificantBit(Remaining);
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;

        Remaining &= ~(1ULL << Pin);
        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (Group)
        {
            PINTELGPIO_COMMUNITY Community = &Context->Communities[Group->Community];
            ULONG Pending;

            Pending = IntelGpioRead(Community, Soc->InterruptStatusOffset + Group->RegisterNumber * sizeof(ULONG));
            Pending &= IntelGpioRead(Community, Soc->InterruptEnableOffset + Group->RegisterNumber * sizeof(ULONG));
            if (Pending & (1UL << GroupOffset))
                Parameters->ActiveMask |= 1ULL << Pin;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Remaining = Parameters->ClearActiveMask;

    Parameters->FailedClearMask = 0;
    while (Remaining)
    {
        ULONG Pin = (ULONG)RtlFindLeastSignificantBit(Remaining);
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;

        Remaining &= ~(1ULL << Pin);
        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (Group)
            IntelGpioClearInterruptStatus(Context, Group, GroupOffset);
        else
            Parameters->FailedClearMask |= 1ULL << Pin;
    }
    return Parameters->FailedClearMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioQueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    ULONG Pin;

    Parameters->EnabledMask = 0;
    for (Pin = 0; Pin < INTELGPIO_PINS_PER_BANK; Pin++)
    {
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;

        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (Group && (Context->EnabledInterrupts[IntelGpioGroupIndex(Context, Group)] & (1UL << GroupOffset)))
            Parameters->EnabledMask |= 1ULL << Pin;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioConnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    USHORT Index;

    if (Parameters->ConnectMode != ConnectModeInput && Parameters->ConnectMode != ConnectModeOutput)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Parameters->PinCount; Index++)
    {
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;

        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Parameters->PinNumberTable[Index], &GroupOffset);
        if (!Group)
            return STATUS_INVALID_PARAMETER;
        if (!IntelGpioIsHostOwned(Context, Group, GroupOffset))
            return STATUS_ACCESS_DENIED;
    }
    for (Index = 0; Index < Parameters->PinCount; Index++)
    {
        PINTELGPIO_COMMUNITY Community;
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;
        ULONG Offset;
        ULONG Value;
        ULONG NewValue;

        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Parameters->PinNumberTable[Index], &GroupOffset);
        if (IntelGpioGetLockState(Context, Group, GroupOffset) & INTELGPIO_LOCK_CONFIGURATION)
            continue;
        Community = &Context->Communities[Group->Community];
        Offset = IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG0);
        GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
        Value = IntelGpioRead(Community, Offset);
        NewValue = Value & ~(INTELGPIO_PADCFG0_PMODE_MASK | INTELGPIO_PADCFG0_ROUTE_MASK);
        if (Parameters->ConnectMode == ConnectModeInput)
            NewValue = (NewValue & ~INTELGPIO_PADCFG0_RXDIS) | INTELGPIO_PADCFG0_TXDIS;
        else
            NewValue = (NewValue & ~INTELGPIO_PADCFG0_TXDIS) | INTELGPIO_PADCFG0_RXDIS;
        NewValue = IntelGpioApplyDebounce(Context, Group, GroupOffset, Parameters->DebounceTimeout, NewValue);
        if (NewValue != Value)
            IntelGpioWrite(Community, Offset, NewValue);
        IntelGpioApplyPull(Context, Group, GroupOffset, Parameters->PullConfiguration);
        GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioDisconnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(ContextPointer);
    UNREFERENCED_PARAMETER(Parameters);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioReadPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Values = 0;
    ULONG Pin;

    for (Pin = 0; Pin < INTELGPIO_PINS_PER_BANK; Pin++)
    {
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;
        ULONG Value;

        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (!Group)
            continue;
        Value = IntelGpioRead(&Context->Communities[Group->Community], IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG0));
        if (Value & INTELGPIO_PADCFG0_TXDIS)
            Value &= INTELGPIO_PADCFG0_RXSTATE;
        else
            Value &= INTELGPIO_PADCFG0_TXSTATE;
        if (Value)
            Values |= 1ULL << Pin;
    }
    *Parameters->PinValues = Values;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioWritePins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    PINTELGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Remaining = Parameters->SetMask | Parameters->ClearMask;
    NTSTATUS Status = STATUS_SUCCESS;

    while (Remaining)
    {
        ULONG Pin = (ULONG)RtlFindLeastSignificantBit(Remaining);
        PINTELGPIO_COMMUNITY Community;
        const INTELGPIO_GROUP *Group;
        ULONG GroupOffset;
        ULONG Offset;
        ULONG Value;

        Remaining &= ~(1ULL << Pin);
        Group = IntelGpioResolveBankPin(Context, Parameters->BankId, Pin, &GroupOffset);
        if (!Group)
        {
            Status = STATUS_INVALID_PARAMETER;
            continue;
        }
        if (!IntelGpioIsHostOwned(Context, Group, GroupOffset) || (IntelGpioGetLockState(Context, Group, GroupOffset) & INTELGPIO_LOCK_TRANSMIT))
        {
            Status = STATUS_ACCESS_DENIED;
            continue;
        }
        Community = &Context->Communities[Group->Community];
        Offset = IntelGpioPadOffset(Context, Group, GroupOffset, INTELGPIO_PADCFG0);
        Value = IntelGpioRead(Community, Offset);
        if (Value & (INTELGPIO_PADCFG0_PMODE_MASK | INTELGPIO_PADCFG0_TXDIS))
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            continue;
        }
        if (Parameters->SetMask & (1ULL << Pin))
            Value |= INTELGPIO_PADCFG0_TXSTATE;
        else
            Value &= ~INTELGPIO_PADCFG0_TXSTATE;
        IntelGpioWrite(Community, Offset, Value);
    }
    return Status;
}

static
NTSTATUS
NTAPI
IntelGpioEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE Device;
    NTSTATUS Status;

    Status = GPIO_CLX_ProcessAddDevicePreDeviceCreate(Driver, DeviceInit, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    return GPIO_CLX_ProcessAddDevicePostDeviceCreate(Driver, Device);
}

static
VOID
NTAPI
IntelGpioEvtDriverUnload(
    _In_ WDFDRIVER Driver)
{
    GPIO_CLX_UnregisterClient(Driver);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    GPIO_CLIENT_REGISTRATION_PACKET Packet;
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, IntelGpioEvtDeviceAdd);
    Config.DriverPoolTag = INTELGPIO_TAG;
    Config.EvtDriverUnload = IntelGpioEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(INTELGPIO_CONTEXT);
    Packet.CLIENT_PrepareController = IntelGpioPrepareController;
    Packet.CLIENT_ReleaseController = IntelGpioReleaseController;
    Packet.CLIENT_StartController = IntelGpioStartController;
    Packet.CLIENT_StopController = IntelGpioStopController;
    Packet.CLIENT_QueryControllerBasicInformation = IntelGpioQueryControllerBasicInformation;
    Packet.CLIENT_QuerySetControllerInformation = IntelGpioQuerySetControllerInformation;
    Packet.CLIENT_EnableInterrupt = IntelGpioEnableInterrupt;
    Packet.CLIENT_DisableInterrupt = IntelGpioDisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = IntelGpioUnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = IntelGpioMaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = IntelGpioQueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = IntelGpioClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = IntelGpioQueryEnabledInterrupts;
    Packet.CLIENT_ConnectIoPins = IntelGpioConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = IntelGpioDisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = IntelGpioReadPins;
    Packet.CLIENT_WriteGpioPinsUsingMask = IntelGpioWritePins;
    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
