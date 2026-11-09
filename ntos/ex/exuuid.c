/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Names that are never handed out twice: UUIDs and local ids
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LARGE_INTEGER ExpLuid = {{0x3e9, 0x0}};
LARGE_INTEGER ExpLuidIncrement = {{1, 0}};

static KSPIN_LOCK ExpLuidLock;

UUID_CACHED_VALUES_STRUCT ExpUuidCachedValues = {0};
BOOLEAN ExpUuidCacheValid = FALSE;
FAST_MUTEX ExpUuidLock;
LARGE_INTEGER ExpUuidLastTimeAllocated = {{0, 0}};
ULONG ExpUuidSequenceNumber = 0;
BOOLEAN ExpUuidSequenceNumberValid = FALSE;
BOOLEAN ExpUuidSequenceNumberNotSaved = FALSE;

/* PRIVATE DATA ***************************************************************/

/*
 * The number of hundred-nanosecond intervals between the start of the Gregorian
 * calendar, which a time-based UUID counts from, and the start of 1601, which NT
 * counts from.
 */
#define EXP_UUID_EPOCH_BIAS 0x01B21DD213814000ULL

/* How many names one reading of the clock is allowed to cover */
#define EXP_UUID_VALUES_PER_TICK 10000

/* The version and variant a time-based UUID carries */
#define EXP_UUID_VERSION_TIME  0x1000
#define EXP_UUID_VARIANT_DCE   0x80

/*
 * Where the sequence number is kept between starts. The number guards against
 * a name being made twice if the clock ever goes backwards, so it has to
 * survive a restart and has to be raised whenever it is read back.
 */
#define EXP_UUID_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Uuid"
#define EXP_UUID_SEQUENCE_VALUE L"SequenceNumber"
#define EXP_UUID_NODE_VALUE L"NodeId"

/* How far ahead the saved number is set, so a start that does not get to save
   its own number still cannot repeat a name the last one handed out */
#define EXP_UUID_SEQUENCE_LEAP 0x400

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
ExpUuidLoadNode(
    _Out_writes_bytes_(6) PUCHAR Node);

static
VOID
ExpUuidSaveNode(VOID);

/*
 * The node part of a time-based name identifies the machine. Without a network
 * address to use, a random value is drawn and its multicast bit is set, which is
 * what the specification asks of a name that is not built from a real address.
 */
static
VOID
ExpUuidSeedNode(
    _Out_writes_bytes_(6) PUCHAR NodeId)
{
    LARGE_INTEGER Counter;
    LARGE_INTEGER Time;
    ULONG Seed;

    Counter = KeQueryPerformanceCounter(NULL);
    KeQuerySystemTime(&Time);

    Seed = (ULONG)(Counter.LowPart ^ Counter.HighPart ^
                   Time.LowPart ^ Time.HighPart);

    NodeId[0] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF) | 0x01;
    NodeId[1] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF);
    NodeId[2] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF);
    NodeId[3] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF);
    NodeId[4] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF);
    NodeId[5] = (UCHAR)(RtlRandomEx(&Seed) & 0xFF);
}

/*
 * Reads the clock and works out how many names that reading may still cover.
 * A reading that has not moved on since last time leaves nothing, and the caller
 * waits for the clock.
 */
static
NTSTATUS
ExpUuidRefreshTime(VOID)
{
    LARGE_INTEGER Time;

    KeQuerySystemTime(&Time);

    if (Time.QuadPart <= ExpUuidLastTimeAllocated.QuadPart)
    {
        return STATUS_RETRY;
    }

    ExpUuidLastTimeAllocated.QuadPart = Time.QuadPart;

    ExpUuidCachedValues.Time = (ULONGLONG)Time.QuadPart + EXP_UUID_EPOCH_BIAS;
    ExpUuidCachedValues.AllocatedCount = EXP_UUID_VALUES_PER_TICK;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpUuidOpenKey(
    _Out_ PHANDLE Key,
    _In_ BOOLEAN Create)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(EXP_UUID_KEY);
    ULONG Disposition;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    if (!Create)
    {
        return ZwOpenKey(Key, KEY_READ, &ObjectAttributes);
    }

    return ZwCreateKey(Key,
                       KEY_READ | KEY_WRITE,
                       &ObjectAttributes,
                       0,
                       NULL,
                       REG_OPTION_NON_VOLATILE,
                       &Disposition);
}

static
NTSTATUS
ExpUuidReadValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    UNICODE_STRING ValueName;
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    ULONG Size = (ULONG)sizeof(KEY_VALUE_PARTIAL_INFORMATION) + Length;
    ULONG Returned;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);

    Value = ExAllocatePoolZero(PagedPool, Size, 'iuUE');
    if (Value == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(Key,
                             &ValueName,
                             KeyValuePartialInformation,
                             Value,
                             Size,
                             &Returned);
    if (NT_SUCCESS(Status))
    {
        if (Value->DataLength >= Length)
        {
            RtlCopyMemory(Buffer, Value->Data, Length);
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }
    }

    ExFreePoolWithTag(Value, 'iuUE');

    return Status;
}

static
NTSTATUS
ExpUuidWriteValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);

    return ZwSetValueKey(Key, &ValueName, 0, Type, Buffer, Length);
}

/*
 * Saves the number this start will not go past, which is further ahead than it
 * has reached. A start that ends without saving again therefore leaves a number
 * that no name it handed out can collide with.
 */
static
VOID
ExpUuidSaveSequenceNumber(VOID)
{
    HANDLE Key;
    ULONG Value = ExpUuidSequenceNumber + EXP_UUID_SEQUENCE_LEAP;

    if (!NT_SUCCESS(ExpUuidOpenKey(&Key, TRUE)))
    {
        return;
    }

    if (NT_SUCCESS(ExpUuidWriteValue(Key,
                                     EXP_UUID_SEQUENCE_VALUE,
                                     REG_DWORD,
                                     &Value,
                                     sizeof(Value))))
    {
        ExpUuidSequenceNumberNotSaved = FALSE;
    }

    ZwClose(Key);
}

/*
 * Takes up the sequence number where the last start left it. Without one to
 * take up, a number is drawn, which cannot repeat an earlier name either but
 * says nothing about what came before.
 */
static
VOID
ExpUuidLoadSequenceNumber(VOID)
{
    LARGE_INTEGER Counter;
    HANDLE Key;
    ULONG Value = 0;
    ULONG Seed;
    BOOLEAN Loaded = FALSE;

    if (NT_SUCCESS(ExpUuidOpenKey(&Key, FALSE)))
    {
        if (NT_SUCCESS(ExpUuidReadValue(Key,
                                        EXP_UUID_SEQUENCE_VALUE,
                                        &Value,
                                        sizeof(Value))))
        {
            Loaded = TRUE;
        }

        ZwClose(Key);
    }

    if (!Loaded)
    {
        Counter = KeQueryPerformanceCounter(NULL);
        Seed = Counter.LowPart ^ Counter.HighPart;
        Value = RtlRandomEx(&Seed);
    }

    ExpUuidSequenceNumber = Value & 0x3FFF;
    ExpUuidSequenceNumberValid = TRUE;
    ExpUuidSequenceNumberNotSaved = TRUE;

    ExpUuidSaveSequenceNumber();
}

/*
 * Reads what the last start left, and settles what this one will use. Doing it
 * here keeps the registry out of the path that holds the lock, because the
 * registry can only be reached with nothing raised above passive level.
 */
static
VOID
ExpUuidPrepareValues(VOID)
{
    UCHAR Node[sizeof(ExpUuidCachedValues.NodeId)];
    BOOLEAN HaveNode;

    PAGED_CODE();

    if (ExpUuidCacheValid)
    {
        return;
    }

    ExpUuidLoadSequenceNumber();

    RtlZeroMemory(Node, sizeof(Node));
    HaveNode = ExpUuidLoadNode(Node);

    if (!HaveNode)
    {
        ExpUuidSeedNode(Node);
    }

    ExAcquireFastMutex(&ExpUuidLock);

    if (!ExpUuidCacheValid)
    {
        RtlCopyMemory(ExpUuidCachedValues.NodeId, Node, sizeof(Node));

        ExpUuidCachedValues.ClockSeqHiAndReserved =
            (UCHAR)(EXP_UUID_VARIANT_DCE |
                    ((ExpUuidSequenceNumber >> 8) & 0x3F));
        ExpUuidCachedValues.ClockSeqLow =
            (UCHAR)(ExpUuidSequenceNumber & 0xFF);

        ExpUuidCachedValues.AllocatedCount = 0;
        ExpUuidCacheValid = TRUE;
    }

    ExReleaseFastMutex(&ExpUuidLock);

    if (!HaveNode)
    {
        ExpUuidSaveNode();
    }
}

/*
 * Whether a node was taken up from where the last start kept it. A node of all
 * zeroes is one that was never written.
 */
static
BOOLEAN
ExpUuidLoadNode(
    _Out_writes_bytes_(6) PUCHAR Node)
{
    HANDLE Key;
    ULONG Index;
    BOOLEAN Loaded = FALSE;

    if (!NT_SUCCESS(ExpUuidOpenKey(&Key, FALSE)))
    {
        return FALSE;
    }

    if (NT_SUCCESS(ExpUuidReadValue(Key,
                                    EXP_UUID_NODE_VALUE,
                                    Node,
                                    6)))
    {
        for (Index = 0; Index < 6; Index++)
        {
            if (Node[Index] != 0)
            {
                Loaded = TRUE;
                break;
            }
        }
    }

    ZwClose(Key);

    return Loaded;
}

static
VOID
ExpUuidSaveNode(VOID)
{
    HANDLE Key;

    if (!NT_SUCCESS(ExpUuidOpenKey(&Key, TRUE)))
    {
        return;
    }

    ExpUuidWriteValue(Key,
                      EXP_UUID_NODE_VALUE,
                      REG_BINARY,
                      ExpUuidCachedValues.NodeId,
                      sizeof(ExpUuidCachedValues.NodeId));

    ZwClose(Key);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExLuidInitialization(VOID)
{
    KeInitializeSpinLock(&ExpLuidLock);

    return TRUE;
}

/*
 * Hands out a value that no other call will hand out again for as long as the
 * system runs. Used wherever something has to be told apart from everything
 * else of its kind without a name of its own.
 */
VOID
NTAPI
ExAllocateLocallyUniqueId(
    _Out_ PLUID LocallyUniqueId)
{
    LARGE_INTEGER Luid;

    Luid = ExInterlockedAddLargeInteger(&ExpLuid,
                                        ExpLuidIncrement,
                                        &ExpLuidLock);

    LocallyUniqueId->LowPart = Luid.LowPart;
    LocallyUniqueId->HighPart = Luid.HighPart;
}

NTSTATUS
NTAPI
NtAllocateLocallyUniqueId(
    _Out_ PLUID LocallyUniqueId)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LUID Luid;

    PAGED_CODE();

    ExAllocateLocallyUniqueId(&Luid);

    if (PreviousMode == KernelMode)
    {
        *LocallyUniqueId = Luid;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWrite(LocallyUniqueId, sizeof(LUID), sizeof(ULONG));
        *LocallyUniqueId = Luid;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpUuidInitialization(VOID)
{
    ExInitializeFastMutex(&ExpUuidLock);

    ExpUuidCacheValid = FALSE;
    ExpUuidLastTimeAllocated.QuadPart = 0;
    ExpUuidSequenceNumberValid = FALSE;

    return TRUE;
}

/*
 * Builds the next time-based name. The clock reading, the count that tells names
 * made from the same reading apart, and the machine's node make up the name.
 */
NTSTATUS
NTAPI
ExUuidCreate(
    _Out_ UUID *Uuid)
{
    ULONGLONG Time;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    ExpUuidPrepareValues();

    ExAcquireFastMutex(&ExpUuidLock);

    if (ExpUuidCachedValues.AllocatedCount == 0)
    {
        Status = ExpUuidRefreshTime();
        if (!NT_SUCCESS(Status))
        {
            ExReleaseFastMutex(&ExpUuidLock);
            return Status;
        }
    }

    ExpUuidCachedValues.AllocatedCount--;
    Time = ExpUuidCachedValues.Time +
           (ULONGLONG)(EXP_UUID_VALUES_PER_TICK -
                       ExpUuidCachedValues.AllocatedCount - 1);

    Uuid->Data1 = (ULONG)Time;
    Uuid->Data2 = (USHORT)(Time >> 32);
    Uuid->Data3 = (USHORT)((USHORT)(Time >> 48) | EXP_UUID_VERSION_TIME);
    RtlCopyMemory(Uuid->Data4,
                  ExpUuidCachedValues.GuidInit,
                  sizeof(Uuid->Data4));

    ExReleaseFastMutex(&ExpUuidLock);

    /*
     * The node part is drawn rather than taken from a network address, so the
     * name is unique here but carries no claim to be unique anywhere else.
     */
    return RPC_NT_UUID_LOCAL_ONLY;
}

NTSTATUS
NTAPI
NtAllocateUuids(
    _Out_ PULARGE_INTEGER Time,
    _Out_ PULONG Range,
    _Out_ PULONG Sequence,
    _Out_ PUCHAR Seed)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULARGE_INTEGER SafeTime;
    ULONG SafeRange;
    ULONG SafeSequence;
    UCHAR SafeSeed[6];
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlonglong((PULONGLONG)Time);
            ProbeForWriteUlong(Range);
            ProbeForWriteUlong(Sequence);
            ProbeForWrite(Seed, sizeof(SafeSeed), sizeof(UCHAR));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    ExpUuidPrepareValues();

    ExAcquireFastMutex(&ExpUuidLock);

    Status = ExpUuidRefreshTime();
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&ExpUuidLock);
        return Status;
    }

    SafeTime.QuadPart = ExpUuidCachedValues.Time;
    SafeRange = EXP_UUID_VALUES_PER_TICK;
    SafeSequence = ExpUuidSequenceNumber;
    RtlCopyMemory(SafeSeed, ExpUuidCachedValues.NodeId, sizeof(SafeSeed));

    ExpUuidCachedValues.AllocatedCount = 0;

    ExReleaseFastMutex(&ExpUuidLock);

    _SEH2_TRY
    {
        *Time = SafeTime;
        *Range = SafeRange;
        *Sequence = SafeSequence;
        RtlCopyMemory(Seed, SafeSeed, sizeof(SafeSeed));
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSetUuidSeed(
    _In_ PUCHAR Seed)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UCHAR SafeSeed[6];

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Seed, sizeof(SafeSeed), sizeof(UCHAR));
            RtlCopyMemory(SafeSeed, Seed, sizeof(SafeSeed));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(SafeSeed, Seed, sizeof(SafeSeed));
    }

    ExAcquireFastMutex(&ExpUuidLock);

    RtlCopyMemory(ExpUuidCachedValues.NodeId, SafeSeed, sizeof(SafeSeed));
    ExpUuidCacheValid = TRUE;

    ExReleaseFastMutex(&ExpUuidLock);

    return STATUS_SUCCESS;
}

/* EOF */
