/*
 * PROJECT:         ReactOS Kernel
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/pnpmgr/pnpmgr.c
 * PURPOSE:         Initializes the PnP manager
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

ERESOURCE PpRegistryDeviceResource;
KGUARDED_MUTEX PpDeviceReferenceTableLock;
RTL_AVL_TABLE PpDeviceReferenceTable;

extern ULONG ExpInitializationPhase;

/* DATA **********************************************************************/

PDRIVER_OBJECT IopRootDriverObject;
IO_BUS_TYPE_GUID_LIST PnpBusTypeGuidList;

/* FUNCTIONS *****************************************************************/

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
IopApplyDriverDatabaseMatch(
    _In_ HANDLE Database,
    _In_ HANDLE InstanceKey,
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
        !IopGetDriverDatabaseString(Configuration, L"Service", Config->Service, RTL_NUMBER_OF(Config->Service)) ||
        !NT_SUCCESS(RtlStringFromGUID(&Match->ClassGuid, &GuidString)))
    {
        goto Cleanup;
    }

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

    if (!NT_SUCCESS(IopCreateDeviceKeyPath(&DeviceNode->InstancePath, REG_OPTION_NON_VOLATILE, &InstanceKey)))
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
        Applied = IopApplyDriverDatabaseMatch(Database, InstanceKey, &Matches[0]);

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

NTSTATUS
IopGetSystemPowerDeviceObject(PDEVICE_OBJECT *DeviceObject)
{
    KIRQL OldIrql;

    if (PopSystemPowerDeviceNode)
    {
        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
        *DeviceObject = PopSystemPowerDeviceNode->PhysicalDeviceObject;
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

USHORT
NTAPI
IopGetBusTypeGuidIndex(LPGUID BusTypeGuid)
{
    USHORT i, FoundIndex = 0xFFFF;
    ULONG NewAllocatedCount;
    PGUID NewList;

    /* Acquire the lock */
    ExAcquireFastMutex(&PnpBusTypeGuidList.Lock);

    /* Loop all entries */
    for (i = 0; i < PnpBusTypeGuidList.GuidCount; i++)
    {
         /* Try to find a match */
         if (IsEqualGUID(BusTypeGuid, &PnpBusTypeGuidList.Guids[i]))
         {
              /* Found it */
              FoundIndex = i;
              goto Quickie;
         }
    }

    /* Check if we have to grow the list */
    ASSERT(PnpBusTypeGuidList.GuidCount <= PnpBusTypeGuidList.AllocatedCount);
    if (PnpBusTypeGuidList.GuidCount == PnpBusTypeGuidList.AllocatedCount)
    {
        /* Calculate the new size */
        NewAllocatedCount = PnpBusTypeGuidList.AllocatedCount + 8;

        /* Allocate the new copy */
        NewList = ExAllocatePoolWithTag(PagedPool,
                                        NewAllocatedCount * sizeof(GUID),
                                        TAG_PNP_GUIDS);
        if (!NewList)
        {
            /* Fail */
            goto Quickie;
        }

        /* Copy the existing GUIDs to the new list and zero the rest */
        RtlCopyMemory(NewList,
                      PnpBusTypeGuidList.Guids,
                      PnpBusTypeGuidList.GuidCount * sizeof(GUID));
        RtlZeroMemory(&NewList[PnpBusTypeGuidList.GuidCount],
                      (NewAllocatedCount - PnpBusTypeGuidList.GuidCount) * sizeof(GUID));

        /* Free the old list */
        ExFreePool(PnpBusTypeGuidList.Guids);

        /* Use the new buffer */
        PnpBusTypeGuidList.Guids = NewList;
        PnpBusTypeGuidList.AllocatedCount = NewAllocatedCount;
    }

    /* Copy the new GUID */
    PnpBusTypeGuidList.Guids[PnpBusTypeGuidList.GuidCount] = *BusTypeGuid;

    /* The new entry is the index */
    ASSERT(PnpBusTypeGuidList.GuidCount < MAXUSHORT);
    FoundIndex = (USHORT)PnpBusTypeGuidList.GuidCount;
    PnpBusTypeGuidList.GuidCount++;

Quickie:
    ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);
    return FoundIndex;
}

NTSTATUS
NTAPI
IopInitiatePnpIrp(IN PDEVICE_OBJECT DeviceObject,
                  IN OUT PIO_STATUS_BLOCK IoStatusBlock,
                  IN UCHAR MinorFunction,
                  IN PIO_STACK_LOCATION Stack OPTIONAL)
{
    IO_STACK_LOCATION IoStackLocation;

    /* Fill out the stack information */
    RtlZeroMemory(&IoStackLocation, sizeof(IO_STACK_LOCATION));
    IoStackLocation.MajorFunction = IRP_MJ_PNP;
    IoStackLocation.MinorFunction = MinorFunction;
    if (Stack)
    {
        /* Copy the rest */
        RtlCopyMemory(&IoStackLocation.Parameters,
                      &Stack->Parameters,
                      sizeof(Stack->Parameters));
    }

    /* Do the PnP call */
    IoStatusBlock->Status = IopSynchronousCall(DeviceObject,
                                               &IoStackLocation,
                                               (PVOID)&IoStatusBlock->Information);
    return IoStatusBlock->Status;
}

/*
 * IopCreateDeviceKeyPath
 *
 * Creates a registry key
 *
 * Parameters
 *    RegistryPath
 *        Name of the key to be created.
 *    Handle
 *        Handle to the newly created key
 *
 * Remarks
 *     This method can create nested trees, so parent of RegistryPath can
 *     be not existant, and will be created if needed.
 */
NTSTATUS
NTAPI
IopCreateDeviceKeyPath(IN PCUNICODE_STRING RegistryPath,
                       IN ULONG CreateOptions,
                       OUT PHANDLE Handle)
{
    UNICODE_STRING EnumU = RTL_CONSTANT_STRING(ENUM_ROOT);
    HANDLE hParent = NULL, hKey;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    PCWSTR Current, Last;
    USHORT Length;
    NTSTATUS Status;

    /* Assume failure */
    *Handle = NULL;

    /* Open root key for device instances */
    Status = IopOpenRegistryKeyEx(&hParent, NULL, &EnumU, KEY_CREATE_SUB_KEY);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwOpenKey('%wZ') failed with status 0x%08lx\n", &EnumU, Status);
        return Status;
    }

    Current = KeyName.Buffer = RegistryPath->Buffer;
    Last = &RegistryPath->Buffer[RegistryPath->Length / sizeof(WCHAR)];

    /* Go up to the end of the string */
    while (Current <= Last)
    {
        if (Current != Last && *Current != L'\\')
        {
            /* Not the end of the string and not a separator */
            Current++;
            continue;
        }

        /* Prepare relative key name */
        Length = (USHORT)((ULONG_PTR)Current - (ULONG_PTR)KeyName.Buffer);
        KeyName.MaximumLength = KeyName.Length = Length;
        DPRINT("Create '%wZ'\n", &KeyName);

        /* Open key */
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   hParent,
                                   NULL);
        Status = ZwCreateKey(&hKey,
                             Current == Last ? KEY_ALL_ACCESS : KEY_CREATE_SUB_KEY,
                             &ObjectAttributes,
                             0,
                             NULL,
                             CreateOptions,
                             NULL);

        /* Close parent key handle, we don't need it anymore */
        if (hParent)
            ZwClose(hParent);

        /* Key opening/creating failed? */
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ZwCreateKey('%wZ') failed with status 0x%08lx\n", &KeyName, Status);
            return Status;
        }

        /* Check if it is the end of the string */
        if (Current == Last)
        {
            /* Yes, return success */
            *Handle = hKey;
            return STATUS_SUCCESS;
        }

        /* Start with this new parent key */
        hParent = hKey;
        Current++;
        KeyName.Buffer = (PWSTR)Current;
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
IopSetDeviceInstanceData(HANDLE InstanceKey,
                         PDEVICE_NODE DeviceNode)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE LogConfKey, ControlKey, DeviceParamsKey;
    ULONG ResCount;
    ULONG ResultLength;
    NTSTATUS Status;

    DPRINT("IopSetDeviceInstanceData() called\n");

    /* Create the 'LogConf' key */
    RtlInitUnicodeString(&KeyName, L"LogConf");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&LogConfKey,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         // FIXME? In r53694 it was silently turned from non-volatile into this,
                         // without any extra warning. Is this still needed??
                         REG_OPTION_VOLATILE,
                         NULL);
    if (NT_SUCCESS(Status))
    {
        /* Set 'BootConfig' value */
        if (DeviceNode->BootResources != NULL)
        {
            ResCount = DeviceNode->BootResources->Count;
            if (ResCount != 0)
            {
                RtlInitUnicodeString(&KeyName, L"BootConfig");
                Status = ZwSetValueKey(LogConfKey,
                                       &KeyName,
                                       0,
                                       REG_RESOURCE_LIST,
                                       DeviceNode->BootResources,
                                       PnpDetermineResourceListSize(DeviceNode->BootResources));
            }
        }

        /* Set 'BasicConfigVector' value */
        if (DeviceNode->ResourceRequirements != NULL &&
            DeviceNode->ResourceRequirements->ListSize != 0)
        {
            RtlInitUnicodeString(&KeyName, L"BasicConfigVector");
            Status = ZwSetValueKey(LogConfKey,
                                   &KeyName,
                                   0,
                                   REG_RESOURCE_REQUIREMENTS_LIST,
                                   DeviceNode->ResourceRequirements,
                                   DeviceNode->ResourceRequirements->ListSize);
        }

        ZwClose(LogConfKey);
    }

    /* Set the 'ConfigFlags' value */
    RtlInitUnicodeString(&KeyName, L"ConfigFlags");
    Status = ZwQueryValueKey(InstanceKey,
                             &KeyName,
                             KeyValueBasicInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        /* Write the default value */
        ULONG DefaultConfigFlags = 0;
        Status = ZwSetValueKey(InstanceKey,
                               &KeyName,
                               0,
                               REG_DWORD,
                               &DefaultConfigFlags,
                               sizeof(DefaultConfigFlags));
    }

    /* Create the 'Control' key */
    RtlInitUnicodeString(&KeyName, L"Control");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&ControlKey,
                         0,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    if (NT_SUCCESS(Status))
        ZwClose(ControlKey);

    /* Create the 'Device Parameters' key and set the 'FirmwareIdentified' value for all ACPI-enumerated devices */
    if (_wcsnicmp(DeviceNode->InstancePath.Buffer, L"ACPI\\", 5) == 0)
    {
        RtlInitUnicodeString(&KeyName, L"Device Parameters");
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   InstanceKey,
                                   NULL);
        Status = ZwCreateKey(&DeviceParamsKey,
                             0,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
        if (NT_SUCCESS(Status))
        {
            ULONG FirmwareIdentified = 1;
            RtlInitUnicodeString(&KeyName, L"FirmwareIdentified");
            Status = ZwSetValueKey(DeviceParamsKey,
                                   &KeyName,
                                   0,
                                   REG_DWORD,
                                   &FirmwareIdentified,
                                   sizeof(FirmwareIdentified));

            ZwClose(DeviceParamsKey);
        }
    }

    DPRINT("IopSetDeviceInstanceData() done\n");

    return Status;
}

/*
 * IopGetParentIdPrefix
 *
 * Retrieve (or create) a string which identifies a device.
 *
 * Parameters
 *    DeviceNode
 *        Pointer to device node.
 *    ParentIdPrefix
 *        Pointer to the string where is returned the parent node identifier
 *
 * Remarks
 *     If the return code is STATUS_SUCCESS, the ParentIdPrefix string is
 *     valid and its Buffer field is NULL-terminated. The caller needs to
 *     to free the string with RtlFreeUnicodeString when it is no longer
 *     needed.
 */

NTSTATUS
IopGetParentIdPrefix(PDEVICE_NODE DeviceNode,
                     PUNICODE_STRING ParentIdPrefix)
{
    const UNICODE_STRING EnumKeyPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum\\");
    ULONG KeyNameBufferLength;
    PKEY_VALUE_PARTIAL_INFORMATION ParentIdPrefixInformation = NULL;
    UNICODE_STRING KeyName = {0, 0, NULL};
    UNICODE_STRING KeyValue;
    UNICODE_STRING ValueName;
    HANDLE hKey = NULL;
    ULONG crc32;
    NTSTATUS Status;

    /* HACK: As long as some devices have a NULL device
     * instance path, the following test is required :(
     */
    if (DeviceNode->Parent->InstancePath.Length == 0)
    {
        DPRINT1("Parent of %wZ has NULL Instance path, please report!\n",
                &DeviceNode->InstancePath);
        return STATUS_UNSUCCESSFUL;
    }

    /* 1. Try to retrieve ParentIdPrefix from registry */
    KeyNameBufferLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(L"12345678&12345678");
    ParentIdPrefixInformation = ExAllocatePoolWithTag(PagedPool,
                                                      KeyNameBufferLength + sizeof(UNICODE_NULL),
                                                      TAG_IO);
    if (!ParentIdPrefixInformation)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeyName.Length = 0;
    KeyName.MaximumLength = EnumKeyPath.Length +
                            DeviceNode->Parent->InstancePath.Length +
                            sizeof(UNICODE_NULL);
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                           KeyName.MaximumLength,
                                           TAG_IO);
    if (!KeyName.Buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    RtlCopyUnicodeString(&KeyName, &EnumKeyPath);
    RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->Parent->InstancePath);

    Status = IopOpenRegistryKeyEx(&hKey, NULL, &KeyName, KEY_QUERY_VALUE | KEY_SET_VALUE);
    if (!NT_SUCCESS(Status))
    {
        goto cleanup;
    }
    RtlInitUnicodeString(&ValueName, L"ParentIdPrefix");
    Status = ZwQueryValueKey(hKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             ParentIdPrefixInformation,
                             KeyNameBufferLength,
                             &KeyNameBufferLength);
    if (NT_SUCCESS(Status))
    {
        if (ParentIdPrefixInformation->Type != REG_SZ)
        {
            Status = STATUS_UNSUCCESSFUL;
        }
        else
        {
            KeyValue.MaximumLength = (USHORT)ParentIdPrefixInformation->DataLength;
            KeyValue.Length = KeyValue.MaximumLength - sizeof(UNICODE_NULL);
            KeyValue.Buffer = (PWSTR)ParentIdPrefixInformation->Data;
            ASSERT(KeyValue.Buffer[KeyValue.Length / sizeof(WCHAR)] == UNICODE_NULL);
        }
        goto cleanup;
    }
    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        /* FIXME how do we get here and why is ParentIdPrefixInformation valid? */
        KeyValue.MaximumLength = (USHORT)ParentIdPrefixInformation->DataLength;
        KeyValue.Length = KeyValue.MaximumLength - sizeof(UNICODE_NULL);
        KeyValue.Buffer = (PWSTR)ParentIdPrefixInformation->Data;
        ASSERT(KeyValue.Buffer[KeyValue.Length / sizeof(WCHAR)] == UNICODE_NULL);
        goto cleanup;
    }

    /* 2. Create the ParentIdPrefix value */
    crc32 = RtlComputeCrc32(0,
                            (PUCHAR)DeviceNode->Parent->InstancePath.Buffer,
                            DeviceNode->Parent->InstancePath.Length);

    RtlStringCbPrintfW((PWSTR)ParentIdPrefixInformation,
                       KeyNameBufferLength,
                       L"%lx&%lx",
                       DeviceNode->Parent->Level,
                       crc32);
    RtlInitUnicodeString(&KeyValue, (PWSTR)ParentIdPrefixInformation);

    /* 3. Try to write the ParentIdPrefix to registry */
    Status = ZwSetValueKey(hKey,
                           &ValueName,
                           0,
                           REG_SZ,
                           KeyValue.Buffer,
                           ((ULONG)wcslen(KeyValue.Buffer) + 1) * sizeof(WCHAR));

cleanup:
    if (NT_SUCCESS(Status))
    {
        /* Duplicate the string to return it */
        Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                           &KeyValue,
                                           ParentIdPrefix);
    }
    ExFreePoolWithTag(ParentIdPrefixInformation, TAG_IO);
    RtlFreeUnicodeString(&KeyName);
    if (hKey != NULL)
    {
        ZwClose(hKey);
    }
    return Status;
}

NTSTATUS
NTAPI
IopOpenRegistryKeyEx(PHANDLE KeyHandle,
                     HANDLE ParentKey,
                     PUNICODE_STRING Name,
                     ACCESS_MASK DesiredAccess)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    PAGED_CODE();

    *KeyHandle = NULL;

    InitializeObjectAttributes(&ObjectAttributes,
        Name,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        ParentKey,
        NULL);

    Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);

    return Status;
}

NTSTATUS
NTAPI
IopCreateRegistryKeyEx(OUT PHANDLE Handle,
                       IN HANDLE RootHandle OPTIONAL,
                       IN PUNICODE_STRING KeyName,
                       IN ACCESS_MASK DesiredAccess,
                       IN ULONG CreateOptions,
                       OUT PULONG Disposition OPTIONAL)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG KeyDisposition, RootHandleIndex = 0, i = 1, NestedCloseLevel = 0;
    USHORT Length;
    HANDLE HandleArray[2];
    BOOLEAN Recursing = TRUE;
    PWCHAR pp, p, p1;
    UNICODE_STRING KeyString;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    /* P1 is start, pp is end */
    p1 = KeyName->Buffer;
    pp = (PVOID)((ULONG_PTR)p1 + KeyName->Length);

    /* Create the target key */
    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               RootHandle,
                               NULL);
    Status = ZwCreateKey(&HandleArray[i],
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         CreateOptions,
                         &KeyDisposition);

    /* Now we check if this failed */
    if ((Status == STATUS_OBJECT_NAME_NOT_FOUND) && (RootHandle))
    {
        /* Target key failed, so we'll need to create its parent. Setup array */
        HandleArray[0] = NULL;
        HandleArray[1] = RootHandle;

        /* Keep recursing for each missing parent */
        while (Recursing)
        {
            /* And if we're deep enough, close the last handle */
            if (NestedCloseLevel > 1) ZwClose(HandleArray[RootHandleIndex]);

            /* We're setup to ping-pong between the two handle array entries */
            RootHandleIndex = i;
            i = (i + 1) & 1;

            /* Clear the one we're attempting to open now */
            HandleArray[i] = NULL;

            /* Process the parent key name */
            for (p = p1; ((p < pp) && (*p != OBJ_NAME_PATH_SEPARATOR)); p++);
            Length = (USHORT)(p - p1) * sizeof(WCHAR);

            /* Is there a parent name? */
            if (Length)
            {
                /* Build the unicode string for it */
                KeyString.Buffer = p1;
                KeyString.Length = KeyString.MaximumLength = Length;

                /* Now try opening the parent */
                InitializeObjectAttributes(&ObjectAttributes,
                                           &KeyString,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           HandleArray[RootHandleIndex],
                                           NULL);
                Status = ZwCreateKey(&HandleArray[i],
                                     DesiredAccess,
                                     &ObjectAttributes,
                                     0,
                                     NULL,
                                     CreateOptions,
                                     &KeyDisposition);
                if (NT_SUCCESS(Status))
                {
                    /* It worked, we have one more handle */
                    NestedCloseLevel++;
                }
                else
                {
                    /* Parent key creation failed, abandon loop */
                    Recursing = FALSE;
                    continue;
                }
            }
            else
            {
                /* We don't have a parent name, probably corrupted key name */
                Status = STATUS_INVALID_PARAMETER;
                Recursing = FALSE;
                continue;
            }

            /* Now see if there's more parents to create */
            p1 = p + 1;
            if ((p == pp) || (p1 == pp))
            {
                /* We're done, hopefully successfully, so stop */
                Recursing = FALSE;
            }
        }

        /* Outer loop check for handle nesting that requires closing the top handle */
        if (NestedCloseLevel > 1) ZwClose(HandleArray[RootHandleIndex]);
    }

    /* Check if we broke out of the loop due to success */
    if (NT_SUCCESS(Status))
    {
        /* Return the target handle (we closed all the parent ones) and disposition */
        *Handle = HandleArray[i];
        if (Disposition) *Disposition = KeyDisposition;
    }

    /* Return the success state */
    return Status;
}

NTSTATUS
NTAPI
IopGetRegistryValue(IN HANDLE Handle,
                    IN PWSTR ValueName,
                    OUT PKEY_VALUE_FULL_INFORMATION *Information)
{
    UNICODE_STRING ValueString;
    NTSTATUS Status;
    PKEY_VALUE_FULL_INFORMATION FullInformation;
    ULONG Size;
    PAGED_CODE();

    RtlInitUnicodeString(&ValueString, ValueName);

    Status = ZwQueryValueKey(Handle,
                             &ValueString,
                             KeyValueFullInformation,
                             NULL,
                             0,
                             &Size);
    if ((Status != STATUS_BUFFER_OVERFLOW) &&
        (Status != STATUS_BUFFER_TOO_SMALL))
    {
        return Status;
    }

    FullInformation = ExAllocatePool(NonPagedPool, Size);
    if (!FullInformation) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(Handle,
                             &ValueString,
                             KeyValueFullInformation,
                             FullInformation,
                             Size,
                             &Size);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(FullInformation);
        return Status;
    }

    *Information = FullInformation;
    return STATUS_SUCCESS;
}

RTL_GENERIC_COMPARE_RESULTS
NTAPI
PiCompareInstancePath(IN PRTL_AVL_TABLE Table,
                      IN PVOID FirstStruct,
                      IN PVOID SecondStruct)
{
    /* FIXME: TODO */
    ASSERT(FALSE);
    return 0;
}

//
//  The allocation function is called by the generic table package whenever
//  it needs to allocate memory for the table.
//

PVOID
NTAPI
PiAllocateGenericTableEntry(IN PRTL_AVL_TABLE Table,
                            IN CLONG ByteSize)
{
    /* FIXME: TODO */
    ASSERT(FALSE);
    return NULL;
}

VOID
NTAPI
PiFreeGenericTableEntry(IN PRTL_AVL_TABLE Table,
                        IN PVOID Buffer)
{
    /* FIXME: TODO */
    ASSERT(FALSE);
}

VOID
NTAPI
PpInitializeDeviceReferenceTable(VOID)
{
    /* Setup the guarded mutex and AVL table */
    KeInitializeGuardedMutex(&PpDeviceReferenceTableLock);
    RtlInitializeGenericTableAvl(
        &PpDeviceReferenceTable,
        (PRTL_AVL_COMPARE_ROUTINE)PiCompareInstancePath,
        (PRTL_AVL_ALLOCATE_ROUTINE)PiAllocateGenericTableEntry,
        (PRTL_AVL_FREE_ROUTINE)PiFreeGenericTableEntry,
        NULL);
}

BOOLEAN
NTAPI
PiInitPhase0(VOID)
{
    /* Initialize the resource when accessing device registry data */
    ExInitializeResourceLite(&PpRegistryDeviceResource);

    /* In-memory resource database */
    IopResDbInitialize();

    /* Setup the device reference AVL table */
    PpInitializeDeviceReferenceTable();
    return TRUE;
}

BOOLEAN
NTAPI
PpInitSystem(VOID)
{
    /* Check the initialization phase */
    switch (ExpInitializationPhase)
    {
    case 0:

        /* Do Phase 0 */
        return PiInitPhase0();

    case 1:

        /* Do Phase 1 */
        return TRUE;
        //return PiInitPhase1();

    default:

        /* Don't know any other phase! Bugcheck! */
        KeBugCheck(UNEXPECTED_INITIALIZATION_CALL);
        return FALSE;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
PnpBusTypeGuidGet(IN USHORT Index,
                  IN LPGUID BusTypeGuid)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* Acquire the lock */
    ExAcquireFastMutex(&PnpBusTypeGuidList.Lock);

    /* Validate size */
    if (Index < PnpBusTypeGuidList.GuidCount)
    {
        /* Copy the data */
        RtlCopyMemory(BusTypeGuid, &PnpBusTypeGuidList.Guids[Index], sizeof(GUID));
    }
    else
    {
        /* Failure path */
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* Release lock and return status */
    ExReleaseFastMutex(&PnpBusTypeGuidList.Lock);
    return Status;
}

NTSTATUS
NTAPI
PnpDeviceObjectToDeviceInstance(IN PDEVICE_OBJECT DeviceObject,
                                IN PHANDLE DeviceInstanceHandle,
                                IN ACCESS_MASK DesiredAccess)
{
    NTSTATUS Status;
    HANDLE KeyHandle;
    PDEVICE_NODE DeviceNode;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\ENUM");
    PAGED_CODE();

    /* Open the enum key */
    Status = IopOpenRegistryKeyEx(&KeyHandle,
                                  NULL,
                                  &KeyName,
                                  KEY_READ);
    if (!NT_SUCCESS(Status)) return Status;

    /* Make sure we have an instance path */
    DeviceNode = IopGetDeviceNode(DeviceObject);
    if ((DeviceNode) && (DeviceNode->InstancePath.Length))
    {
        /* Get the instance key */
        Status = IopOpenRegistryKeyEx(DeviceInstanceHandle,
                                      KeyHandle,
                                      &DeviceNode->InstancePath,
                                      DesiredAccess);
    }
    else
    {
        /* Fail */
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    /* Close the handle and return status */
    ZwClose(KeyHandle);
    return Status;
}

ULONG
NTAPI
PnpDetermineResourceListSize(IN PCM_RESOURCE_LIST ResourceList)
{
    ULONG FinalSize, PartialSize, EntrySize, i, j;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;

    /* If we don't have one, that's easy */
    if (!ResourceList) return 0;

    /* Start with the minimum size possible */
    FinalSize = FIELD_OFFSET(CM_RESOURCE_LIST, List);

    /* Loop each full descriptor */
    FullDescriptor = ResourceList->List;
    for (i = 0; i < ResourceList->Count; i++)
    {
        /* Start with the minimum size possible */
        PartialSize = FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
        FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);

        /* Loop each partial descriptor */
        PartialDescriptor = FullDescriptor->PartialResourceList.PartialDescriptors;
        for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
        {
            /* Start with the minimum size possible */
            EntrySize = sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

            /* Check if there is extra data */
            if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
            {
                /* Add that data */
                EntrySize += PartialDescriptor->u.DeviceSpecificData.DataSize;
            }

            /* The size of partial descriptors is bigger */
            PartialSize += EntrySize;

            /* Go to the next partial descriptor */
            PartialDescriptor = (PVOID)((ULONG_PTR)PartialDescriptor + EntrySize);
        }

        /* The size of full descriptors is bigger */
        FinalSize += PartialSize;

        /* Go to the next full descriptor */
        FullDescriptor = (PVOID)((ULONG_PTR)FullDescriptor + PartialSize);
    }

    /* Return the final size */
    return FinalSize;
}

NTSTATUS
NTAPI
PiGetDeviceRegistryProperty(IN PDEVICE_OBJECT DeviceObject,
                            IN ULONG ValueType,
                            IN PWSTR ValueName,
                            IN PWSTR KeyName,
                            OUT PVOID Buffer,
                            IN PULONG BufferLength)
{
    NTSTATUS Status;
    HANDLE KeyHandle, SubHandle;
    UNICODE_STRING KeyString;
    PKEY_VALUE_FULL_INFORMATION KeyValueInfo = NULL;
    ULONG Length;
    PAGED_CODE();

    /* Find the instance key */
    Status = PnpDeviceObjectToDeviceInstance(DeviceObject, &KeyHandle, KEY_READ);
    if (NT_SUCCESS(Status))
    {
        /* Check for name given by caller */
        if (KeyName)
        {
            /* Open this key */
            RtlInitUnicodeString(&KeyString, KeyName);
            Status = IopOpenRegistryKeyEx(&SubHandle,
                                          KeyHandle,
                                          &KeyString,
                                          KEY_READ);
            if (NT_SUCCESS(Status))
            {
                /* And use this handle instead */
                ZwClose(KeyHandle);
                KeyHandle = SubHandle;
            }
        }

        /* Check if sub-key handle succeeded (or no-op if no key name given) */
        if (NT_SUCCESS(Status))
        {
            /* Now get the size of the property */
            Status = IopGetRegistryValue(KeyHandle,
                                         ValueName,
                                         &KeyValueInfo);
        }

        /* Close the key */
        ZwClose(KeyHandle);
    }

    /* Fail if any of the registry operations failed */
    if (!NT_SUCCESS(Status)) return Status;

    /* Check how much data we have to copy */
    Length = KeyValueInfo->DataLength;
    if (*BufferLength >= Length)
    {
        /* Check for a match in the value type */
        if (KeyValueInfo->Type == ValueType)
        {
            /* Copy the data */
            RtlCopyMemory(Buffer,
                          (PVOID)((ULONG_PTR)KeyValueInfo +
                          KeyValueInfo->DataOffset),
                          Length);
        }
        else
        {
            /* Invalid registry property type, fail */
           Status = STATUS_INVALID_PARAMETER_2;
        }
    }
    else
    {
        /* Buffer is too small to hold data */
        Status = STATUS_BUFFER_TOO_SMALL;
    }

    /* Return the required buffer length, free the buffer, and return status */
    *BufferLength = Length;
    ExFreePool(KeyValueInfo);
    return Status;
}

#define PIP_RETURN_DATA(x, y)   {ReturnLength = x; Data = y; Status = STATUS_SUCCESS; break;}
#define PIP_REGISTRY_DATA(x, y) {ValueName = x; ValueType = y; break;}
#define PIP_UNIMPLEMENTED()     {UNIMPLEMENTED_DBGBREAK(); break;}

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoGetDeviceProperty(IN PDEVICE_OBJECT DeviceObject,
                    IN DEVICE_REGISTRY_PROPERTY DeviceProperty,
                    IN ULONG BufferLength,
                    OUT PVOID PropertyBuffer,
                    OUT PULONG ResultLength)
{
    PDEVICE_NODE DeviceNode = IopGetDeviceNode(DeviceObject);
    DEVICE_CAPABILITIES DeviceCaps;
    ULONG ReturnLength = 0, Length = 0, ValueType;
    PWCHAR ValueName = NULL, EnumeratorNameEnd, DeviceInstanceName;
    PVOID Data = NULL;
    NTSTATUS Status = STATUS_BUFFER_TOO_SMALL;
    GUID BusTypeGuid;
    POBJECT_NAME_INFORMATION ObjectNameInfo = NULL;
    BOOLEAN NullTerminate = FALSE;
    DEVICE_REMOVAL_POLICY Policy;

    DPRINT("IoGetDeviceProperty(0x%p %d)\n", DeviceObject, DeviceProperty);

    /* Assume failure */
    *ResultLength = 0;

    /* Only PDOs can call this */
    if (!DeviceNode) return STATUS_INVALID_DEVICE_REQUEST;

    /* Handle all properties */
    switch (DeviceProperty)
    {
        case DevicePropertyBusTypeGuid:

            /* Get the GUID from the internal cache */
            Status = PnpBusTypeGuidGet(DeviceNode->ChildBusTypeIndex, &BusTypeGuid);
            if (!NT_SUCCESS(Status)) return Status;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(GUID), &BusTypeGuid);

        case DevicePropertyLegacyBusType:

            /* Validate correct interface type */
            if (DeviceNode->ChildInterfaceType == InterfaceTypeUndefined)
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(INTERFACE_TYPE), &DeviceNode->ChildInterfaceType);

        case DevicePropertyBusNumber:

            /* Validate correct bus number */
            if ((DeviceNode->ChildBusNumber & 0x80000000) == 0x80000000)
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(ULONG), &DeviceNode->ChildBusNumber);

        case DevicePropertyEnumeratorName:

            /* Get the instance path */
            DeviceInstanceName = DeviceNode->InstancePath.Buffer;

            /* Sanity checks */
            ASSERT((BufferLength & 1) == 0);
            ASSERT(DeviceInstanceName != NULL);

            /* Get the name from the path */
            EnumeratorNameEnd = wcschr(DeviceInstanceName, OBJ_NAME_PATH_SEPARATOR);
            ASSERT(EnumeratorNameEnd);

            /* This string needs to be NULL-terminated */
            NullTerminate = TRUE;

            /* This is the format of the returned data */
            PIP_RETURN_DATA((ULONG)(EnumeratorNameEnd - DeviceInstanceName) * sizeof(WCHAR),
                            DeviceInstanceName);

        case DevicePropertyAddress:

            /* Query the device caps */
            Status = IopQueryDeviceCapabilities(DeviceNode, &DeviceCaps);
            if (!NT_SUCCESS(Status) || (DeviceCaps.Address == MAXULONG))
                return STATUS_OBJECT_NAME_NOT_FOUND;

            /* This is the format of the returned data */
            PIP_RETURN_DATA(sizeof(ULONG), &DeviceCaps.Address);

        case DevicePropertyBootConfigurationTranslated:

            /* Validate we have resources */
            if (!DeviceNode->BootResources)
//            if (!DeviceNode->BootResourcesTranslated) // FIXFIX: Need this field
            {
                /* No resources will still fake success, but with 0 bytes */
                *ResultLength = 0;
                return STATUS_SUCCESS;
            }

            /* This is the format of the returned data */
            PIP_RETURN_DATA(PnpDetermineResourceListSize(DeviceNode->BootResources), // FIXFIX: Should use BootResourcesTranslated
                            DeviceNode->BootResources); // FIXFIX: Should use BootResourcesTranslated

        case DevicePropertyPhysicalDeviceObjectName:

            /* Sanity check for Unicode-sized string */
            ASSERT((BufferLength & 1) == 0);

            /* Allocate name buffer */
            Length = BufferLength + sizeof(OBJECT_NAME_INFORMATION);
            ObjectNameInfo = ExAllocatePool(PagedPool, Length);
            if (!ObjectNameInfo) return STATUS_INSUFFICIENT_RESOURCES;

            /* Query the PDO name */
            Status = ObQueryNameString(DeviceObject,
                                       ObjectNameInfo,
                                       Length,
                                       ResultLength);
            if (Status == STATUS_INFO_LENGTH_MISMATCH)
            {
                /* It's up to the caller to try again */
                Status = STATUS_BUFFER_TOO_SMALL;
            }

            /* This string needs to be NULL-terminated */
            NullTerminate = TRUE;

            /* Return if successful */
            if (NT_SUCCESS(Status)) PIP_RETURN_DATA(ObjectNameInfo->Name.Length,
                                                    ObjectNameInfo->Name.Buffer);

            /* Let the caller know how big the name is */
            *ResultLength -= sizeof(OBJECT_NAME_INFORMATION);
            break;

        case DevicePropertyRemovalPolicy:

            Policy = DeviceNode->RemovalPolicy;
            PIP_RETURN_DATA(sizeof(Policy), &Policy);

        /* Handle the registry-based properties */
        case DevicePropertyUINumber:
            PIP_REGISTRY_DATA(REGSTR_VAL_UI_NUMBER, REG_DWORD);
        case DevicePropertyLocationInformation:
            PIP_REGISTRY_DATA(REGSTR_VAL_LOCATION_INFORMATION, REG_SZ);
        case DevicePropertyDeviceDescription:
            PIP_REGISTRY_DATA(REGSTR_VAL_DEVDESC, REG_SZ);
        case DevicePropertyHardwareID:
            PIP_REGISTRY_DATA(REGSTR_VAL_HARDWAREID, REG_MULTI_SZ);
        case DevicePropertyCompatibleIDs:
            PIP_REGISTRY_DATA(REGSTR_VAL_COMPATIBLEIDS, REG_MULTI_SZ);
        case DevicePropertyBootConfiguration:
            PIP_REGISTRY_DATA(REGSTR_VAL_BOOTCONFIG, REG_RESOURCE_LIST);
        case DevicePropertyClassName:
            PIP_REGISTRY_DATA(REGSTR_VAL_CLASS, REG_SZ);
        case DevicePropertyClassGuid:
            PIP_REGISTRY_DATA(REGSTR_VAL_CLASSGUID, REG_SZ);
        case DevicePropertyDriverKeyName:
            PIP_REGISTRY_DATA(REGSTR_VAL_DRIVER, REG_SZ);
        case DevicePropertyManufacturer:
            PIP_REGISTRY_DATA(REGSTR_VAL_MFG, REG_SZ);
        case DevicePropertyFriendlyName:
            PIP_REGISTRY_DATA(REGSTR_VAL_FRIENDLYNAME, REG_SZ);
        case DevicePropertyContainerID:
            PIP_REGISTRY_DATA(REGSTR_VAL_CONTAINERID, REG_SZ);
        case DevicePropertyInstallState:
            PIP_REGISTRY_DATA(REGSTR_VAL_CONFIGFLAGS, REG_DWORD);
            break;
        case DevicePropertyResourceRequirements:
            PIP_UNIMPLEMENTED();
        case DevicePropertyAllocatedResources:
            PIP_UNIMPLEMENTED();
        default:
            return STATUS_INVALID_PARAMETER_2;
    }

    /* Having a registry value name implies registry data */
    if (ValueName)
    {
        /* We know up-front how much data to expect */
        *ResultLength = BufferLength;

        /* Go get the data, use the LogConf subkey if necessary */
        Status = PiGetDeviceRegistryProperty(DeviceObject,
                                             ValueType,
                                             ValueName,
                                             (DeviceProperty ==
                                              DevicePropertyBootConfiguration) ?
                                             L"LogConf":  NULL,
                                             PropertyBuffer,
                                             ResultLength);
    }
    else if (NT_SUCCESS(Status))
    {
        /* We know up-front how much data to expect, check the caller's buffer */
        *ResultLength = ReturnLength + (NullTerminate ? sizeof(UNICODE_NULL) : 0);
        if (*ResultLength <= BufferLength)
        {
            /* Buffer is all good, copy the data */
            RtlCopyMemory(PropertyBuffer, Data, ReturnLength);

            /* Check if we need to NULL-terminate the string */
            if (NullTerminate)
            {
                /* Terminate the string */
                ((PWCHAR)PropertyBuffer)[ReturnLength / sizeof(WCHAR)] = UNICODE_NULL;
            }

            /* This is the success path */
            Status = STATUS_SUCCESS;
        }
        else
        {
            /* Failure path */
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    /* Free any allocation we may have made, and return the status code */
    if (ObjectNameInfo) ExFreePool(ObjectNameInfo);
    return Status;
}

static
VOID
IopCreateKeyPath(
    _In_ PUNICODE_STRING RootPath,
    _In_ PCUNICODE_STRING RelativePath)
{
    HANDLE Parent = NULL, Key;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    PCWSTR Current, Last;
    NTSTATUS Status;

    Status = IopOpenRegistryKeyEx(&Parent, NULL, RootPath, KEY_CREATE_SUB_KEY);
    if (!NT_SUCCESS(Status))
        return;

    Current = KeyName.Buffer = RelativePath->Buffer;
    Last = &RelativePath->Buffer[RelativePath->Length / sizeof(WCHAR)];
    while (Current <= Last)
    {
        if (Current != Last && *Current != L'\\')
        {
            Current++;
            continue;
        }

        KeyName.MaximumLength = KeyName.Length = (USHORT)((ULONG_PTR)Current - (ULONG_PTR)KeyName.Buffer);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   Parent,
                                   NULL);
        Status = ZwCreateKey(&Key, KEY_CREATE_SUB_KEY, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
        ZwClose(Parent);
        if (!NT_SUCCESS(Status))
            return;

        Parent = Key;
        if (Current == Last)
            break;
        Current++;
        KeyName.Buffer = (PWSTR)Current;
    }
    ZwClose(Parent);
}

/**
 * @name IoOpenDeviceRegistryKey
 *
 * Open a registry key unique for a specified driver or device instance.
 *
 * @param DeviceObject   Device to get the registry key for.
 * @param DevInstKeyType Type of the key to return.
 * @param DesiredAccess  Access mask (eg. KEY_READ | KEY_WRITE).
 * @param DevInstRegKey  Handle to the opened registry key on
 *                       successful return.
 *
 * @return Status.
 *
 * @implemented
 */
NTSTATUS
NTAPI
IoOpenDeviceRegistryKey(IN PDEVICE_OBJECT DeviceObject,
                        IN ULONG DevInstKeyType,
                        IN ACCESS_MASK DesiredAccess,
                        OUT PHANDLE DevInstRegKey)
{
    static WCHAR RootKeyName[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\";
    static WCHAR ProfileKeyName[] =
        L"Hardware Profiles\\Current\\System\\CurrentControlSet\\";
    static WCHAR ClassKeyName[] = L"Control\\Class\\";
    static WCHAR EnumKeyName[] = L"Enum\\";
    static WCHAR DeviceParametersKeyName[] = L"Device Parameters";
    ULONG KeyNameLength;
    PWSTR KeyNameBuffer;
    UNICODE_STRING KeyName;
    ULONG DriverKeyLength;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PDEVICE_NODE DeviceNode = NULL;
    NTSTATUS Status;

    DPRINT("IoOpenDeviceRegistryKey() called\n");

    if ((DevInstKeyType & (PLUGPLAY_REGKEY_DEVICE | PLUGPLAY_REGKEY_DRIVER)) == 0)
    {
        DPRINT1("IoOpenDeviceRegistryKey(): got wrong params, exiting...\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (!IopIsValidPhysicalDeviceObject(DeviceObject))
        return STATUS_INVALID_DEVICE_REQUEST;
    DeviceNode = IopGetDeviceNode(DeviceObject);

    /*
     * Calculate the length of the base key name. This is the full
     * name for driver key or the name excluding "Device Parameters"
     * subkey for device key.
     */

    KeyNameLength = sizeof(RootKeyName);
    if (DevInstKeyType & PLUGPLAY_REGKEY_CURRENT_HWPROFILE)
        KeyNameLength += sizeof(ProfileKeyName) - sizeof(UNICODE_NULL);
    if (DevInstKeyType & PLUGPLAY_REGKEY_DRIVER)
    {
        KeyNameLength += sizeof(ClassKeyName) - sizeof(UNICODE_NULL);
        Status = IoGetDeviceProperty(DeviceObject, DevicePropertyDriverKeyName,
                                     0, NULL, &DriverKeyLength);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            HANDLE InstanceKey;

            Status = IopCreateDeviceKeyPath(&DeviceNode->InstancePath, REG_OPTION_NON_VOLATILE, &InstanceKey);
            if (!NT_SUCCESS(Status))
                return Status;
            IopEnsureDeviceDriverKey(InstanceKey);
            ZwClose(InstanceKey);
            Status = IoGetDeviceProperty(DeviceObject, DevicePropertyDriverKeyName,
                                         0, NULL, &DriverKeyLength);
        }
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return Status;
        KeyNameLength += DriverKeyLength;
    }
    else
    {
        KeyNameLength += sizeof(EnumKeyName) - sizeof(UNICODE_NULL) +
                         DeviceNode->InstancePath.Length;
    }

    /*
     * Now allocate the buffer for the key name...
     */

    KeyNameBuffer = ExAllocatePool(PagedPool, KeyNameLength);
    if (KeyNameBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeyName.Length = 0;
    KeyName.MaximumLength = (USHORT)KeyNameLength;
    KeyName.Buffer = KeyNameBuffer;

    /*
     * ...and build the key name.
     */

    KeyName.Length += sizeof(RootKeyName) - sizeof(UNICODE_NULL);
    RtlCopyMemory(KeyNameBuffer, RootKeyName, KeyName.Length);

    if (DevInstKeyType & PLUGPLAY_REGKEY_CURRENT_HWPROFILE)
        RtlAppendUnicodeToString(&KeyName, ProfileKeyName);

    if (DevInstKeyType & PLUGPLAY_REGKEY_DRIVER)
    {
        RtlAppendUnicodeToString(&KeyName, ClassKeyName);
        Status = IoGetDeviceProperty(DeviceObject, DevicePropertyDriverKeyName,
                                     DriverKeyLength, KeyNameBuffer +
                                     (KeyName.Length / sizeof(WCHAR)),
                                     &DriverKeyLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Call to IoGetDeviceProperty() failed with Status 0x%08lx\n", Status);
            ExFreePool(KeyNameBuffer);
            return Status;
        }
        KeyName.Length += (USHORT)DriverKeyLength - sizeof(UNICODE_NULL);
    }
    else
    {
        RtlAppendUnicodeToString(&KeyName, EnumKeyName);
        Status = RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->InstancePath);
        if (DeviceNode->InstancePath.Length == 0)
        {
            ExFreePool(KeyNameBuffer);
            return Status;
        }
    }

    if ((DevInstKeyType & (PLUGPLAY_REGKEY_CURRENT_HWPROFILE | PLUGPLAY_REGKEY_DRIVER)) == PLUGPLAY_REGKEY_CURRENT_HWPROFILE)
    {
        static const WCHAR CurrentProfile[] = L"Hardware Profiles\\Current";
        UNICODE_STRING ProfileRoot, Relative;

        ProfileRoot.Buffer = KeyNameBuffer;
        ProfileRoot.Length = sizeof(RootKeyName) - sizeof(UNICODE_NULL) +
                             sizeof(CurrentProfile) - sizeof(UNICODE_NULL);
        ProfileRoot.MaximumLength = ProfileRoot.Length;
        Relative.Buffer = KeyNameBuffer + ProfileRoot.Length / sizeof(WCHAR) + 1;
        Relative.Length = KeyName.Length - ProfileRoot.Length - sizeof(WCHAR);
        Relative.MaximumLength = Relative.Length;
        IopCreateKeyPath(&ProfileRoot, &Relative);
    }

    /*
     * Open the base key.
     */
    Status = IopOpenRegistryKeyEx(DevInstRegKey, NULL, &KeyName, DesiredAccess);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoOpenDeviceRegistryKey(%wZ): Base key doesn't exist, exiting... (Status 0x%08lx)\n", &KeyName, Status);
        ExFreePool(KeyNameBuffer);
        return Status;
    }
    ExFreePool(KeyNameBuffer);

    /*
     * For driver key we're done now.
     */

    if (DevInstKeyType & (PLUGPLAY_REGKEY_DRIVER | PLUGPLAY_REGKEY_CURRENT_HWPROFILE))
        return Status;

    /*
     * Let's go further. For device key we must open "Device Parameters"
     * subkey and create it if it doesn't exist yet.
     */

    RtlInitUnicodeString(&KeyName, DeviceParametersKeyName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               *DevInstRegKey,
                               NULL);
    Status = ZwCreateKey(DevInstRegKey,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    ZwClose(ObjectAttributes.RootDirectory);

    return Status;
}

/*
 * @implemented
 */
VOID
NTAPI
IoInvalidateDeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN DEVICE_RELATION_TYPE Type)
{
    if (!IopIsValidPhysicalDeviceObject(DeviceObject))
    {
        KeBugCheckEx(PNP_DETECTED_FATAL_ERROR, 0x2, (ULONG_PTR)DeviceObject, 0, 0);
    }

    switch (Type)
    {
        case BusRelations:
            /* Enumerate the device */
            PiQueueDeviceAction(DeviceObject, PiActionEnumDeviceTree, NULL, NULL);
            break;
        default:
            /* Everything else is not implemented */
            break;
    }
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoSynchronousInvalidateDeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN DEVICE_RELATION_TYPE Type)
{
    PAGED_CODE();

    if (!IopIsValidPhysicalDeviceObject(DeviceObject))
    {
        KeBugCheckEx(PNP_DETECTED_FATAL_ERROR, 0x2, (ULONG_PTR)DeviceObject, 0, 0);
    }

    switch (Type)
    {
        case BusRelations:
            /* Enumerate the device */
            return PiPerformSyncDeviceAction(DeviceObject, PiActionEnumDeviceTree);
        case PowerRelations:
             /* Not handled yet */
             return STATUS_NOT_IMPLEMENTED;
        case TargetDeviceRelation:
            /* Nothing to do */
            return STATUS_SUCCESS;
        default:
            /* Ejection relations are not supported */
            return STATUS_NOT_SUPPORTED;
    }
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
IoTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                      IN ULONG BusNumber,
                      IN PHYSICAL_ADDRESS BusAddress,
                      IN OUT PULONG AddressSpace,
                      OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    /* FIXME: Notify the resource arbiter */

    return HalTranslateBusAddress(InterfaceType,
                                  BusNumber,
                                  BusAddress,
                                  AddressSpace,
                                  TranslatedAddress);
}

VOID
NTAPI
IoInvalidateDeviceState(
    IN PDEVICE_OBJECT DeviceObject)
{
    if (!IopIsValidPhysicalDeviceObject(DeviceObject))
    {
        KeBugCheckEx(PNP_DETECTED_FATAL_ERROR, 0x2, (ULONG_PTR)DeviceObject, 0, 0);
    }

    PiQueueDeviceAction(DeviceObject, PiActionQueryState, NULL, NULL);
}
