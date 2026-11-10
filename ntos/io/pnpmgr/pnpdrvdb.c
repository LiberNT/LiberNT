/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Configuring a new device from the driver database
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

static
NTSTATUS
IopOpenInstanceKeyByPath(
    _In_ PUNICODE_STRING InstancePath,
    _Out_ PHANDLE InstanceKey)
{
    static const UNICODE_STRING EnumRoot = RTL_CONSTANT_STRING(ENUM_ROOT);
    UNICODE_STRING KeyPath;
    NTSTATUS Status;
    USHORT Length;

    Length = EnumRoot.Length + sizeof(WCHAR) + InstancePath->Length + sizeof(WCHAR);

    KeyPath.Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (KeyPath.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeyPath.Length = 0;
    KeyPath.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&KeyPath, (PUNICODE_STRING)&EnumRoot);
    RtlAppendUnicodeToString(&KeyPath, L"\\");
    RtlAppendUnicodeStringToString(&KeyPath, InstancePath);

    Status = IopCreateDeviceKeyPath(&KeyPath, REG_OPTION_NON_VOLATILE, InstanceKey);

    ExFreePoolWithTag(KeyPath.Buffer, TAG_IO);
    return Status;
}

static
VOID
IopEnsureDeviceDriverKey(
    _In_ HANDLE InstanceKey)
{
    UNICODE_STRING DriverU = RTL_CONSTANT_STRING(L"Driver");
    UNICODE_STRING ClassGuidU = RTL_CONSTANT_STRING(L"ClassGUID");
    UNICODE_STRING ClassPrefixU = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\");
    PKEY_VALUE_PARTIAL_INFORMATION PartialInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE ClassKey = NULL, InstKey = NULL;
    PWSTR ClassGuidStr, PathBuffer, DriverValue;
    WCHAR InstanceName[8];
    ULONG Length = 0, Instance, Disposition, GuidBytes, GuidChars;
    NTSTATUS Status;

    /* Idempotency: leave an already-installed driver key untouched */
    Status = ZwQueryValueKey(InstanceKey, &DriverU, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
        return;

    /* Read the device's ClassGUID */
    Length = 0;
    Status = ZwQueryValueKey(InstanceKey, &ClassGuidU, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
        return;
    PartialInfo = ExAllocatePool(PagedPool, Length);
    if (!PartialInfo)
        return;
    Status = ZwQueryValueKey(InstanceKey, &ClassGuidU, KeyValuePartialInformation, PartialInfo, Length, &Length);
    if (!NT_SUCCESS(Status) || PartialInfo->Type != REG_SZ ||
        PartialInfo->DataLength < 2 * sizeof(WCHAR))
    {
        ExFreePool(PartialInfo);
        return;
    }
    ClassGuidStr = (PWSTR)PartialInfo->Data;
    GuidChars = PartialInfo->DataLength / sizeof(WCHAR);
    if (ClassGuidStr[GuidChars - 1] != UNICODE_NULL)
    {
        ExFreePool(PartialInfo);
        return;
    }
    GuidBytes = PartialInfo->DataLength - sizeof(WCHAR); /* without terminating NUL */

    /* Open (create) Control\Class\{ClassGUID} */
    PathBuffer = ExAllocatePool(PagedPool, ClassPrefixU.Length + PartialInfo->DataLength);
    if (!PathBuffer)
    {
        ExFreePool(PartialInfo);
        return;
    }
    RtlCopyMemory(PathBuffer, ClassPrefixU.Buffer, ClassPrefixU.Length);
    RtlCopyMemory((PUCHAR)PathBuffer + ClassPrefixU.Length, ClassGuidStr, PartialInfo->DataLength);
    RtlInitUnicodeString(&KeyName, PathBuffer);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&ClassKey, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL,
                         REG_OPTION_NON_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(PathBuffer);
        ExFreePool(PartialInfo);
        return;
    }

    /* Allocate the first free NNNN instance subkey under the class */
    for (Instance = 0; Instance < 1000; Instance++)
    {
        InstanceName[0] = L'0' + (WCHAR)((Instance / 1000) % 10);
        InstanceName[1] = L'0' + (WCHAR)((Instance / 100) % 10);
        InstanceName[2] = L'0' + (WCHAR)((Instance / 10) % 10);
        InstanceName[3] = L'0' + (WCHAR)(Instance % 10);
        InstanceName[4] = UNICODE_NULL;
        RtlInitUnicodeString(&KeyName, InstanceName);
        InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, ClassKey, NULL);
        Status = ZwCreateKey(&InstKey, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL,
                             REG_OPTION_NON_VOLATILE, &Disposition);
        if (!NT_SUCCESS(Status))
        {
            InstKey = NULL;
            break;
        }
        if (Disposition == REG_CREATED_NEW_KEY)
            break;
        ZwClose(InstKey);
        InstKey = NULL;
    }
    ZwClose(ClassKey);
    if (InstKey == NULL)
    {
        ExFreePool(PathBuffer);
        ExFreePool(PartialInfo);
        return;
    }
    ZwClose(InstKey);

    /* Record "Driver" = "{ClassGUID}\NNNN" on the device instance */
    DriverValue = ExAllocatePool(PagedPool, GuidBytes + 6 * sizeof(WCHAR));
    if (DriverValue)
    {
        RtlCopyMemory(DriverValue, ClassGuidStr, GuidBytes);
        DriverValue[GuidBytes / sizeof(WCHAR)] = L'\\';
        RtlCopyMemory((PUCHAR)DriverValue + GuidBytes + sizeof(WCHAR), InstanceName, 4 * sizeof(WCHAR));
        DriverValue[GuidBytes / sizeof(WCHAR) + 5] = UNICODE_NULL;
        Status = ZwSetValueKey(InstanceKey, &DriverU, 0, REG_SZ, DriverValue,
                               (GuidBytes / sizeof(WCHAR) + 6) * sizeof(WCHAR));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Could not set Driver='%S' (0x%08lX)\n",
                    DriverValue, Status);
        }
        ExFreePool(DriverValue);
    }

    ExFreePool(PathBuffer);
    ExFreePool(PartialInfo);
}

typedef struct _IOP_DRIVER_DATABASE_MATCH
{
    BOOLEAN Found;
    ULONG Rank;
    LARGE_INTEGER DriverDate;
    ULONGLONG DriverVersion;
    GUID ClassGuid;
    WCHAR DeviceId[200];
    WCHAR InfName[260];
    WCHAR PackageId[260];
} IOP_DRIVER_DATABASE_MATCH, *PIOP_DRIVER_DATABASE_MATCH;

typedef struct _IOP_DRIVER_DATABASE_CONFIGURATION
{
    WCHAR Configuration[260];
    WCHAR Service[260];
    WCHAR Description[512];
    WCHAR Manufacturer[512];
    WCHAR Provider[260];
    WCHAR MatchingDeviceId[200];
    WCHAR Token[260];
    WCHAR Text[64];
} IOP_DRIVER_DATABASE_CONFIGURATION, *PIOP_DRIVER_DATABASE_CONFIGURATION;

static
NTSTATUS
IopOpenDriverDatabaseKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE ParentKey,
    _In_ PCWSTR Format,
    _In_ PCWSTR Argument,
    _In_ ACCESS_MASK DesiredAccess)
{
    UNICODE_STRING KeyName;
    WCHAR Buffer[512];
    NTSTATUS Status;

    Status = RtlStringCbPrintfW(Buffer, sizeof(Buffer), Format, Argument);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, Buffer);
    return IopOpenRegistryKeyEx(KeyHandle, ParentKey, &KeyName, DesiredAccess);
}

static
BOOLEAN
IopGetDriverDatabaseString(
    _In_ HANDLE Key,
    _In_ PWSTR ValueName,
    _Out_writes_(BufferLength) PWSTR Buffer,
    _In_ ULONG BufferLength)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    ULONG Length;

    if (!NT_SUCCESS(IopGetRegistryValue(Key, ValueName, &Information)))
        return FALSE;

    Length = Information->DataLength / sizeof(WCHAR);
    if (Information->Type != REG_SZ || (Information->DataLength % sizeof(WCHAR)) != 0 ||
        Length < 2 || Length > BufferLength)
    {
        ExFreePool(Information);
        return FALSE;
    }

    RtlCopyMemory(Buffer, (PUCHAR)Information + Information->DataOffset, Information->DataLength);
    Buffer[Length - 1] = UNICODE_NULL;
    ExFreePool(Information);
    return TRUE;
}

static
VOID
IopSetDriverDatabaseValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_reads_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);
    ZwSetValueKey(Key, &Name, 0, Type, Data, DataLength);
}

static
VOID
IopSetDriverDatabaseString(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ PCWSTR Value)
{
    IopSetDriverDatabaseValue(Key, ValueName, REG_SZ, (PVOID)Value, ((ULONG)wcslen(Value) + 1) * sizeof(WCHAR));
}

static
VOID
IopCopyRegistryTree(
    _In_ HANDLE Source,
    _In_ HANDLE Destination,
    _In_ ULONG Depth)
{
    PKEY_VALUE_FULL_INFORMATION ValueInformation;
    PKEY_BASIC_INFORMATION KeyInformation;
    UNICODE_STRING Name;
    HANDLE SourceChild, DestinationChild;
    ULONG Index, Length;
    NTSTATUS Status;

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateValueKey(Source, Index, KeyValueFullInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        ValueInformation = ExAllocatePool(PagedPool, Length);
        if (!ValueInformation)
            break;
        if (NT_SUCCESS(ZwEnumerateValueKey(Source, Index, KeyValueFullInformation, ValueInformation, Length, &Length)))
        {
            Name.Buffer = ValueInformation->Name;
            Name.Length = Name.MaximumLength = (USHORT)ValueInformation->NameLength;
            ZwSetValueKey(Destination,
                          &Name,
                          0,
                          ValueInformation->Type,
                          (PUCHAR)ValueInformation + ValueInformation->DataOffset,
                          ValueInformation->DataLength);
        }
        ExFreePool(ValueInformation);
    }

    if (Depth >= 16)
        return;

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(Source, Index, KeyBasicInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        KeyInformation = ExAllocatePool(PagedPool, Length);
        if (!KeyInformation)
            break;
        if (NT_SUCCESS(ZwEnumerateKey(Source, Index, KeyBasicInformation, KeyInformation, Length, &Length)))
        {
            Name.Buffer = KeyInformation->Name;
            Name.Length = Name.MaximumLength = (USHORT)KeyInformation->NameLength;
            if (NT_SUCCESS(IopOpenRegistryKeyEx(&SourceChild, Source, &Name, KEY_READ)))
            {
                if (NT_SUCCESS(IopCreateRegistryKeyEx(&DestinationChild, Destination, &Name,
                                                      KEY_ALL_ACCESS, REG_OPTION_NON_VOLATILE, NULL)))
                {
                    IopCopyRegistryTree(SourceChild, DestinationChild, Depth + 1);
                    ZwClose(DestinationChild);
                }
                ZwClose(SourceChild);
            }
        }
        ExFreePool(KeyInformation);
    }
}

static
BOOLEAN
IopGetDriverDatabaseVersion(
    _In_ HANDLE Package,
    _Out_ PIOP_DRIVER_DATABASE_MATCH Match)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    PUCHAR Data;

    if (!NT_SUCCESS(IopGetRegistryValue(Package, L"Version", &Information)))
        return FALSE;

    if (Information->Type != REG_BINARY || Information->DataLength < 40)
    {
        ExFreePool(Information);
        return FALSE;
    }

    Data = (PUCHAR)Information + Information->DataOffset;
    RtlCopyMemory(&Match->ClassGuid, Data + 8, sizeof(Match->ClassGuid));
    RtlCopyMemory(&Match->DriverDate.QuadPart, Data + 24, sizeof(Match->DriverDate.QuadPart));
    RtlCopyMemory(&Match->DriverVersion, Data + 32, sizeof(Match->DriverVersion));
    ExFreePool(Information);
    return TRUE;
}

static
ULONG
IopGetDriverDatabaseSignatureScore(
    _In_ HANDLE Package)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    ULONG SignerScore = 0xFF000000;

    if (NT_SUCCESS(IopGetRegistryValue(Package, L"SignerScore", &Information)))
    {
        if (Information->Type == REG_DWORD && Information->DataLength == sizeof(ULONG))
            SignerScore = *(PULONG)((PUCHAR)Information + Information->DataOffset);
        ExFreePool(Information);
    }

    return (SignerScore & 0xF0000000) ? (SignerScore & 0xFF000000) : 0;
}

static
VOID
IopRankDriverDatabaseId(
    _In_ HANDLE Database,
    _In_ PCWSTR DeviceId,
    _In_ BOOLEAN Compatible,
    _In_ ULONG Position,
    _Inout_ PIOP_DRIVER_DATABASE_MATCH Best,
    _Inout_ PIOP_DRIVER_DATABASE_MATCH Candidate)
{
    PKEY_VALUE_FULL_INFORMATION Information;
    HANDLE DeviceIds, InfFile, Package;
    ULONG Index, Length, Identifier, InfPosition;
    UCHAR Flags, FeatureScore;
    PUCHAR Data;
    NTSTATUS Status;

    if (!NT_SUCCESS(IopOpenDriverDatabaseKey(&DeviceIds, Database, L"DeviceIds\\%s", DeviceId, KEY_READ)))
        return;

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateValueKey(DeviceIds, Index, KeyValueFullInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        Information = ExAllocatePool(PagedPool, Length);
        if (!Information)
            break;
        Status = ZwEnumerateValueKey(DeviceIds, Index, KeyValueFullInformation, Information, Length, &Length);
        if (!NT_SUCCESS(Status) || Information->Type != REG_BINARY || Information->DataLength < 4 ||
            Information->NameLength >= sizeof(Candidate->InfName))
        {
            ExFreePool(Information);
            continue;
        }

        Data = (PUCHAR)Information + Information->DataOffset;
        Flags = Data[0];
        FeatureScore = Data[1];
        InfPosition = Data[2] | ((ULONG)Data[3] << 8);
        RtlCopyMemory(Candidate->InfName, Information->Name, Information->NameLength);
        Candidate->InfName[Information->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        ExFreePool(Information);

        if (!Compatible && (Flags & 1))
            Identifier = Position;
        else if (!Compatible && (Flags & 2))
            Identifier = 0x1000 + Position;
        else if (Compatible && (Flags & 1))
            Identifier = 0x2000 + Position;
        else if (Compatible && (Flags & 2))
            Identifier = 0x3000 + min(Position + InfPosition * 0x100, 0xFFF);
        else
            continue;

        if (!NT_SUCCESS(IopOpenDriverDatabaseKey(&InfFile, Database, L"DriverInfFiles\\%s", Candidate->InfName, KEY_READ)))
            continue;
        if (!IopGetDriverDatabaseString(InfFile, L"Active", Candidate->PackageId, RTL_NUMBER_OF(Candidate->PackageId)))
        {
            ZwClose(InfFile);
            continue;
        }
        ZwClose(InfFile);

        if (!NT_SUCCESS(IopOpenDriverDatabaseKey(&Package, Database, L"DriverPackages\\%s", Candidate->PackageId, KEY_READ)))
            continue;
        if (!IopGetDriverDatabaseVersion(Package, Candidate))
        {
            ZwClose(Package);
            continue;
        }
        Candidate->Rank = IopGetDriverDatabaseSignatureScore(Package) | ((ULONG)FeatureScore << 16) | Identifier;
        ZwClose(Package);

        if (!Best->Found ||
            Candidate->Rank < Best->Rank ||
            (Candidate->Rank == Best->Rank &&
             (Candidate->DriverDate.QuadPart > Best->DriverDate.QuadPart ||
              (Candidate->DriverDate.QuadPart == Best->DriverDate.QuadPart &&
               Candidate->DriverVersion > Best->DriverVersion))))
        {
            Candidate->Found = TRUE;
            RtlStringCbCopyW(Candidate->DeviceId, sizeof(Candidate->DeviceId), DeviceId);
            RtlCopyMemory(Best, Candidate, sizeof(*Best));
        }
    }

    ZwClose(DeviceIds);
}

static
VOID
IopGetDriverDatabaseText(
    _In_ HANDLE Strings,
    _In_ HANDLE Descriptor,
    _In_ PWSTR ValueName,
    _Out_writes_(BufferLength) PWSTR Buffer,
    _In_ ULONG BufferLength,
    _Out_writes_(TokenLength) PWSTR Token,
    _In_ ULONG TokenLength)
{
    SIZE_T Length;

    Buffer[0] = UNICODE_NULL;
    if (!IopGetDriverDatabaseString(Descriptor, ValueName, Token, TokenLength))
        return;

    Length = wcslen(Token);
    if (Length > 2 && Token[0] == L'%' && Token[Length - 1] == L'%')
    {
        Token[Length - 1] = UNICODE_NULL;
        if (!Strings || !IopGetDriverDatabaseString(Strings, Token + 1, Buffer, BufferLength))
            Buffer[0] = UNICODE_NULL;
        return;
    }

    RtlStringCchCopyW(Buffer, BufferLength, Token);
}

static
VOID
IopGetDriverDatabaseMatchingId(
    _In_ HANDLE Descriptor,
    _In_ PCWSTR DeviceId,
    _Out_writes_(BufferLength) PWSTR Buffer,
    _In_ ULONG BufferLength)
{
    static const WCHAR Separator[] = L"\\Descriptors\\";
    PKEY_NAME_INFORMATION Information;
    ULONG Length, Count, Index, SeparatorLength = RTL_NUMBER_OF(Separator) - 1;
    NTSTATUS Status;

    RtlStringCchCopyW(Buffer, BufferLength, DeviceId);

    Status = ZwQueryKey(Descriptor, KeyNameInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
        return;
    Information = ExAllocatePool(PagedPool, Length);
    if (!Information)
        return;

    if (NT_SUCCESS(ZwQueryKey(Descriptor, KeyNameInformation, Information, Length, &Length)))
    {
        Count = Information->NameLength / sizeof(WCHAR);
        for (Index = 0; Index + SeparatorLength < Count; Index++)
        {
            if (RtlCompareMemory(&Information->Name[Index], Separator, SeparatorLength * sizeof(WCHAR)) ==
                SeparatorLength * sizeof(WCHAR))
            {
                Index += SeparatorLength;
                if (Count - Index < BufferLength)
                {
                    RtlCopyMemory(Buffer, &Information->Name[Index], (Count - Index) * sizeof(WCHAR));
                    Buffer[Count - Index] = UNICODE_NULL;
                }
                break;
            }
        }
    }

    ExFreePool(Information);
}

static
VOID
IopApplyDriverDatabaseServices(
    _In_ HANDLE Configuration,
    _Inout_ PIOP_DRIVER_DATABASE_CONFIGURATION Config)
{
    UNICODE_STRING ServicesU = RTL_CONSTANT_STRING(L"Services");
    PKEY_BASIC_INFORMATION Information;
    UNICODE_STRING Name;
    HANDLE Services, Source, Destination;
    ULONG Index, Length;
    NTSTATUS Status;

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Services, Configuration, &ServicesU, KEY_READ)))
        return;

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(Services, Index, KeyBasicInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        Information = ExAllocatePool(PagedPool, Length);
        if (!Information)
            break;
        Status = ZwEnumerateKey(Services, Index, KeyBasicInformation, Information, Length, &Length);
        if (!NT_SUCCESS(Status) || Information->NameLength >= sizeof(Config->Token))
        {
            ExFreePool(Information);
            continue;
        }
        RtlCopyMemory(Config->Token, Information->Name, Information->NameLength);
        Config->Token[Information->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        ExFreePool(Information);

        RtlInitUnicodeString(&Name, Config->Token);
        if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Source, Services, &Name, KEY_READ)))
            continue;
        if (NT_SUCCESS(IopOpenDriverDatabaseKey(&Destination,
                                                NULL,
                                                L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",
                                                Config->Token,
                                                KEY_ALL_ACCESS)))
        {
            IopCopyRegistryTree(Source, Destination, 0);
            ZwClose(Destination);
        }
        ZwClose(Source);
    }

    ZwClose(Services);
}

static
VOID
IopApplyDriverDatabaseDriverKey(
    _In_ HANDLE InstanceKey,
    _In_ HANDLE Configuration,
    _In_ PIOP_DRIVER_DATABASE_MATCH Match,
    _Inout_ PIOP_DRIVER_DATABASE_CONFIGURATION Config)
{
    UNICODE_STRING DriverU = RTL_CONSTANT_STRING(L"Driver");
    PKEY_VALUE_FULL_INFORMATION Included;
    TIME_FIELDS TimeFields;
    HANDLE DriverKey, Source;

    IopEnsureDeviceDriverKey(InstanceKey);
    if (!IopGetDriverDatabaseString(InstanceKey, L"Driver", Config->Token, RTL_NUMBER_OF(Config->Token)))
        return;
    if (!NT_SUCCESS(IopOpenDriverDatabaseKey(&DriverKey,
                                            NULL,
                                            L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\%s",
                                            Config->Token,
                                            KEY_ALL_ACCESS)))
    {
        return;
    }

    if (Config->Description[0])
        IopSetDriverDatabaseString(DriverKey, L"DriverDesc", Config->Description);
    if (Config->Provider[0])
        IopSetDriverDatabaseString(DriverKey, L"ProviderName", Config->Provider);
    IopSetDriverDatabaseValue(DriverKey, L"DriverDateData", REG_BINARY,
                              &Match->DriverDate.QuadPart, sizeof(Match->DriverDate.QuadPart));
    RtlTimeToTimeFields(&Match->DriverDate, &TimeFields);
    if (NT_SUCCESS(RtlStringCbPrintfW(Config->Text, sizeof(Config->Text), L"%u-%u-%u",
                                      TimeFields.Month, TimeFields.Day, TimeFields.Year)))
    {
        IopSetDriverDatabaseString(DriverKey, L"DriverDate", Config->Text);
    }
    if (NT_SUCCESS(RtlStringCbPrintfW(Config->Text, sizeof(Config->Text), L"%u.%u.%u.%u",
                                      (ULONG)(Match->DriverVersion >> 48) & 0xFFFF,
                                      (ULONG)(Match->DriverVersion >> 32) & 0xFFFF,
                                      (ULONG)(Match->DriverVersion >> 16) & 0xFFFF,
                                      (ULONG)Match->DriverVersion & 0xFFFF)))
    {
        IopSetDriverDatabaseString(DriverKey, L"DriverVersion", Config->Text);
    }
    IopSetDriverDatabaseString(DriverKey, L"InfPath", Match->InfName);
    IopSetDriverDatabaseString(DriverKey, L"InfSection", Config->Configuration);
    if (NT_SUCCESS(IopGetRegistryValue(Configuration, L"IncludedInfs", &Included)))
    {
        if (Included->Type == REG_MULTI_SZ)
        {
            IopSetDriverDatabaseValue(DriverKey, L"IncludedInfs", REG_MULTI_SZ,
                                      (PUCHAR)Included + Included->DataOffset, Included->DataLength);
        }
        ExFreePool(Included);
    }
    IopSetDriverDatabaseString(DriverKey, L"MatchingDeviceId", Config->MatchingDeviceId);

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&Source, Configuration, &DriverU, KEY_READ)))
    {
        IopCopyRegistryTree(Source, DriverKey, 0);
        ZwClose(Source);
    }

    ZwClose(DriverKey);
}

static
BOOLEAN
IopMultiSzContains(
    _In_opt_ PKEY_VALUE_FULL_INFORMATION Information,
    _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING Entry;
    PCWSTR Current, End;
    SIZE_T Length;

    if (!Information || Information->Type != REG_MULTI_SZ)
        return FALSE;

    Current = (PCWSTR)((PUCHAR)Information + Information->DataOffset);
    End = Current + Information->DataLength / sizeof(WCHAR);
    while (Current < End && *Current)
    {
        Length = wcsnlen(Current, End - Current);
        if (Length * sizeof(WCHAR) <= MAXUSHORT)
        {
            Entry.Buffer = (PWSTR)Current;
            Entry.Length = Entry.MaximumLength = (USHORT)(Length * sizeof(WCHAR));
            if (RtlEqualUnicodeString(&Entry, Name, TRUE))
                return TRUE;
        }
        Current += Length + 1;
    }
    return FALSE;
}

static
VOID
IopAppendDriverDatabaseFilter(
    _In_ HANDLE InstanceKey,
    _In_ PWSTR ValueName,
    _In_ PCUNICODE_STRING Filter)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    UNICODE_STRING NameU;
    PUCHAR Buffer;
    ULONG Used = 0, Size;

    if (NT_SUCCESS(IopGetRegistryValue(InstanceKey, ValueName, &Information)))
    {
        if (IopMultiSzContains(Information, Filter))
        {
            ExFreePool(Information);
            return;
        }
        if (Information->Type == REG_MULTI_SZ && Information->DataLength >= sizeof(WCHAR) &&
            (Information->DataLength % sizeof(WCHAR)) == 0)
        {
            Used = Information->DataLength - sizeof(WCHAR);
            while (Used >= sizeof(WCHAR) &&
                   *(PWCHAR)((PUCHAR)Information + Information->DataOffset + Used - sizeof(WCHAR)) == UNICODE_NULL)
            {
                Used -= sizeof(WCHAR);
            }
            if (Used)
                Used += sizeof(WCHAR);
        }
    }

    Size = Used + Filter->Length + 2 * sizeof(WCHAR);
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_IO);
    if (Buffer)
    {
        if (Used)
            RtlCopyMemory(Buffer, (PUCHAR)Information + Information->DataOffset, Used);
        RtlCopyMemory(Buffer + Used, Filter->Buffer, Filter->Length);
        *(PWCHAR)(Buffer + Used + Filter->Length) = UNICODE_NULL;
        *(PWCHAR)(Buffer + Used + Filter->Length + sizeof(WCHAR)) = UNICODE_NULL;
        RtlInitUnicodeString(&NameU, ValueName);
        ZwSetValueKey(InstanceKey, &NameU, 0, REG_MULTI_SZ, Buffer, Size);
        ExFreePoolWithTag(Buffer, TAG_IO);
    }

    if (Information)
        ExFreePool(Information);
}

static
VOID
IopApplyDriverDatabaseFilters(
    _In_ HANDLE InstanceKey,
    _In_ HANDLE Configuration)
{
    UNICODE_STRING FiltersU = RTL_CONSTANT_STRING(L"Filters");
    UNICODE_STRING DeviceU = RTL_CONSTANT_STRING(L"Device");
    UNICODE_STRING UpperU = RTL_CONSTANT_STRING(L"*Upper");
    UNICODE_STRING LowerU = RTL_CONSTANT_STRING(L"*Lower");
    PKEY_VALUE_FULL_INFORMATION UpperLevels = NULL, LowerLevels = NULL;
    PKEY_VALUE_BASIC_INFORMATION ValueInformation;
    PKEY_BASIC_INFORMATION KeyInformation;
    UNICODE_STRING Level, Filter;
    HANDLE Filters, Device, LevelKey;
    ULONG Index, ValueIndex, Length;
    PWSTR Target;
    NTSTATUS Status;

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Filters, Configuration, &FiltersU, KEY_READ)))
        return;

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&Device, Configuration, &DeviceU, KEY_READ)))
    {
        if (!NT_SUCCESS(IopGetRegistryValue(Device, L"UpperFilterLevels", &UpperLevels)))
            UpperLevels = NULL;
        if (!NT_SUCCESS(IopGetRegistryValue(Device, L"LowerFilterLevels", &LowerLevels)))
            LowerLevels = NULL;
        ZwClose(Device);
    }

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(Filters, Index, KeyBasicInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        KeyInformation = ExAllocatePool(PagedPool, Length);
        if (!KeyInformation)
            break;
        if (!NT_SUCCESS(ZwEnumerateKey(Filters, Index, KeyBasicInformation, KeyInformation, Length, &Length)))
        {
            ExFreePool(KeyInformation);
            continue;
        }

        Level.Buffer = KeyInformation->Name;
        Level.Length = Level.MaximumLength = (USHORT)KeyInformation->NameLength;
        if (RtlEqualUnicodeString(&Level, &UpperU, TRUE) || IopMultiSzContains(UpperLevels, &Level))
            Target = L"UpperFilters";
        else if (RtlEqualUnicodeString(&Level, &LowerU, TRUE) || IopMultiSzContains(LowerLevels, &Level))
            Target = L"LowerFilters";
        else
            Target = NULL;

        if (Target && NT_SUCCESS(IopOpenRegistryKeyEx(&LevelKey, Filters, &Level, KEY_READ)))
        {
            for (ValueIndex = 0; ; ValueIndex++)
            {
                Status = ZwEnumerateValueKey(LevelKey, ValueIndex, KeyValueBasicInformation, NULL, 0, &Length);
                if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
                    break;
                ValueInformation = ExAllocatePool(PagedPool, Length);
                if (!ValueInformation)
                    break;
                if (NT_SUCCESS(ZwEnumerateValueKey(LevelKey, ValueIndex, KeyValueBasicInformation,
                                                   ValueInformation, Length, &Length)) &&
                    ValueInformation->NameLength)
                {
                    Filter.Buffer = ValueInformation->Name;
                    Filter.Length = Filter.MaximumLength = (USHORT)ValueInformation->NameLength;
                    IopAppendDriverDatabaseFilter(InstanceKey, Target, &Filter);
                }
                ExFreePool(ValueInformation);
            }
            ZwClose(LevelKey);
        }
        ExFreePool(KeyInformation);
    }

    if (UpperLevels)
        ExFreePool(UpperLevels);
    if (LowerLevels)
        ExFreePool(LowerLevels);
    ZwClose(Filters);
}

static
VOID
IopApplyDriverDatabaseInterface(
    _In_ PCUNICODE_STRING InstancePath,
    _In_ HANDLE Interface,
    _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING ClassesU = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\DeviceClasses");
    UNICODE_STRING FriendlyNameKeyU = RTL_CONSTANT_STRING(L"Properties\\{026e516e-b814-414b-83cd-856d6fef4822}\\0002");
    UNICODE_STRING DeviceU = RTL_CONSTANT_STRING(L"Device");
    UNICODE_STRING ParametersU = RTL_CONSTANT_STRING(L"Device Parameters");
    UNICODE_STRING FriendlyNameU = RTL_CONSTANT_STRING(L"FriendlyName");
    UNICODE_STRING EmptyU = RTL_CONSTANT_STRING(L"");
    UNICODE_STRING Guid, Reference, KeyName;
    PKEY_VALUE_FULL_INFORMATION Value;
    HANDLE Classes, Class = NULL, Key = NULL, ReferenceKey = NULL, Parameters = NULL, Source;
    USHORT Index, Length;
    NTSTATUS Status;

    Guid.Buffer = Name->Buffer;
    Guid.Length = Guid.MaximumLength = 38 * sizeof(WCHAR);
    if (Name->Length < Guid.Length || Name->Buffer[0] != L'{' || Name->Buffer[37] != L'}')
        return;
    Reference.Buffer = Name->Buffer + 38;
    Reference.Length = Reference.MaximumLength = Name->Length - Guid.Length;
    if (Reference.Length)
    {
        if (Reference.Buffer[0] != L'#')
            return;
        Reference.Buffer++;
        Reference.Length -= sizeof(WCHAR);
        Reference.MaximumLength = Reference.Length;
    }

    Length = (USHORT)(4 * sizeof(WCHAR) + InstancePath->Length + sizeof(WCHAR) + Guid.Length);
    if (Length < Guid.Length + sizeof(WCHAR) + Reference.Length)
        Length = Guid.Length + sizeof(WCHAR) + Reference.Length;
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO);
    if (!KeyName.Buffer)
        return;
    KeyName.Length = 0;
    KeyName.MaximumLength = Length;

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Classes, NULL, &ClassesU, KEY_CREATE_SUB_KEY)))
        goto Cleanup;
    Status = IopCreateRegistryKeyEx(&Class, Classes, &Guid, KEY_CREATE_SUB_KEY, REG_OPTION_NON_VOLATILE, NULL);
    ZwClose(Classes);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RtlAppendUnicodeToString(&KeyName, L"##?#");
    Index = KeyName.Length / sizeof(WCHAR);
    RtlAppendUnicodeStringToString(&KeyName, InstancePath);
    for (; Index < KeyName.Length / sizeof(WCHAR); Index++)
    {
        if (KeyName.Buffer[Index] == L'\\')
            KeyName.Buffer[Index] = L'#';
    }
    RtlAppendUnicodeToString(&KeyName, L"#");
    RtlAppendUnicodeStringToString(&KeyName, &Guid);
    if (!NT_SUCCESS(IopCreateRegistryKeyEx(&Key, Class, &KeyName, KEY_CREATE_SUB_KEY, REG_OPTION_NON_VOLATILE, NULL)))
        goto Cleanup;

    KeyName.Length = 0;
    RtlAppendUnicodeToString(&KeyName, L"#");
    RtlAppendUnicodeStringToString(&KeyName, &Reference);
    if (!NT_SUCCESS(IopCreateRegistryKeyEx(&ReferenceKey, Key, &KeyName, KEY_CREATE_SUB_KEY, REG_OPTION_NON_VOLATILE, NULL)) ||
        !NT_SUCCESS(IopCreateRegistryKeyEx(&Parameters, ReferenceKey, &ParametersU, KEY_ALL_ACCESS, REG_OPTION_NON_VOLATILE, NULL)))
    {
        goto Cleanup;
    }

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&Source, Interface, &DeviceU, KEY_READ)))
    {
        IopCopyRegistryTree(Source, Parameters, 0);
        ZwClose(Source);
    }

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&Source, Interface, &FriendlyNameKeyU, KEY_READ)))
    {
        Status = IopGetRegistryValue(Source, EmptyU.Buffer, &Value);
        if (NT_SUCCESS(Status))
        {
            if ((Value->Type == REG_SZ || Value->Type == (0xFFFF0000 | DEVPROP_TYPE_STRING)) && Value->DataLength)
            {
                ZwSetValueKey(Parameters, &FriendlyNameU, 0, REG_SZ,
                              (PUCHAR)Value + Value->DataOffset, Value->DataLength);
            }
            ExFreePool(Value);
        }
        ZwClose(Source);
    }

Cleanup:
    if (Parameters)
        ZwClose(Parameters);
    if (ReferenceKey)
        ZwClose(ReferenceKey);
    if (Key)
        ZwClose(Key);
    if (Class)
        ZwClose(Class);
    ExFreePoolWithTag(KeyName.Buffer, TAG_IO);
}

static
VOID
IopApplyDriverDatabaseInterfaces(
    _In_ PCUNICODE_STRING InstancePath,
    _In_ HANDLE Configuration)
{
    UNICODE_STRING InterfacesU = RTL_CONSTANT_STRING(L"Interfaces");
    PKEY_BASIC_INFORMATION KeyInformation;
    HANDLE Interfaces, Interface;
    UNICODE_STRING Name;
    ULONG Index, Length;
    NTSTATUS Status;

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Interfaces, Configuration, &InterfacesU, KEY_READ)))
        return;

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(Interfaces, Index, KeyBasicInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        KeyInformation = ExAllocatePool(PagedPool, Length);
        if (!KeyInformation)
            break;
        if (NT_SUCCESS(ZwEnumerateKey(Interfaces, Index, KeyBasicInformation, KeyInformation, Length, &Length)))
        {
            Name.Buffer = KeyInformation->Name;
            Name.Length = Name.MaximumLength = (USHORT)KeyInformation->NameLength;
            if (NT_SUCCESS(IopOpenRegistryKeyEx(&Interface, Interfaces, &Name, KEY_READ)))
            {
                IopApplyDriverDatabaseInterface(InstancePath, Interface, &Name);
                ZwClose(Interface);
            }
        }
        ExFreePool(KeyInformation);
    }
    ZwClose(Interfaces);
}


/*
 * The class of a device is written next to the class GUID, as the name the
 * class key carries, which is what a caller asking for the class property
 * reads back.
 */
static
VOID
IopSetDriverDatabaseClassName(
    _In_ HANDLE InstanceKey,
    _In_ PCUNICODE_STRING ClassGuidString)
{
    static CONST UNICODE_STRING ClassRootU =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");
    PKEY_VALUE_FULL_INFORMATION Information;
    HANDLE ClassRoot, ClassKey;
    UNICODE_STRING Name;

    PAGED_CODE();

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&ClassRoot, NULL, (PUNICODE_STRING)&ClassRootU, KEY_READ)))
        return;

    Name = *ClassGuidString;
    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&ClassKey, ClassRoot, &Name, KEY_READ)))
    {
        ZwClose(ClassRoot);
        return;
    }

    ZwClose(ClassRoot);

    if (NT_SUCCESS(IopGetRegistryValue(ClassKey, L"Class", &Information)))
    {
        if (Information->Type == REG_SZ && Information->DataLength > sizeof(WCHAR))
        {
            IopSetDriverDatabaseValue(InstanceKey,
                                      L"Class",
                                      REG_SZ,
                                      (PUCHAR)Information + Information->DataOffset,
                                      Information->DataLength);
        }

        ExFreePool(Information);
    }

    ZwClose(ClassKey);
}

static
BOOLEAN
IopApplyDriverDatabaseMatch(
    _In_ HANDLE Database,
    _In_ HANDLE InstanceKey,
    _In_ PCUNICODE_STRING InstancePath,
    _In_ PIOP_DRIVER_DATABASE_MATCH Match)
{
    UNICODE_STRING DeviceU = RTL_CONSTANT_STRING(L"Device");
    UNICODE_STRING StringsU = RTL_CONSTANT_STRING(L"Strings");
    UNICODE_STRING ParametersU = RTL_CONSTANT_STRING(L"Device Parameters");
    PIOP_DRIVER_DATABASE_CONFIGURATION Config;
    PKEY_VALUE_FULL_INFORMATION ConfigFlags;
    HANDLE Package = NULL, Descriptor = NULL, Configuration = NULL, Strings = NULL;
    HANDLE Source, Destination;
    UNICODE_STRING GuidString;
    ULONG Index, Flags = 0;
    BOOLEAN Applied = FALSE;

    Config = ExAllocatePoolWithTag(PagedPool, sizeof(*Config), TAG_IO);
    if (!Config)
        return FALSE;
    RtlZeroMemory(Config, sizeof(*Config));

    if (!NT_SUCCESS(IopOpenDriverDatabaseKey(&Package, Database, L"DriverPackages\\%s", Match->PackageId, KEY_READ)) ||
        !NT_SUCCESS(IopOpenDriverDatabaseKey(&Descriptor, Package, L"Descriptors\\%s", Match->DeviceId, KEY_READ)) ||
        !IopGetDriverDatabaseString(Descriptor, L"Configuration", Config->Configuration, RTL_NUMBER_OF(Config->Configuration)) ||
        !NT_SUCCESS(IopOpenDriverDatabaseKey(&Configuration, Package, L"Configurations\\%s", Config->Configuration, KEY_READ)) ||
        !IopGetDriverDatabaseString(Configuration, L"Service", Config->Service, RTL_NUMBER_OF(Config->Service)))
    {
        goto Cleanup;
    }

    if (NT_SUCCESS(IopGetRegistryValue(Configuration, L"ConfigFlags", &ConfigFlags)))
    {
        if (ConfigFlags->Type == REG_DWORD && ConfigFlags->DataLength == sizeof(ULONG))
            Flags = *(PULONG)((PUCHAR)ConfigFlags + ConfigFlags->DataOffset);
        ExFreePool(ConfigFlags);
        if (Flags & CONFIGFLAG_FINISH_INSTALL)
            goto Cleanup;
        Flags = 0;
    }

    if (!NT_SUCCESS(RtlStringFromGUID(&Match->ClassGuid, &GuidString)))
        goto Cleanup;

    for (Index = 0; Index < GuidString.Length / sizeof(WCHAR); Index++)
    {
        if (GuidString.Buffer[Index] >= L'A' && GuidString.Buffer[Index] <= L'F')
            GuidString.Buffer[Index] += L'a' - L'A';
    }

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Strings, Package, &StringsU, KEY_READ)))
        Strings = NULL;
    IopGetDriverDatabaseText(Strings, Descriptor, L"Description", Config->Description,
                             RTL_NUMBER_OF(Config->Description), Config->Token, RTL_NUMBER_OF(Config->Token));
    IopGetDriverDatabaseText(Strings, Descriptor, L"Manufacturer", Config->Manufacturer,
                             RTL_NUMBER_OF(Config->Manufacturer), Config->Token, RTL_NUMBER_OF(Config->Token));
    if (!IopGetDriverDatabaseString(Package, L"Provider", Config->Provider, RTL_NUMBER_OF(Config->Provider)))
        Config->Provider[0] = UNICODE_NULL;
    IopGetDriverDatabaseMatchingId(Descriptor, Match->DeviceId, Config->MatchingDeviceId,
                                   RTL_NUMBER_OF(Config->MatchingDeviceId));

    IopSetDriverDatabaseValue(InstanceKey, L"ClassGUID", REG_SZ, GuidString.Buffer,
                              GuidString.Length + sizeof(UNICODE_NULL));
    IopSetDriverDatabaseClassName(InstanceKey, &GuidString);
    RtlFreeUnicodeString(&GuidString);
    IopSetDriverDatabaseString(InstanceKey, L"Service", Config->Service);
    if (Config->Description[0])
        IopSetDriverDatabaseString(InstanceKey, L"DeviceDesc", Config->Description);
    if (Config->Manufacturer[0])
        IopSetDriverDatabaseString(InstanceKey, L"Mfg", Config->Manufacturer);
    if (NT_SUCCESS(IopGetRegistryValue(InstanceKey, L"ConfigFlags", &ConfigFlags)))
        ExFreePool(ConfigFlags);
    else
        IopSetDriverDatabaseValue(InstanceKey, L"ConfigFlags", REG_DWORD, &Flags, sizeof(Flags));

    if (NT_SUCCESS(IopOpenRegistryKeyEx(&Source, Configuration, &DeviceU, KEY_READ)))
    {
        if (NT_SUCCESS(IopCreateRegistryKeyEx(&Destination, InstanceKey, &ParametersU,
                                              KEY_ALL_ACCESS, REG_OPTION_NON_VOLATILE, NULL)))
        {
            IopCopyRegistryTree(Source, Destination, 0);
            ZwClose(Destination);
        }
        ZwClose(Source);
    }

    IopApplyDriverDatabaseFilters(InstanceKey, Configuration);
    IopApplyDriverDatabaseInterfaces(InstancePath, Configuration);
    IopApplyDriverDatabaseDriverKey(InstanceKey, Configuration, Match, Config);
    IopApplyDriverDatabaseServices(Configuration, Config);

    DPRINT("Configured %S from driver package %S section %S rank 0x%08lx\n",
           Match->DeviceId, Match->PackageId, Config->Configuration, Match->Rank);
    Applied = TRUE;

Cleanup:
    if (Strings)
        ZwClose(Strings);
    if (Configuration)
        ZwClose(Configuration);
    if (Descriptor)
        ZwClose(Descriptor);
    if (Package)
        ZwClose(Package);
    ExFreePoolWithTag(Config, TAG_IO);
    return Applied;
}

BOOLEAN
NTAPI
IopConfigureDeviceFromDriverDatabase(
    _In_ PDEVICE_NODE DeviceNode)
{
    UNICODE_STRING DatabaseU = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\DriverDatabase");
    PKEY_VALUE_FULL_INFORMATION Ids[2] = { NULL, NULL };
    PWSTR IdNames[2] = { L"HardwareID", L"CompatibleIDs" };
    PIOP_DRIVER_DATABASE_MATCH Matches;
    PKEY_VALUE_FULL_INFORMATION Service;
    HANDLE Database, InstanceKey;
    BOOLEAN Applied = FALSE;
    PCWSTR Id, End;
    SIZE_T IdLength;
    ULONG List, Position;

    if (!NT_SUCCESS(IopOpenInstanceKeyByPath(&DeviceNode->InstancePath, &InstanceKey)))
        return FALSE;

    if (NT_SUCCESS(IopGetRegistryValue(InstanceKey, L"Service", &Service)))
    {
        ExFreePool(Service);
        ZwClose(InstanceKey);
        return FALSE;
    }

    if (!NT_SUCCESS(IopOpenRegistryKeyEx(&Database, NULL, &DatabaseU, KEY_READ)))
    {
        ZwClose(InstanceKey);
        return FALSE;
    }

    Matches = ExAllocatePoolWithTag(PagedPool, 2 * sizeof(*Matches), TAG_IO);
    if (!Matches)
    {
        ZwClose(Database);
        ZwClose(InstanceKey);
        return FALSE;
    }
    RtlZeroMemory(Matches, 2 * sizeof(*Matches));

    for (List = 0; List < RTL_NUMBER_OF(Ids); List++)
    {
        if (!NT_SUCCESS(IopGetRegistryValue(InstanceKey, IdNames[List], &Ids[List])))
            continue;
        if (Ids[List]->Type != REG_MULTI_SZ)
            continue;

        Id = (PCWSTR)((PUCHAR)Ids[List] + Ids[List]->DataOffset);
        End = Id + Ids[List]->DataLength / sizeof(WCHAR);
        for (Position = 0; Id < End && *Id && Position < 0x1000; Position++)
        {
            IdLength = wcsnlen(Id, End - Id);
            if (IdLength == (SIZE_T)(End - Id))
                break;
            if (IdLength < RTL_NUMBER_OF(Matches->DeviceId))
                IopRankDriverDatabaseId(Database, Id, List != 0, Position, &Matches[0], &Matches[1]);
            Id += IdLength + 1;
        }
    }

    if (Matches[0].Found)
        Applied = IopApplyDriverDatabaseMatch(Database, InstanceKey, &DeviceNode->InstancePath, &Matches[0]);

    for (List = 0; List < RTL_NUMBER_OF(Ids); List++)
    {
        if (Ids[List])
            ExFreePool(Ids[List]);
    }
    ExFreePoolWithTag(Matches, TAG_IO);
    ZwClose(Database);
    ZwClose(InstanceKey);
    return Applied;
}

