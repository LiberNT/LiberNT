/*
 * PROJECT:     LiberNT Intel Serial I/O I2C Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Intel LPSS DesignWare I2C controller support
 */

#include <ntddk.h>
#include <initguid.h>
#include <reactos/drivers/inteli2c.h>

#define NDEBUG
#include <debug.h>

#define INTELI2C_TAG 'c2II'
#define INTELI2C_DEFAULT_SPEED 400000UL
#define INTELI2C_DEFAULT_TIMEOUT_MS 1000UL
#define INTELI2C_TARGET_SPIN_TIME 20000ULL
#define INTELI2C_TARGET_IDLE_INTERVAL 10000LL

#define LPSS_PRIV_RESETS 0x204
#define LPSS_PRIV_RESETS_FUNC 0x00000003
#define LPSS_PRIV_RESETS_IDMA 0x00000004
#define LPSS_PRIV_REMAP_ADDR_LOW 0x240
#define LPSS_PRIV_REMAP_ADDR_HIGH 0x244
#define LPSS_PRIV_CAPS 0x2fc
#define LPSS_PRIV_CAPS_TYPE_MASK 0x000000f0
#define LPSS_PRIV_CAPS_TYPE_I2C 0x00000000

#define DW_IC_CON 0x00
#define DW_IC_TAR 0x04
#define DW_IC_SAR 0x08
#define DW_IC_DATA_CMD 0x10
#define DW_IC_SS_SCL_HCNT 0x14
#define DW_IC_SS_SCL_LCNT 0x18
#define DW_IC_FS_SCL_HCNT 0x1c
#define DW_IC_FS_SCL_LCNT 0x20
#define DW_IC_INTR_MASK 0x30
#define DW_IC_RAW_INTR_STAT 0x34
#define DW_IC_RX_TL 0x38
#define DW_IC_TX_TL 0x3c
#define DW_IC_CLR_INTR 0x40
#define DW_IC_CLR_RX_UNDER 0x44
#define DW_IC_CLR_RX_OVER 0x48
#define DW_IC_CLR_TX_OVER 0x4c
#define DW_IC_CLR_RD_REQ 0x50
#define DW_IC_CLR_TX_ABRT 0x54
#define DW_IC_CLR_RX_DONE 0x58
#define DW_IC_CLR_STOP_DET 0x60
#define DW_IC_ENABLE 0x6c
#define DW_IC_STATUS 0x70
#define DW_IC_TXFLR 0x74
#define DW_IC_RXFLR 0x78
#define DW_IC_SDA_HOLD 0x7c
#define DW_IC_TX_ABRT_SOURCE 0x80
#define DW_IC_ENABLE_STATUS 0x9c
#define DW_IC_COMP_PARAM_1 0xf4
#define DW_IC_COMP_VERSION 0xf8
#define DW_IC_COMP_TYPE 0xfc

#define DW_IC_COMP_TYPE_VALUE 0x44570140
#define DW_IC_SDA_HOLD_MIN_VERSION 0x3131312a

#define DW_IC_CON_MASTER 0x00000001
#define DW_IC_CON_SPEED_STANDARD 0x00000002
#define DW_IC_CON_SPEED_FAST 0x00000004
#define DW_IC_CON_10BITADDR_SLAVE 0x00000008
#define DW_IC_CON_10BITADDR_MASTER 0x00000010
#define DW_IC_CON_RESTART_EN 0x00000020
#define DW_IC_CON_SLAVE_DISABLE 0x00000040
#define DW_IC_CON_STOP_DET_IFADDRESSED 0x00000080
#define DW_IC_CON_RX_FIFO_FULL_HLD_CTRL 0x00000200

#define DW_IC_TAR_10BITADDR_MASTER 0x00001000
#define DW_IC_DATA_CMD_READ 0x00000100
#define DW_IC_DATA_CMD_STOP 0x00000200
#define DW_IC_DATA_CMD_RESTART 0x00000400

#define DW_IC_INTR_RX_UNDER 0x00000001
#define DW_IC_INTR_RX_OVER 0x00000002
#define DW_IC_INTR_TX_OVER 0x00000008
#define DW_IC_INTR_RD_REQ 0x00000020
#define DW_IC_INTR_TX_ABRT 0x00000040
#define DW_IC_INTR_RX_DONE 0x00000080
#define DW_IC_INTR_STOP_DET 0x00000200
#define DW_IC_INTR_TARGET_ERRORS (DW_IC_INTR_RX_UNDER | DW_IC_INTR_RX_OVER | DW_IC_INTR_TX_OVER)

#define DW_IC_STATUS_MASTER_ACTIVITY 0x00000020

#define DW_IC_TX_ABRT_7B_ADDR_NOACK 0x00000001
#define DW_IC_TX_ABRT_10ADDR1_NOACK 0x00000002
#define DW_IC_TX_ABRT_10ADDR2_NOACK 0x00000004
#define DW_IC_TX_ABRT_SLVFLUSH_TXFIFO 0x00002000
#define DW_IC_TX_ABRT_SLV_ARBLOST 0x00004000
#define DW_IC_TX_ABRT_SLVRD_INTX 0x00008000

typedef struct _INTELI2C_PROFILE
{
    ULONG InputClockHz;
    ULONG SdaHoldNanoseconds;
    ULONG SdaFallNanoseconds;
    ULONG SclFallNanoseconds;
} INTELI2C_PROFILE;

typedef struct _INTELI2C_PCI_ID
{
    USHORT DeviceId;
    UCHAR ControllerIndex;
    const INTELI2C_PROFILE *Profile;
} INTELI2C_PCI_ID;

typedef struct _INTELI2C_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    FAST_MUTEX TransferLock;
    KMUTEX TargetLock;
    KEVENT TargetStopEvent;
    PKTHREAD TargetThread;
    PFILE_OBJECT TargetOwner;
    UNICODE_STRING InterfaceName;
    PVOID Registers;
    PHYSICAL_ADDRESS RegisterAddress;
    ULONG RegisterLength;
    ULONG ControllerIndex;
    ULONG PciDeviceId;
    const INTELI2C_PROFILE *Profile;
    ULONG TxFifoDepth;
    ULONG RxFifoDepth;
    ULONG TargetResponseLength;
    ULONG TargetResponseOffset;
    BOOLEAN Started;
    BOOLEAN TargetActive;
    INTELI2C_TARGET_STATUS TargetStatus;
    UCHAR TargetResponse[INTELI2C_TARGET_BUFFER_SIZE];
} INTELI2C_DEVICE_EXTENSION, *PINTELI2C_DEVICE_EXTENSION;

static const INTELI2C_PROFILE IntelI2cSptProfile = {120000000, 230, 300, 300};
static const INTELI2C_PROFILE IntelI2cBxtProfile = {133000000, 42, 171, 208};
static const INTELI2C_PROFILE IntelI2cAplProfile = {133000000, 207, 171, 208};
static const INTELI2C_PROFILE IntelI2cGlkProfile = {133000000, 313, 171, 290};
static const INTELI2C_PROFILE IntelI2cCnlProfile = {216000000, 230, 300, 300};
static const INTELI2C_PROFILE IntelI2cEhlProfile = {100000000, 42, 171, 208};

static const INTELI2C_PCI_ID IntelI2cPciIds[] =
{
    {0x02e8, 0, &IntelI2cCnlProfile},
    {0x02e9, 1, &IntelI2cCnlProfile},
    {0x02ea, 2, &IntelI2cCnlProfile},
    {0x02eb, 3, &IntelI2cCnlProfile},
    {0x02c5, 4, &IntelI2cCnlProfile},
    {0x02c6, 5, &IntelI2cCnlProfile},
    {0x06e8, 0, &IntelI2cCnlProfile},
    {0x06e9, 1, &IntelI2cCnlProfile},
    {0x06ea, 2, &IntelI2cCnlProfile},
    {0x06eb, 3, &IntelI2cCnlProfile},
    {0x0aac, 0, &IntelI2cBxtProfile},
    {0x0aae, 1, &IntelI2cBxtProfile},
    {0x0ab0, 2, &IntelI2cBxtProfile},
    {0x0ab2, 3, &IntelI2cBxtProfile},
    {0x0ab4, 4, &IntelI2cBxtProfile},
    {0x0ab6, 5, &IntelI2cBxtProfile},
    {0x0ab8, 6, &IntelI2cBxtProfile},
    {0x0aba, 7, &IntelI2cBxtProfile},
    {0x1aac, 0, &IntelI2cBxtProfile},
    {0x1aae, 1, &IntelI2cBxtProfile},
    {0x1ab0, 2, &IntelI2cBxtProfile},
    {0x1ab2, 3, &IntelI2cBxtProfile},
    {0x1ab4, 4, &IntelI2cBxtProfile},
    {0x1ab6, 5, &IntelI2cBxtProfile},
    {0x1ab8, 6, &IntelI2cBxtProfile},
    {0x1aba, 7, &IntelI2cBxtProfile},
    {0x31ac, 0, &IntelI2cGlkProfile},
    {0x31ae, 1, &IntelI2cGlkProfile},
    {0x31b0, 2, &IntelI2cGlkProfile},
    {0x31b2, 3, &IntelI2cGlkProfile},
    {0x31b4, 4, &IntelI2cGlkProfile},
    {0x31b6, 5, &IntelI2cGlkProfile},
    {0x31b8, 6, &IntelI2cGlkProfile},
    {0x31ba, 7, &IntelI2cGlkProfile},
    {0x34e8, 0, &IntelI2cBxtProfile},
    {0x34e9, 1, &IntelI2cBxtProfile},
    {0x34ea, 2, &IntelI2cBxtProfile},
    {0x34eb, 3, &IntelI2cBxtProfile},
    {0x34c5, 4, &IntelI2cBxtProfile},
    {0x34c6, 5, &IntelI2cBxtProfile},
    {0x43e8, 0, &IntelI2cBxtProfile},
    {0x43e9, 1, &IntelI2cBxtProfile},
    {0x43ea, 2, &IntelI2cBxtProfile},
    {0x43eb, 3, &IntelI2cBxtProfile},
    {0x43ad, 4, &IntelI2cBxtProfile},
    {0x43ae, 5, &IntelI2cBxtProfile},
    {0x43d8, 6, &IntelI2cBxtProfile},
    {0x4b44, 0, &IntelI2cEhlProfile},
    {0x4b45, 1, &IntelI2cEhlProfile},
    {0x4b4b, 2, &IntelI2cEhlProfile},
    {0x4b4c, 3, &IntelI2cEhlProfile},
    {0x4b78, 4, &IntelI2cEhlProfile},
    {0x4b79, 5, &IntelI2cEhlProfile},
    {0x4b7a, 6, &IntelI2cEhlProfile},
    {0x4b7b, 7, &IntelI2cEhlProfile},
    {0x4d50, 0, &IntelI2cEhlProfile},
    {0x4d51, 1, &IntelI2cEhlProfile},
    {0x4d78, 2, &IntelI2cEhlProfile},
    {0x4d79, 3, &IntelI2cEhlProfile},
    {0x4d7a, 4, &IntelI2cEhlProfile},
    {0x4d7b, 5, &IntelI2cEhlProfile},
    {0x4de8, 0, &IntelI2cBxtProfile},
    {0x4de9, 1, &IntelI2cBxtProfile},
    {0x4dea, 2, &IntelI2cBxtProfile},
    {0x4deb, 3, &IntelI2cBxtProfile},
    {0x4dc5, 4, &IntelI2cBxtProfile},
    {0x4dc6, 5, &IntelI2cBxtProfile},
    {0x51e8, 0, &IntelI2cBxtProfile},
    {0x51e9, 1, &IntelI2cBxtProfile},
    {0x51ea, 2, &IntelI2cBxtProfile},
    {0x51eb, 3, &IntelI2cBxtProfile},
    {0x51c5, 4, &IntelI2cBxtProfile},
    {0x51c6, 5, &IntelI2cBxtProfile},
    {0x51d8, 6, &IntelI2cBxtProfile},
    {0x51d9, 7, &IntelI2cBxtProfile},
    {0x54e8, 0, &IntelI2cBxtProfile},
    {0x54e9, 1, &IntelI2cBxtProfile},
    {0x54ea, 2, &IntelI2cBxtProfile},
    {0x54eb, 3, &IntelI2cBxtProfile},
    {0x54c5, 4, &IntelI2cBxtProfile},
    {0x54c6, 5, &IntelI2cBxtProfile},
    {0x5aac, 0, &IntelI2cAplProfile},
    {0x5aae, 1, &IntelI2cAplProfile},
    {0x5ab0, 2, &IntelI2cAplProfile},
    {0x5ab2, 3, &IntelI2cAplProfile},
    {0x5ab4, 4, &IntelI2cAplProfile},
    {0x5ab6, 5, &IntelI2cAplProfile},
    {0x5ab8, 6, &IntelI2cAplProfile},
    {0x5aba, 7, &IntelI2cAplProfile},
    {0x6e4c, 0, &IntelI2cEhlProfile},
    {0x6e4d, 1, &IntelI2cEhlProfile},
    {0x6e4e, 2, &IntelI2cEhlProfile},
    {0x6e4f, 3, &IntelI2cEhlProfile},
    {0x6e7a, 4, &IntelI2cEhlProfile},
    {0x6e7b, 5, &IntelI2cEhlProfile},
    {0x7750, 0, &IntelI2cBxtProfile},
    {0x7751, 1, &IntelI2cBxtProfile},
    {0x7778, 2, &IntelI2cBxtProfile},
    {0x7779, 3, &IntelI2cBxtProfile},
    {0x777a, 4, &IntelI2cBxtProfile},
    {0x777b, 5, &IntelI2cBxtProfile},
    {0x7a4c, 0, &IntelI2cBxtProfile},
    {0x7a4d, 1, &IntelI2cBxtProfile},
    {0x7a4e, 2, &IntelI2cBxtProfile},
    {0x7a4f, 3, &IntelI2cBxtProfile},
    {0x7a7c, 4, &IntelI2cBxtProfile},
    {0x7a7d, 5, &IntelI2cBxtProfile},
    {0x7acc, 0, &IntelI2cBxtProfile},
    {0x7acd, 1, &IntelI2cBxtProfile},
    {0x7ace, 2, &IntelI2cBxtProfile},
    {0x7acf, 3, &IntelI2cBxtProfile},
    {0x7afc, 4, &IntelI2cBxtProfile},
    {0x7afd, 5, &IntelI2cBxtProfile},
    {0x7e50, 0, &IntelI2cBxtProfile},
    {0x7e51, 1, &IntelI2cBxtProfile},
    {0x7e78, 2, &IntelI2cBxtProfile},
    {0x7e79, 3, &IntelI2cBxtProfile},
    {0x7e7a, 4, &IntelI2cBxtProfile},
    {0x7e7b, 5, &IntelI2cBxtProfile},
    {0x7f4c, 0, &IntelI2cBxtProfile},
    {0x7f4d, 1, &IntelI2cBxtProfile},
    {0x7f4e, 2, &IntelI2cBxtProfile},
    {0x7f4f, 3, &IntelI2cBxtProfile},
    {0x7f7a, 4, &IntelI2cBxtProfile},
    {0x7f7b, 5, &IntelI2cBxtProfile},
    {0x98e8, 0, &IntelI2cBxtProfile},
    {0x98e9, 1, &IntelI2cBxtProfile},
    {0x98ea, 2, &IntelI2cBxtProfile},
    {0x98eb, 3, &IntelI2cBxtProfile},
    {0x98c5, 4, &IntelI2cBxtProfile},
    {0x98c6, 5, &IntelI2cBxtProfile},
    {0x9d60, 0, &IntelI2cSptProfile},
    {0x9d61, 1, &IntelI2cSptProfile},
    {0x9d62, 2, &IntelI2cSptProfile},
    {0x9d63, 3, &IntelI2cSptProfile},
    {0x9d64, 4, &IntelI2cSptProfile},
    {0x9d65, 5, &IntelI2cSptProfile},
    {0x9de8, 0, &IntelI2cCnlProfile},
    {0x9de9, 1, &IntelI2cCnlProfile},
    {0x9dea, 2, &IntelI2cCnlProfile},
    {0x9deb, 3, &IntelI2cCnlProfile},
    {0x9dc5, 4, &IntelI2cCnlProfile},
    {0x9dc6, 5, &IntelI2cCnlProfile},
    {0xa0e8, 0, &IntelI2cSptProfile},
    {0xa0e9, 1, &IntelI2cSptProfile},
    {0xa0ea, 2, &IntelI2cSptProfile},
    {0xa0eb, 3, &IntelI2cSptProfile},
    {0xa0c5, 4, &IntelI2cSptProfile},
    {0xa0c6, 5, &IntelI2cSptProfile},
    {0xa0d8, 6, &IntelI2cSptProfile},
    {0xa0d9, 7, &IntelI2cSptProfile},
    {0xa160, 0, &IntelI2cSptProfile},
    {0xa161, 1, &IntelI2cSptProfile},
    {0xa162, 2, &IntelI2cSptProfile},
    {0xa2e0, 0, &IntelI2cSptProfile},
    {0xa2e1, 1, &IntelI2cSptProfile},
    {0xa2e2, 2, &IntelI2cSptProfile},
    {0xa2e3, 3, &IntelI2cSptProfile},
    {0xa368, 0, &IntelI2cCnlProfile},
    {0xa369, 1, &IntelI2cCnlProfile},
    {0xa36a, 2, &IntelI2cCnlProfile},
    {0xa36b, 3, &IntelI2cCnlProfile},
    {0xa3e0, 0, &IntelI2cSptProfile},
    {0xa3e1, 1, &IntelI2cSptProfile},
    {0xa3e2, 2, &IntelI2cSptProfile},
    {0xa3e3, 3, &IntelI2cSptProfile},
    {0xa850, 0, &IntelI2cEhlProfile},
    {0xa851, 1, &IntelI2cEhlProfile},
    {0xa878, 2, &IntelI2cEhlProfile},
    {0xa879, 3, &IntelI2cEhlProfile},
    {0xa87a, 4, &IntelI2cEhlProfile},
    {0xa87b, 5, &IntelI2cEhlProfile},
    {0xe350, 0, &IntelI2cEhlProfile},
    {0xe351, 1, &IntelI2cEhlProfile},
    {0xe378, 2, &IntelI2cEhlProfile},
    {0xe379, 3, &IntelI2cEhlProfile},
    {0xe37a, 4, &IntelI2cEhlProfile},
    {0xe37b, 5, &IntelI2cEhlProfile},
    {0xe450, 0, &IntelI2cEhlProfile},
    {0xe451, 1, &IntelI2cEhlProfile},
    {0xe478, 2, &IntelI2cEhlProfile},
    {0xe479, 3, &IntelI2cEhlProfile},
    {0xe47a, 4, &IntelI2cEhlProfile},
    {0xe47b, 5, &IntelI2cEhlProfile},
};

static
ULONG
IntelI2cRead32(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->Registers + Offset));
}

static
VOID
IntelI2cWrite32(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->Registers + Offset), Value);
}

static
BOOLEAN
IntelI2cParseDeviceId(
    _In_reads_(CharacterCount) PCWSTR Buffer,
    _In_ ULONG CharacterCount,
    _Out_ PULONG DeviceId)
{
    static const WCHAR Prefix[] = L"DEV_";
    ULONG Index;

    for (Index = 0; Index + 8 <= CharacterCount; Index++)
    {
        ULONG Value = 0;
        ULONG Digit;

        for (Digit = 0; Digit < 4; Digit++)
        {
            if (RtlUpcaseUnicodeChar(Buffer[Index + Digit]) != Prefix[Digit])
                break;
        }
        if (Digit != 4)
            continue;
        for (Digit = 0; Digit < 4; Digit++)
        {
            WCHAR Character = RtlUpcaseUnicodeChar(Buffer[Index + 4 + Digit]);

            if (Character >= L'0' && Character <= L'9')
                Value = (Value << 4) | (ULONG)(Character - L'0');
            else if (Character >= L'A' && Character <= L'F')
                Value = (Value << 4) | (ULONG)(Character - L'A' + 10);
            else
                break;
        }
        if (Digit != 4)
            continue;
        *DeviceId = Value;
        return TRUE;
    }
    return FALSE;
}

static
const INTELI2C_PCI_ID *
IntelI2cIdentifyController(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    WCHAR HardwareIds[256];
    ULONG RequiredLength = 0;
    ULONG DeviceId;
    ULONG Index;

    if (!NT_SUCCESS(IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, sizeof(HardwareIds), HardwareIds, &RequiredLength)))
        return NULL;
    RequiredLength = min(RequiredLength, (ULONG)sizeof(HardwareIds));
    if (!IntelI2cParseDeviceId(HardwareIds, RequiredLength / sizeof(WCHAR), &DeviceId))
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(IntelI2cPciIds); Index++)
    {
        if (IntelI2cPciIds[Index].DeviceId == DeviceId)
            return &IntelI2cPciIds[Index];
    }
    return NULL;
}

static
ULONGLONG
IntelI2cDeadline(
    _In_ ULONG TimeoutMilliseconds)
{
    return KeQueryInterruptTime() + (ULONGLONG)TimeoutMilliseconds * 10000;
}

static
NTSTATUS
IntelI2cWaitForRegister(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Expected,
    _In_ ULONGLONG Deadline)
{
    do
    {
        if ((IntelI2cRead32(DeviceExtension, Offset) & Mask) == Expected)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
IntelI2cCheckAbort(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG AbortSource;

    if (!(IntelI2cRead32(DeviceExtension, DW_IC_RAW_INTR_STAT) & DW_IC_INTR_TX_ABRT))
        return STATUS_SUCCESS;
    AbortSource = IntelI2cRead32(DeviceExtension, DW_IC_TX_ABRT_SOURCE);
    IntelI2cRead32(DeviceExtension, DW_IC_CLR_TX_ABRT);
    DPRINT("INTELI2C%lu: transfer aborted, source=0x%08lx\n", DeviceExtension->ControllerIndex, AbortSource);
    if (AbortSource & (DW_IC_TX_ABRT_7B_ADDR_NOACK | DW_IC_TX_ABRT_10ADDR1_NOACK | DW_IC_TX_ABRT_10ADDR2_NOACK))
        return STATUS_NO_SUCH_DEVICE;
    return STATUS_IO_DEVICE_ERROR;
}

static
NTSTATUS
IntelI2cDisable(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG Deadline)
{
    IntelI2cWrite32(DeviceExtension, DW_IC_ENABLE, 0);
    return IntelI2cWaitForRegister(DeviceExtension, DW_IC_ENABLE_STATUS, 1, 0, Deadline);
}

static
ULONG
IntelI2cTimingCount(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Nanoseconds,
    _In_ ULONG Adjustment)
{
    ULONGLONG Count;

    Count = ((ULONGLONG)DeviceExtension->Profile->InputClockHz * Nanoseconds + 500000000) / 1000000000;
    if (Count > Adjustment)
        Count -= Adjustment;
    else
        Count = 1;
    return (ULONG)max(Count, 8ULL);
}

static
VOID
IntelI2cProgramTiming(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ConnectionSpeed)
{
    const INTELI2C_PROFILE *Profile = DeviceExtension->Profile;
    ULONG HighNanoseconds;
    ULONG LowNanoseconds;

    IntelI2cWrite32(DeviceExtension, DW_IC_SS_SCL_HCNT, IntelI2cTimingCount(DeviceExtension, 4000 + Profile->SdaFallNanoseconds, 3));
    IntelI2cWrite32(DeviceExtension, DW_IC_SS_SCL_LCNT, IntelI2cTimingCount(DeviceExtension, 4700 + Profile->SclFallNanoseconds, 1));
    if (ConnectionSpeed <= 400000)
    {
        HighNanoseconds = 600;
        LowNanoseconds = 1300;
    }
    else
    {
        HighNanoseconds = 260;
        LowNanoseconds = 500;
    }
    IntelI2cWrite32(DeviceExtension, DW_IC_FS_SCL_HCNT, IntelI2cTimingCount(DeviceExtension, HighNanoseconds + Profile->SdaFallNanoseconds, 3));
    IntelI2cWrite32(DeviceExtension, DW_IC_FS_SCL_LCNT, IntelI2cTimingCount(DeviceExtension, LowNanoseconds + Profile->SclFallNanoseconds, 1));
    if (IntelI2cRead32(DeviceExtension, DW_IC_COMP_VERSION) >= DW_IC_SDA_HOLD_MIN_VERSION)
        IntelI2cWrite32(DeviceExtension, DW_IC_SDA_HOLD, IntelI2cTimingCount(DeviceExtension, Profile->SdaHoldNanoseconds, 0));
}

static
NTSTATUS
IntelI2cConfigureTransfer(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELI2C_TRANSFER_REQUEST Request,
    _In_ ULONGLONG Deadline)
{
    ULONG Control;
    ULONG Target;
    NTSTATUS Status;

    Status = IntelI2cWaitForRegister(DeviceExtension, DW_IC_STATUS, DW_IC_STATUS_MASTER_ACTIVITY, 0, Deadline);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IntelI2cDisable(DeviceExtension, Deadline);
    if (!NT_SUCCESS(Status))
        return Status;
    Control = DW_IC_CON_MASTER | DW_IC_CON_RESTART_EN | DW_IC_CON_SLAVE_DISABLE;
    Control |= Request->ConnectionSpeed <= 100000 ? DW_IC_CON_SPEED_STANDARD : DW_IC_CON_SPEED_FAST;
    Target = Request->SlaveAddress;
    if (Request->AddressMode == INTELI2C_ADDRESS_MODE_10BIT)
    {
        Control |= DW_IC_CON_10BITADDR_MASTER;
        Target |= DW_IC_TAR_10BITADDR_MASTER;
    }
    IntelI2cProgramTiming(DeviceExtension, Request->ConnectionSpeed);
    IntelI2cWrite32(DeviceExtension, DW_IC_CON, Control);
    IntelI2cWrite32(DeviceExtension, DW_IC_TAR, Target);
    IntelI2cWrite32(DeviceExtension, DW_IC_RX_TL, 0);
    IntelI2cWrite32(DeviceExtension, DW_IC_TX_TL, 0);
    IntelI2cWrite32(DeviceExtension, DW_IC_INTR_MASK, 0);
    IntelI2cRead32(DeviceExtension, DW_IC_CLR_INTR);
    IntelI2cWrite32(DeviceExtension, DW_IC_ENABLE, 1);
    return IntelI2cWaitForRegister(DeviceExtension, DW_IC_ENABLE_STATUS, 1, 1, Deadline);
}

static
NTSTATUS
IntelI2cWaitForTxSpace(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG Deadline)
{
    NTSTATUS Status;

    do
    {
        Status = IntelI2cCheckAbort(DeviceExtension);
        if (!NT_SUCCESS(Status))
            return Status;
        if (IntelI2cRead32(DeviceExtension, DW_IC_TXFLR) < DeviceExtension->TxFifoDepth)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
IntelI2cWaitForRxData(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG Deadline)
{
    NTSTATUS Status;

    do
    {
        Status = IntelI2cCheckAbort(DeviceExtension);
        if (!NT_SUCCESS(Status))
            return Status;
        if (IntelI2cRead32(DeviceExtension, DW_IC_RXFLR))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
IntelI2cWaitForStop(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG Deadline)
{
    NTSTATUS Status;

    do
    {
        Status = IntelI2cCheckAbort(DeviceExtension);
        if (!NT_SUCCESS(Status))
            return Status;
        if (IntelI2cRead32(DeviceExtension, DW_IC_RAW_INTR_STAT) & DW_IC_INTR_STOP_DET)
        {
            Status = IntelI2cCheckAbort(DeviceExtension);
            IntelI2cRead32(DeviceExtension, DW_IC_CLR_STOP_DET);
            return Status;
        }
        KeStallExecutionProcessor(5);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static
VOID
IntelI2cTransferDelay(
    _In_ ULONG DelayInUs)
{
    LARGE_INTEGER Interval;

    if (!DelayInUs)
        return;
    if (DelayInUs <= 50)
    {
        KeStallExecutionProcessor(DelayInUs);
        return;
    }
    Interval.QuadPart = -(LONGLONG)DelayInUs * 10;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
IntelI2cValidateRequest(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELI2C_TRANSFER_REQUEST Request,
    _In_ ULONG BufferLength)
{
    ULONG EntriesLength;
    ULONG Index;

    if (Request->Version != INTELI2C_INTERFACE_VERSION)
        return STATUS_REVISION_MISMATCH;
    if (Request->ControllerIndex != DeviceExtension->ControllerIndex)
        return STATUS_NO_SUCH_DEVICE;
    if (!Request->TransferCount || Request->TransferCount > 64)
        return STATUS_INVALID_PARAMETER;
    if (Request->TransferCount > (MAXULONG - FIELD_OFFSET(INTELI2C_TRANSFER_REQUEST, Transfers)) / sizeof(INTELI2C_TRANSFER_ENTRY))
        return STATUS_INTEGER_OVERFLOW;
    EntriesLength = FIELD_OFFSET(INTELI2C_TRANSFER_REQUEST, Transfers) + Request->TransferCount * sizeof(INTELI2C_TRANSFER_ENTRY);
    if (EntriesLength > BufferLength)
        return STATUS_BUFFER_TOO_SMALL;
    if ((Request->AddressMode == INTELI2C_ADDRESS_MODE_7BIT && Request->SlaveAddress > 0x7f) || (Request->AddressMode == INTELI2C_ADDRESS_MODE_10BIT && Request->SlaveAddress > 0x3ff) || Request->AddressMode > INTELI2C_ADDRESS_MODE_10BIT)
        return STATUS_INVALID_PARAMETER;
    if (Request->ConnectionSpeed < 10000 || Request->ConnectionSpeed > 1000000)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Request->TransferCount; Index++)
    {
        PINTELI2C_TRANSFER_ENTRY Entry = &Request->Transfers[Index];

        if ((Entry->Direction != IntelI2cTransferDirectionRead && Entry->Direction != IntelI2cTransferDirectionWrite) || !Entry->BufferLength)
            return STATUS_INVALID_PARAMETER;
        if (Entry->BufferOffset < EntriesLength || Entry->BufferOffset > BufferLength || Entry->BufferLength > BufferLength - Entry->BufferOffset)
            return STATUS_INVALID_PARAMETER;
        Entry->Transferred = 0;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntelI2cExecuteTransfer(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PINTELI2C_TRANSFER_REQUEST Request,
    _In_ ULONG BufferLength)
{
    ULONG TimeoutMilliseconds;
    ULONGLONG Deadline;
    ULONG TransferIndex;
    NTSTATUS Status;

    Status = IntelI2cValidateRequest(DeviceExtension, Request, BufferLength);
    if (!NT_SUCCESS(Status))
        return Status;
    TimeoutMilliseconds = Request->TimeoutMilliseconds ? Request->TimeoutMilliseconds : INTELI2C_DEFAULT_TIMEOUT_MS;
    TimeoutMilliseconds = min(TimeoutMilliseconds, INTELI2C_MAXIMUM_TIMEOUT_MS);
    Deadline = IntelI2cDeadline(TimeoutMilliseconds);
    ExAcquireFastMutex(&DeviceExtension->TransferLock);
    if (!DeviceExtension->Started)
    {
        ExReleaseFastMutex(&DeviceExtension->TransferLock);
        return STATUS_DEVICE_NOT_READY;
    }
    if (DeviceExtension->TargetActive)
    {
        ExReleaseFastMutex(&DeviceExtension->TransferLock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Status = IntelI2cConfigureTransfer(DeviceExtension, Request, Deadline);
    if (!NT_SUCCESS(Status))
        goto Finish;
    for (TransferIndex = 0; TransferIndex < Request->TransferCount; TransferIndex++)
    {
        PINTELI2C_TRANSFER_ENTRY Entry = &Request->Transfers[TransferIndex];
        PUCHAR Buffer = (PUCHAR)Request + Entry->BufferOffset;
        ULONG ByteIndex;

        IntelI2cTransferDelay(Entry->DelayInUs);
        for (ByteIndex = 0; ByteIndex < Entry->BufferLength; ByteIndex++)
        {
            ULONG Command = 0;

            if (TransferIndex && !ByteIndex)
                Command |= DW_IC_DATA_CMD_RESTART;
            if (TransferIndex == Request->TransferCount - 1 && ByteIndex == Entry->BufferLength - 1)
                Command |= DW_IC_DATA_CMD_STOP;
            if (Entry->Direction == IntelI2cTransferDirectionRead)
                Command |= DW_IC_DATA_CMD_READ;
            else
                Command |= Buffer[ByteIndex];
            Status = IntelI2cWaitForTxSpace(DeviceExtension, Deadline);
            if (!NT_SUCCESS(Status))
                goto Finish;
            IntelI2cWrite32(DeviceExtension, DW_IC_DATA_CMD, Command);
            if (Entry->Direction == IntelI2cTransferDirectionRead)
            {
                Status = IntelI2cWaitForRxData(DeviceExtension, Deadline);
                if (!NT_SUCCESS(Status))
                    goto Finish;
                Buffer[ByteIndex] = (UCHAR)IntelI2cRead32(DeviceExtension, DW_IC_DATA_CMD);
            }
            Entry->Transferred++;
        }
    }
    Status = IntelI2cWaitForStop(DeviceExtension, Deadline);

Finish:
    IntelI2cDisable(DeviceExtension, IntelI2cDeadline(100));
    if (!NT_SUCCESS(Status))
        DPRINT1("INTELI2C%lu: request to address 0x%x failed, status=0x%08lx\n", DeviceExtension->ControllerIndex, Request->SlaveAddress, Status);
    ExReleaseFastMutex(&DeviceExtension->TransferLock);
    return Status;
}

static
BOOLEAN
IntelI2cDrainTarget(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    PINTELI2C_TARGET_STATUS TargetStatus = &DeviceExtension->TargetStatus;
    ULONG Count;

    Count = min(IntelI2cRead32(DeviceExtension, DW_IC_RXFLR), DeviceExtension->RxFifoDepth);
    if (!Count)
        return FALSE;
    while (Count--)
    {
        UCHAR Data = (UCHAR)IntelI2cRead32(DeviceExtension, DW_IC_DATA_CMD);

        if (TargetStatus->ReceivedLength < INTELI2C_TARGET_BUFFER_SIZE)
            TargetStatus->Received[TargetStatus->ReceivedLength++] = Data;
        TargetStatus->ReceivedTotal++;
    }
    return TRUE;
}

static
VOID
IntelI2cFeedTarget(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Level;
    ULONG Count;

    Level = IntelI2cRead32(DeviceExtension, DW_IC_TXFLR);
    if (Level >= DeviceExtension->TxFifoDepth)
        return;
    Count = DeviceExtension->TxFifoDepth - Level;
    if (DeviceExtension->TargetResponseOffset < DeviceExtension->TargetResponseLength)
        Count = min(Count, DeviceExtension->TargetResponseLength - DeviceExtension->TargetResponseOffset);
    else
        Count = 1;
    while (Count--)
    {
        UCHAR Data = 0xff;

        if (DeviceExtension->TargetResponseOffset < DeviceExtension->TargetResponseLength)
            Data = DeviceExtension->TargetResponse[DeviceExtension->TargetResponseOffset++];
        IntelI2cWrite32(DeviceExtension, DW_IC_DATA_CMD, Data);
        DeviceExtension->TargetStatus.ResponseQueued++;
    }
}

static
BOOLEAN
IntelI2cServiceTarget(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    PINTELI2C_TARGET_STATUS TargetStatus = &DeviceExtension->TargetStatus;
    ULONG Interrupts;
    BOOLEAN Serviced = FALSE;

    if (!DeviceExtension->TargetActive)
        return FALSE;
    Interrupts = IntelI2cRead32(DeviceExtension, DW_IC_RAW_INTR_STAT);
    if (Interrupts & DW_IC_INTR_TARGET_ERRORS)
    {
        TargetStatus->Errors++;
        TargetStatus->LastErrorInterrupts = Interrupts & DW_IC_INTR_TARGET_ERRORS;
        if (Interrupts & DW_IC_INTR_RX_UNDER)
            IntelI2cRead32(DeviceExtension, DW_IC_CLR_RX_UNDER);
        if (Interrupts & DW_IC_INTR_RX_OVER)
            IntelI2cRead32(DeviceExtension, DW_IC_CLR_RX_OVER);
        if (Interrupts & DW_IC_INTR_TX_OVER)
            IntelI2cRead32(DeviceExtension, DW_IC_CLR_TX_OVER);
        Serviced = TRUE;
    }
    if (Interrupts & DW_IC_INTR_TX_ABRT)
    {
        TargetStatus->LastAbortSource = IntelI2cRead32(DeviceExtension, DW_IC_TX_ABRT_SOURCE);
        IntelI2cRead32(DeviceExtension, DW_IC_CLR_TX_ABRT);
        if (!(TargetStatus->LastAbortSource & DW_IC_TX_ABRT_SLVFLUSH_TXFIFO) || (TargetStatus->LastAbortSource & (DW_IC_TX_ABRT_SLV_ARBLOST | DW_IC_TX_ABRT_SLVRD_INTX)))
            TargetStatus->Errors++;
        else
            TargetStatus->TransmitFlushes++;
        Serviced = TRUE;
    }
    if (IntelI2cDrainTarget(DeviceExtension))
        Serviced = TRUE;
    if (Interrupts & DW_IC_INTR_RX_DONE)
    {
        IntelI2cRead32(DeviceExtension, DW_IC_CLR_RX_DONE);
        TargetStatus->ReadCompletions++;
        Serviced = TRUE;
    }
    if (Interrupts & DW_IC_INTR_STOP_DET)
    {
        IntelI2cRead32(DeviceExtension, DW_IC_CLR_STOP_DET);
        IntelI2cDrainTarget(DeviceExtension);
        DeviceExtension->TargetResponseOffset = 0;
        TargetStatus->StopConditions++;
        Serviced = TRUE;
    }
    if (Interrupts & DW_IC_INTR_RD_REQ)
    {
        IntelI2cRead32(DeviceExtension, DW_IC_CLR_RD_REQ);
        TargetStatus->ReadRequests++;
        IntelI2cFeedTarget(DeviceExtension);
        Serviced = TRUE;
    }
    return Serviced;
}

static
VOID
IntelI2cRestoreMaster(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    if (!NT_SUCCESS(IntelI2cDisable(DeviceExtension, IntelI2cDeadline(100))))
        DPRINT1("INTELI2C%lu: controller did not disable after target mode\n", DeviceExtension->ControllerIndex);
    IntelI2cWrite32(DeviceExtension, DW_IC_CON, DW_IC_CON_MASTER | DW_IC_CON_RESTART_EN | DW_IC_CON_SLAVE_DISABLE | DW_IC_CON_SPEED_FAST);
    IntelI2cWrite32(DeviceExtension, DW_IC_INTR_MASK, 0);
    IntelI2cRead32(DeviceExtension, DW_IC_CLR_INTR);
}

static
NTSTATUS
IntelI2cConfigureTarget(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELI2C_TARGET_CONFIGURATION Configuration)
{
    ULONGLONG Deadline = IntelI2cDeadline(100);
    ULONG Control;
    NTSTATUS Status;

    Status = IntelI2cWaitForRegister(DeviceExtension, DW_IC_STATUS, DW_IC_STATUS_MASTER_ACTIVITY, 0, Deadline);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IntelI2cDisable(DeviceExtension, Deadline);
    if (!NT_SUCCESS(Status))
        return Status;
    Control = DW_IC_CON_SPEED_FAST | DW_IC_CON_STOP_DET_IFADDRESSED | DW_IC_CON_RX_FIFO_FULL_HLD_CTRL;
    if (Configuration->AddressMode == INTELI2C_ADDRESS_MODE_10BIT)
        Control |= DW_IC_CON_10BITADDR_SLAVE;
    IntelI2cProgramTiming(DeviceExtension, INTELI2C_DEFAULT_SPEED);
    IntelI2cWrite32(DeviceExtension, DW_IC_SAR, Configuration->TargetAddress);
    IntelI2cWrite32(DeviceExtension, DW_IC_CON, Control);
    IntelI2cWrite32(DeviceExtension, DW_IC_RX_TL, 0);
    IntelI2cWrite32(DeviceExtension, DW_IC_TX_TL, 0);
    IntelI2cWrite32(DeviceExtension, DW_IC_INTR_MASK, 0);
    IntelI2cRead32(DeviceExtension, DW_IC_CLR_INTR);
    DeviceExtension->TargetStatus.Control = IntelI2cRead32(DeviceExtension, DW_IC_CON);
    if (DeviceExtension->TargetStatus.Control & (DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE))
        return STATUS_NOT_SUPPORTED;
    IntelI2cWrite32(DeviceExtension, DW_IC_ENABLE, 1);
    return IntelI2cWaitForRegister(DeviceExtension, DW_IC_ENABLE_STATUS, 1, 1, Deadline);
}

static
VOID
NTAPI
IntelI2cTargetThread(
    _In_ PVOID Context)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension = Context;
    ULONGLONG LastActivity = KeQueryInterruptTime();
    LARGE_INTEGER Timeout;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
    for (;;)
    {
        ExAcquireFastMutex(&DeviceExtension->TransferLock);
        if (IntelI2cServiceTarget(DeviceExtension))
            LastActivity = KeQueryInterruptTime();
        ExReleaseFastMutex(&DeviceExtension->TransferLock);
        if (KeQueryInterruptTime() - LastActivity < INTELI2C_TARGET_SPIN_TIME)
        {
            KeStallExecutionProcessor(5);
            Timeout.QuadPart = 0;
        }
        else
        {
            Timeout.QuadPart = -INTELI2C_TARGET_IDLE_INTERVAL;
        }
        if (KeWaitForSingleObject(&DeviceExtension->TargetStopEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS)
            break;
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
IntelI2cTargetStop(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PFILE_OBJECT Owner,
    _Out_opt_ PINTELI2C_TARGET_STATUS FinalStatus)
{
    PKTHREAD Thread = NULL;
    NTSTATUS Status = STATUS_INVALID_DEVICE_STATE;

    KeWaitForSingleObject(&DeviceExtension->TargetLock, Executive, KernelMode, FALSE, NULL);
    if (Owner && DeviceExtension->TargetOwner != Owner)
    {
        KeReleaseMutex(&DeviceExtension->TargetLock, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    ExAcquireFastMutex(&DeviceExtension->TransferLock);
    if (DeviceExtension->TargetActive)
    {
        IntelI2cServiceTarget(DeviceExtension);
        DeviceExtension->TargetActive = FALSE;
        DeviceExtension->TargetStatus.Active = FALSE;
        IntelI2cRestoreMaster(DeviceExtension);
        Thread = DeviceExtension->TargetThread;
        DeviceExtension->TargetThread = NULL;
        DeviceExtension->TargetOwner = NULL;
        DPRINT("INTELI2C%lu: target 0x%x stopped, received=%lu reads=%lu stops=%lu errors=%lu\n", DeviceExtension->ControllerIndex, DeviceExtension->TargetStatus.TargetAddress, DeviceExtension->TargetStatus.ReceivedTotal, DeviceExtension->TargetStatus.ReadRequests, DeviceExtension->TargetStatus.StopConditions, DeviceExtension->TargetStatus.Errors);
        Status = STATUS_SUCCESS;
    }
    if (FinalStatus)
        RtlCopyMemory(FinalStatus, &DeviceExtension->TargetStatus, sizeof(*FinalStatus));
    ExReleaseFastMutex(&DeviceExtension->TransferLock);
    if (Thread)
    {
        KeSetEvent(&DeviceExtension->TargetStopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Thread);
    }
    KeReleaseMutex(&DeviceExtension->TargetLock, FALSE);
    return Status;
}

static
NTSTATUS
IntelI2cTargetStart(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELI2C_TARGET_CONFIGURATION Configuration,
    _In_opt_ PFILE_OBJECT Owner)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ThreadHandle;
    PKTHREAD Thread = NULL;
    NTSTATUS Status;

    if (Configuration->Version != INTELI2C_INTERFACE_VERSION)
        return STATUS_REVISION_MISMATCH;
    if (Configuration->ControllerIndex != DeviceExtension->ControllerIndex)
        return STATUS_NO_SUCH_DEVICE;
    if (Configuration->ResponseLength > INTELI2C_TARGET_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;
    if ((Configuration->AddressMode == INTELI2C_ADDRESS_MODE_7BIT && (Configuration->TargetAddress < 0x08 || Configuration->TargetAddress > 0x77)) || (Configuration->AddressMode == INTELI2C_ADDRESS_MODE_10BIT && Configuration->TargetAddress > 0x3ff) || Configuration->AddressMode > INTELI2C_ADDRESS_MODE_10BIT)
        return STATUS_INVALID_PARAMETER;

    KeWaitForSingleObject(&DeviceExtension->TargetLock, Executive, KernelMode, FALSE, NULL);
    ExAcquireFastMutex(&DeviceExtension->TransferLock);
    if (!DeviceExtension->Started)
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else if (DeviceExtension->TargetActive)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        RtlZeroMemory(&DeviceExtension->TargetStatus, sizeof(DeviceExtension->TargetStatus));
        DeviceExtension->TargetStatus.Version = INTELI2C_INTERFACE_VERSION;
        DeviceExtension->TargetStatus.ControllerIndex = DeviceExtension->ControllerIndex;
        DeviceExtension->TargetStatus.TargetAddress = Configuration->TargetAddress;
        DeviceExtension->TargetStatus.AddressMode = Configuration->AddressMode;
        RtlCopyMemory(DeviceExtension->TargetResponse, Configuration->Response, Configuration->ResponseLength);
        DeviceExtension->TargetResponseLength = Configuration->ResponseLength;
        DeviceExtension->TargetResponseOffset = 0;
        Status = IntelI2cConfigureTarget(DeviceExtension, Configuration);
        if (NT_SUCCESS(Status))
        {
            DeviceExtension->TargetActive = TRUE;
            DeviceExtension->TargetStatus.Active = TRUE;
        }
        else
        {
            IntelI2cRestoreMaster(DeviceExtension);
        }
    }
    ExReleaseFastMutex(&DeviceExtension->TransferLock);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&DeviceExtension->TargetLock, FALSE);
        DPRINT1("INTELI2C%lu: target mode at 0x%x failed, status=0x%08lx control=0x%08lx\n", DeviceExtension->ControllerIndex, Configuration->TargetAddress, Status, DeviceExtension->TargetStatus.Control);
        return Status;
    }

    KeClearEvent(&DeviceExtension->TargetStopEvent);
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, IntelI2cTargetThread, DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Thread, NULL);
        if (!NT_SUCCESS(Status))
            KeSetEvent(&DeviceExtension->TargetStopEvent, IO_NO_INCREMENT, FALSE);
        ZwClose(ThreadHandle);
    }
    ExAcquireFastMutex(&DeviceExtension->TransferLock);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->TargetThread = Thread;
        DeviceExtension->TargetOwner = Owner;
        DPRINT("INTELI2C%lu: target mode at 0x%x, control=0x%08lx\n", DeviceExtension->ControllerIndex, Configuration->TargetAddress, DeviceExtension->TargetStatus.Control);
    }
    else
    {
        DeviceExtension->TargetActive = FALSE;
        DeviceExtension->TargetStatus.Active = FALSE;
        IntelI2cRestoreMaster(DeviceExtension);
    }
    ExReleaseFastMutex(&DeviceExtension->TransferLock);
    KeReleaseMutex(&DeviceExtension->TargetLock, FALSE);
    return Status;
}

static
VOID
IntelI2cStopHardware(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension)
{
    IntelI2cTargetStop(DeviceExtension, NULL, NULL);
    ExAcquireFastMutex(&DeviceExtension->TransferLock);
    DeviceExtension->Started = FALSE;
    if (DeviceExtension->Registers)
    {
        IntelI2cDisable(DeviceExtension, IntelI2cDeadline(100));
        IntelI2cWrite32(DeviceExtension, LPSS_PRIV_RESETS, 0);
        MmUnmapIoSpace(DeviceExtension->Registers, DeviceExtension->RegisterLength);
        DeviceExtension->Registers = NULL;
        DeviceExtension->RegisterLength = 0;
    }
    ExReleaseFastMutex(&DeviceExtension->TransferLock);
}

static
NTSTATUS
IntelI2cStartHardware(
    _Inout_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST RawResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources)
{
    PCM_PARTIAL_RESOURCE_LIST RawList;
    PCM_PARTIAL_RESOURCE_LIST TranslatedList;
    ULONG ComponentParameter;
    ULONG Capabilities;
    ULONG Index;
    NTSTATUS Status;

    if (!TranslatedResources || !TranslatedResources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    IntelI2cStopHardware(DeviceExtension);
    TranslatedList = &TranslatedResources->List[0].PartialResourceList;
    RawList = RawResources && RawResources->Count ? &RawResources->List[0].PartialResourceList : NULL;
    for (Index = 0; Index < TranslatedList->Count; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &TranslatedList->PartialDescriptors[Index];

        if (Descriptor->Type != CmResourceTypeMemory || Descriptor->u.Memory.Length < 0x300)
            continue;
        DeviceExtension->RegisterLength = Descriptor->u.Memory.Length;
        DeviceExtension->RegisterAddress = RawList && Index < RawList->Count && RawList->PartialDescriptors[Index].Type == CmResourceTypeMemory ? RawList->PartialDescriptors[Index].u.Memory.Start : Descriptor->u.Memory.Start;
        DeviceExtension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, Descriptor->u.Memory.Length, MmNonCached);
        break;
    }
    if (!DeviceExtension->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    IntelI2cWrite32(DeviceExtension, LPSS_PRIV_RESETS, 0);
    KeStallExecutionProcessor(10);
    IntelI2cWrite32(DeviceExtension, LPSS_PRIV_RESETS, LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA);
    IntelI2cWrite32(DeviceExtension, LPSS_PRIV_REMAP_ADDR_LOW, DeviceExtension->RegisterAddress.LowPart);
    IntelI2cWrite32(DeviceExtension, LPSS_PRIV_REMAP_ADDR_HIGH, DeviceExtension->RegisterAddress.HighPart);
    Capabilities = IntelI2cRead32(DeviceExtension, LPSS_PRIV_CAPS);
    if ((Capabilities & LPSS_PRIV_CAPS_TYPE_MASK) != LPSS_PRIV_CAPS_TYPE_I2C || IntelI2cRead32(DeviceExtension, DW_IC_COMP_TYPE) != DW_IC_COMP_TYPE_VALUE)
    {
        DPRINT1("INTELI2C%lu: unsupported controller caps=0x%08lx component=0x%08lx\n", DeviceExtension->ControllerIndex, Capabilities, IntelI2cRead32(DeviceExtension, DW_IC_COMP_TYPE));
        IntelI2cStopHardware(DeviceExtension);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ComponentParameter = IntelI2cRead32(DeviceExtension, DW_IC_COMP_PARAM_1);
    DeviceExtension->TxFifoDepth = ((ComponentParameter >> 16) & 0xff) + 1;
    DeviceExtension->RxFifoDepth = ((ComponentParameter >> 8) & 0xff) + 1;
    Status = IntelI2cDisable(DeviceExtension, IntelI2cDeadline(100));
    if (!NT_SUCCESS(Status))
    {
        IntelI2cStopHardware(DeviceExtension);
        return Status;
    }
    DeviceExtension->Started = TRUE;
    DPRINT1("INTELI2C%lu: PCI device 0x%04lx BAR=%I64x TX FIFO=%lu RX FIFO=%lu\n", DeviceExtension->ControllerIndex, DeviceExtension->PciDeviceId, DeviceExtension->RegisterAddress.QuadPart, DeviceExtension->TxFifoDepth, DeviceExtension->RxFifoDepth);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelI2cCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
IntelI2cForwardSynchronously(
    _In_ PINTELI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, IntelI2cCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
NTAPI
IntelI2cCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelI2cCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);

    if (NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp)))
    {
        IntelI2cTargetStop(DeviceExtension, IrpStack->FileObject, NULL);
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelI2cDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;
    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTELI2C_QUERY_CONTROLLER:
            if (!Buffer || OutputLength < sizeof(INTELI2C_CONTROLLER_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                PINTELI2C_CONTROLLER_INFORMATION Information = Buffer;

                RtlZeroMemory(Information, sizeof(*Information));
                Information->Version = INTELI2C_INTERFACE_VERSION;
                Information->ControllerIndex = DeviceExtension->ControllerIndex;
                Information->PciDeviceId = DeviceExtension->PciDeviceId;
                Information->InputClockHz = DeviceExtension->Profile->InputClockHz;
                Information->MaximumConnectionSpeed = 1000000;
                Information->TxFifoDepth = DeviceExtension->TxFifoDepth;
                Information->RxFifoDepth = DeviceExtension->RxFifoDepth;
                Irp->IoStatus.Information = sizeof(*Information);
                Status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_INTELI2C_EXECUTE_TRANSFER:
            if (!Buffer || InputLength < FIELD_OFFSET(INTELI2C_TRANSFER_REQUEST, Transfers) + sizeof(INTELI2C_TRANSFER_ENTRY) || OutputLength < InputLength)
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Status = IntelI2cExecuteTransfer(DeviceExtension, Buffer, InputLength);
                Irp->IoStatus.Information = InputLength;
            }
            break;

        case IOCTL_INTELI2C_TARGET_START:
            if (!Buffer || InputLength < sizeof(INTELI2C_TARGET_CONFIGURATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
                Status = IntelI2cTargetStart(DeviceExtension, Buffer, IrpStack->FileObject);
            break;

        case IOCTL_INTELI2C_TARGET_STOP:
            if (OutputLength && (!Buffer || OutputLength < sizeof(INTELI2C_TARGET_STATUS)))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Status = IntelI2cTargetStop(DeviceExtension, NULL, OutputLength ? Buffer : NULL);
                if (NT_SUCCESS(Status) && OutputLength)
                    Irp->IoStatus.Information = sizeof(INTELI2C_TARGET_STATUS);
            }
            break;

        case IOCTL_INTELI2C_TARGET_QUERY:
            if (!Buffer || OutputLength < sizeof(INTELI2C_TARGET_STATUS))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                ExAcquireFastMutex(&DeviceExtension->TransferLock);
                IntelI2cServiceTarget(DeviceExtension);
                RtlCopyMemory(Buffer, &DeviceExtension->TargetStatus, sizeof(INTELI2C_TARGET_STATUS));
                ExReleaseFastMutex(&DeviceExtension->TransferLock);
                ((PINTELI2C_TARGET_STATUS)Buffer)->Version = INTELI2C_INTERFACE_VERSION;
                ((PINTELI2C_TARGET_STATUS)Buffer)->ControllerIndex = DeviceExtension->ControllerIndex;
                Irp->IoStatus.Information = sizeof(INTELI2C_TARGET_STATUS);
                Status = STATUS_SUCCESS;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
IntelI2cPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IntelI2cForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = IntelI2cStartHardware(DeviceExtension, IrpStack->Parameters.StartDevice.AllocatedResources, IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            if (NT_SUCCESS(Status))
                Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
            if (!NT_SUCCESS(Status))
                IntelI2cStopHardware(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            if (!NT_SUCCESS(IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE)))
                DPRINT1("IoSetDeviceInterfaceState(%wZ) failed\n", &DeviceExtension->InterfaceName);
            IntelI2cStopHardware(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            if (!NT_SUCCESS(IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE)))
                DPRINT1("IoSetDeviceInterfaceState(%wZ) failed\n", &DeviceExtension->InterfaceName);
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (NT_SUCCESS(Status))
                IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            IntelI2cStopHardware(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelI2cPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelI2cAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PINTELI2C_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    const INTELI2C_PCI_ID *Controller;
    NTSTATUS Status;

    Controller = IntelI2cIdentifyController(PhysicalDeviceObject);
    if (!Controller)
        return STATUS_NOT_SUPPORTED;
    Status = IoCreateDevice(DriverObject, sizeof(INTELI2C_DEVICE_EXTENSION), NULL, FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    DeviceExtension->ControllerIndex = Controller->ControllerIndex;
    DeviceExtension->PciDeviceId = Controller->DeviceId;
    DeviceExtension->Profile = Controller->Profile;
    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!DeviceExtension->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, INTELI2C_TAG, 0, 0);
    ExInitializeFastMutex(&DeviceExtension->TransferLock);
    KeInitializeMutex(&DeviceExtension->TargetLock, 0);
    KeInitializeEvent(&DeviceExtension->TargetStopEvent, NotificationEvent, FALSE);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_INTEL_I2C, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverExtension->AddDevice = IntelI2cAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = IntelI2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IntelI2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = IntelI2cCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IntelI2cDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IntelI2cPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IntelI2cPower;
    return STATUS_SUCCESS;
}
