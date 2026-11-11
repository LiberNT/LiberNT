/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the registry into being, hive by hive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

/*
 * The hives the machine itself is made of. The hardware hive has no file
 * because what it says is read out of the firmware on every boot, and the
 * system hive is already in memory because the boot loader read it.
 */
HIVE_LIST_ENTRY CmpMachineHiveList[CM_NUMBER_OF_MACHINE_HIVES] =
{
    {
        L"\\REGISTRY\\MACHINE\\HARDWARE",
        NULL,
        NULL,
        HIVE_VOLATILE,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    },
    {
        L"\\REGISTRY\\MACHINE\\SYSTEM",
        L"\\SystemRoot\\System32\\Config\\SYSTEM",
        NULL,
        0,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    },
    {
        L"\\REGISTRY\\MACHINE\\SOFTWARE",
        L"\\SystemRoot\\System32\\Config\\SOFTWARE",
        NULL,
        0,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    },
    {
        L"\\REGISTRY\\MACHINE\\SECURITY",
        L"\\SystemRoot\\System32\\Config\\SECURITY",
        NULL,
        0,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    },
    {
        L"\\REGISTRY\\MACHINE\\SAM",
        L"\\SystemRoot\\System32\\Config\\SAM",
        NULL,
        0,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    },
    {
        L"\\REGISTRY\\USER\\.DEFAULT",
        L"\\SystemRoot\\System32\\Config\\DEFAULT",
        NULL,
        0,
        0,
        NULL,
        FALSE,
        FALSE,
        FALSE
    }
};

/* PRIVATE DATA ***************************************************************/

static BOOLEAN CmpHiveListLoaded = FALSE;

static UNICODE_STRING CmpMachineName =
    RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE");
static UNICODE_STRING CmpUserName =
    RTL_CONSTANT_STRING(L"\\REGISTRY\\USER");
static UNICODE_STRING CmpRegistryRootName =
    RTL_CONSTANT_STRING(L"\\REGISTRY");
static UNICODE_STRING CmpRegistryRootKeyName =
    RTL_CONSTANT_STRING(L"REGISTRY");

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
CmpCreateStartupKey(
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Options,
    _Out_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               CmpHiveRootSecurityDescriptor());

    return ZwCreateKey(KeyHandle,
                       KEY_ALL_ACCESS,
                       &ObjectAttributes,
                       0,
                       NULL,
                       Options,
                       NULL);
}

static
NTSTATUS
CmpCreateVolatileKey(
    _In_ PCUNICODE_STRING Name,
    _Out_ PHANDLE KeyHandle)
{
    return CmpCreateStartupKey(Name, REG_OPTION_VOLATILE, KeyHandle);
}

/* Every component of a path has to be there before the last one can be made */
static
NTSTATUS
CmpCreateVolatilePath(
    _In_ PCWSTR Path,
    _Out_opt_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    HANDLE Local;
    NTSTATUS Status;

    if (KeyHandle != NULL) *KeyHandle = NULL;

    RtlInitUnicodeString(&Name, Path);

    Status = CmpCreateVolatileKey(&Name, &Local);
    if (!NT_SUCCESS(Status)) return Status;

    if (KeyHandle != NULL)
    {
        *KeyHandle = Local;
        return Status;
    }

    ZwClose(Local);
    return Status;
}

static
VOID
CmpWriteStringValue(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ PCUNICODE_STRING Value)
{
    UNICODE_STRING Name;

    if (Value->Length == 0) return;

    RtlInitUnicodeString(&Name, ValueName);
    ZwSetValueKey(KeyHandle,
                  &Name,
                  0,
                  REG_SZ,
                  Value->Buffer,
                  Value->Length + sizeof(WCHAR));
}

static
VOID
CmpWriteNumberAsString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value)
{
    UNICODE_STRING Text;
    WCHAR Buffer[16];

    Text.Buffer = Buffer;
    Text.Length = 0;
    Text.MaximumLength = sizeof(Buffer);

    if (!NT_SUCCESS(RtlIntegerToUnicodeString(Value, 10, &Text))) return;

    CmpWriteStringValue(KeyHandle, ValueName, &Text);
}

/* The hive owns every string it holds, because it outlives this routine */
static
VOID
CmpCopyPathInto(
    _Out_ PUNICODE_STRING Destination,
    _In_ PCUNICODE_STRING Source)
{
    PWCHAR Buffer;

    Destination->Buffer = NULL;
    Destination->Length = 0;
    Destination->MaximumLength = 0;

    Buffer = ExAllocatePoolZero(PagedPool,
                                Source->Length + sizeof(WCHAR),
                                TAG_CM);
    if (Buffer == NULL) return;

    RtlCopyMemory(Buffer, Source->Buffer, Source->Length);

    Destination->Buffer = Buffer;
    Destination->Length = Source->Length;
    Destination->MaximumLength = Source->Length + sizeof(WCHAR);
}

/*
 * Gives a loaded hive the file it came from, so that what changes in it from
 * now on can be written back.
 */
static
NTSTATUS
CmpAttachHiveFile(
    _In_ PCMHIVE Hive,
    _In_ PCUNICODE_STRING FileName)
{
    ULONG PrimaryDisposition = 0;
    ULONG LogDisposition = 0;
    HANDLE Primary = NULL;
    HANDLE Log = NULL;
    NTSTATUS Status;

    /* The hive is already in memory, so only its log may be missing and made */
    Status = CmpOpenHiveFiles(FileName,
                              NULL,
                              &Primary,
                              &Log,
                              &PrimaryDisposition,
                              &LogDisposition,
                              TRUE,
                              TRUE,
                              FALSE,
                              NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Hive->FileHandles[HFILE_TYPE_PRIMARY] = Primary;
    Hive->FileHandles[HFILE_TYPE_LOG] = Log;

    if (Hive->Hive.BaseBlock->BootRecover == HBOOT_BOOT_RECOVERED_BY_HIVE_LOG)
    {
        RtlSetAllBits(&Hive->Hive.DirtyVector);
        Hive->Hive.DirtyCount = Hive->Hive.DirtyVector.SizeOfBitMap;
        if (HvSyncHiveFromRecover(&Hive->Hive))
        {
            RtlClearAllBits(&Hive->Hive.DirtyVector);
            Hive->Hive.DirtyCount = 0;
            Hive->Hive.BaseBlock->BootRecover = HBOOT_NO_BOOT_RECOVER;
        }
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The key every registry path starts from. It lives in the volatile master
 * hive and is the only key whose name the object manager itself knows.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpCreateRegistryRoot(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_BODY KeyBody;
    PCM_KEY_NODE Node;
    HCELL_INDEX RootCell;
    NTSTATUS Status;

    RootCell = CmiVolatileHive->Hive.BaseBlock->RootCell;

    InitializeObjectAttributes(&ObjectAttributes,
                               &CmpRegistryRootName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               CmpHiveRootSecurityDescriptor());

    Status = ObCreateObject(KernelMode,
                            CmpKeyObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(CM_KEY_BODY),
                            0,
                            0,
                            (PVOID*)&KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    Node = (PCM_KEY_NODE)HvGetCell(&CmiVolatileHive->Hive, RootCell);
    if (Node == NULL)
    {
        ObDereferenceObject(KeyBody);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Kcb = CmpCreateKeyControlBlock(&CmiVolatileHive->Hive,
                                   RootCell,
                                   Node,
                                   NULL,
                                   0,
                                   &CmpRegistryRootKeyName);
    HvReleaseCell(&CmiVolatileHive->Hive, RootCell);

    if (Kcb == NULL)
    {
        ObDereferenceObject(KeyBody);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(KeyBody, sizeof(CM_KEY_BODY));

    KeyBody->Type = CM_KEY_BODY_TYPE;
    KeyBody->KeyControlBlock = Kcb;
    KeyBody->ProcessID = PsGetCurrentProcessId();
    InitializeListHead(&KeyBody->ContextListHead);
    InitializeListHead(&KeyBody->KeyBodyList);
    EnlistKeyBodyWithKCB(KeyBody, 0);

    /* One reference belongs to the key body, one keeps the root in the hive */
    if (!CmpReferenceKeyControlBlock(Kcb))
    {
        ObDereferenceObject(KeyBody);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    CmiVolatileHive->RootKcb = Kcb;

    Status = ObInsertObject(KeyBody,
                            NULL,
                            KEY_ALL_ACCESS,
                            0,
                            NULL,
                            &CmpRegistryRootHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(CmpRegistryRootHandle,
                                       KEY_READ,
                                       CmpKeyObjectType,
                                       KernelMode,
                                       (PVOID*)&CmpRegistryRootObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    CmpRegistryNamespaceRoot = CmpRegistryRootObject;
    return STATUS_SUCCESS;
}

/*
 * The system hive is the one hive that cannot be read from a file when it is
 * needed, because the file system is not running yet. The boot loader reads
 * it and the kernel takes it over where it lies.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpInitializeSystemHive(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING HiveName;
    PCMHIVE SystemHive;
    NTSTATUS Status;

    if (LoaderBlock->RegistryBase == NULL) return FALSE;

    Status = CmpInitializeHive(&SystemHive,
                               HINIT_MEMORY,
                               0,
                               HFILE_TYPE_PRIMARY,
                               LoaderBlock->RegistryBase,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               &CmpSystemFileName,
                               CM_CHECK_REGISTRY_PURGE_VOLATILES);
    if (!NT_SUCCESS(Status)) return FALSE;

    CmpMachineHiveList[CMP_SYSTEM_HIVE].CmHive = SystemHive;

    RtlInitUnicodeString(&HiveName,
                         CmpMachineHiveList[CMP_SYSTEM_HIVE].Name);

    Status = CmpLinkHiveToMaster(&HiveName,
                                 NULL,
                                 SystemHive,
                                 FALSE,
                                 CmpHiveRootSecurityDescriptor());
    if (!NT_SUCCESS(Status)) return FALSE;

    return TRUE;
}

/*
 * Points the current control set at the one the select key named, so that
 * every path that says CurrentControlSet lands in the right place.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpCreateControlSet(
    _In_ PHHIVE Hive)
{
    UNICODE_STRING CurrentName = RTL_CONSTANT_STRING(L"Current");
    UNICODE_STRING Target;
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet");
    UNICODE_STRING ValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HCELL_INDEX ControlSet;
    PCM_KEY_NODE Node;
    BOOLEAN AutoSelect;
    WCHAR Buffer[64];
    HANDLE KeyHandle;
    NTSTATUS Status;
    USHORT Length;

    ControlSet = CmpFindControlSet(Hive,
                                   Hive->BaseBlock->RootCell,
                                   &CurrentName,
                                   &AutoSelect);
    if (ControlSet == HCELL_NIL) return STATUS_OBJECT_NAME_NOT_FOUND;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, ControlSet);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Target.Buffer = Buffer;
    Target.Length = 0;
    Target.MaximumLength = sizeof(Buffer);

    RtlAppendUnicodeToString(&Target, L"\\REGISTRY\\MACHINE\\SYSTEM\\");

    Length = (Node->Flags & KEY_COMP_NAME)
                 ? (USHORT)(Node->NameLength * sizeof(WCHAR))
                 : Node->NameLength;

    if (Target.Length + Length > Target.MaximumLength)
    {
        HvReleaseCell(Hive, ControlSet);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Node->Flags & KEY_COMP_NAME)
    {
        CmpCopyCompressedName(&Target.Buffer[Target.Length / sizeof(WCHAR)],
                              Length,
                              Node->Name,
                              Node->NameLength);
    }
    else
    {
        RtlCopyMemory(&Target.Buffer[Target.Length / sizeof(WCHAR)],
                      Node->Name,
                      Length);
    }

    Target.Length += Length;
    HvReleaseCell(Hive, ControlSet);

    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateKey(&KeyHandle,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK,
                         NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ValueName = CmSymbolicLinkValueName;

    Status = ZwSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_LINK,
                           Target.Buffer,
                           Target.Length);

    ZwClose(KeyHandle);
    return Status;
}

static
NTSTATUS
CmpCreateBootLink(
    _In_ PCWSTR Link,
    _In_ PCWSTR Target)
{
    UNICODE_STRING ValueName = CmSymbolicLinkValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE KeyHandle;
    ULONG Disposition;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, Link);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_OPENLINK,
                               NULL,
                               NULL);

    Status = ZwCreateKey(&KeyHandle,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK,
                         &Disposition);
    if (!NT_SUCCESS(Status)) return Status;

    if (Disposition != REG_CREATED_NEW_KEY)
    {
        ZwClose(KeyHandle);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    Status = ZwSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_LINK,
                           (PVOID)Target,
                           (ULONG)(wcslen(Target) * sizeof(WCHAR)));

    ZwClose(KeyHandle);
    return Status;
}

CODE_SEG("INIT")
static
NTSTATUS
CmpCreateCurrentProfileLink(VOID)
{
    UNICODE_STRING ConfigName = RTL_CONSTANT_STRING(
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"CurrentConfig");
    UCHAR ValueBuffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    WCHAR Target[96];
    HANDLE KeyHandle;
    ULONG Length;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &ConfigName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             Value,
                             sizeof(ValueBuffer),
                             &Length);
    ZwClose(KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    if (Value->Type != REG_DWORD || Value->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;

    Status = RtlStringCchPrintfW(Target,
                                 RTL_NUMBER_OF(Target),
                                 L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Hardware Profiles\\%04lu",
                                 *(PULONG)Value->Data);
    if (!NT_SUCCESS(Status)) return Status;

    return CmpCreateBootLink(L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current",
                             Target);
}

static
VOID
CmpLinkWow64SharedKey(
    _In_ PCWSTR Name)
{
    WCHAR Link[256];
    WCHAR Target[256];
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE KeyHandle;
    PWCHAR Separator;
    SIZE_T PrefixLength;
    NTSTATUS Status;

    if (!NT_SUCCESS(RtlStringCchPrintfW(Target, RTL_NUMBER_OF(Target),
                                        L"\\REGISTRY\\MACHINE\\SOFTWARE\\%ls", Name)) ||
        !NT_SUCCESS(RtlStringCchPrintfW(Link, RTL_NUMBER_OF(Link),
                                        L"\\REGISTRY\\MACHINE\\SOFTWARE\\Wow6432Node\\%ls", Name)))
    {
        return;
    }

    RtlInitUnicodeString(&KeyName, Target);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    if (!NT_SUCCESS(ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes))) return;
    ZwClose(KeyHandle);

    PrefixLength = wcslen(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Wow6432Node\\");
    for (Separator = wcschr(&Link[PrefixLength], OBJ_NAME_PATH_SEPARATOR);
         Separator != NULL;
         Separator = wcschr(Separator + 1, OBJ_NAME_PATH_SEPARATOR))
    {
        *Separator = UNICODE_NULL;
        RtlInitUnicodeString(&KeyName, Link);
        Status = ZwCreateKey(&KeyHandle,
                             KEY_CREATE_SUB_KEY,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
        *Separator = OBJ_NAME_PATH_SEPARATOR;
        if (!NT_SUCCESS(Status)) return;
        ZwClose(KeyHandle);
    }

    CmpCreateBootLink(Link, Target);
}

/* The shared keys are those of the Microsoft documentation "Registry Keys Affected by WOW64" */
static
VOID
CmpCreateWow64Links(VOID)
{
    static const PCWSTR Parents[] =
    {
        L"\\REGISTRY\\MACHINE\\SOFTWARE\\Wow6432Node",
        L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Wow6432Node",
    };
    static const PCWSTR SharedClasses[] =
    {
        L"AppID",
        L"Protocols",
        L"TypeLib",
    };
    static const PCWSTR SharedKeys[] =
    {
        L"Clients",
        L"Microsoft\\COM3",
        L"Microsoft\\Cryptography\\Calais\\Current",
        L"Microsoft\\Cryptography\\Calais\\Readers",
        L"Microsoft\\Cryptography\\Services",
        L"Microsoft\\CTF\\SystemShared",
        L"Microsoft\\CTF\\TIP",
        L"Microsoft\\DFS",
        L"Microsoft\\Driver Signing",
        L"Microsoft\\EnterpriseCertificates",
        L"Microsoft\\EventSystem",
        L"Microsoft\\MSMQ",
        L"Microsoft\\Non-Driver Signing",
        L"Microsoft\\Notepad\\DefaultFonts",
        L"Microsoft\\OLE",
        L"Microsoft\\RAS",
        L"Microsoft\\RPC",
        L"Microsoft\\SystemCertificates",
        L"Microsoft\\TermServLicensing",
        L"Microsoft\\Transaction Server",
        L"Microsoft\\Windows\\CurrentVersion\\App Paths",
        L"Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes",
        L"Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
        L"Microsoft\\Windows\\CurrentVersion\\Explorer\\DriveIcons",
        L"Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
        L"Microsoft\\Windows\\CurrentVersion\\Group Policy",
        L"Microsoft\\Windows\\CurrentVersion\\Policies",
        L"Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
        L"Microsoft\\Windows\\CurrentVersion\\Setup",
        L"Microsoft\\Windows\\CurrentVersion\\Telephony\\Locations",
        L"Microsoft\\Windows NT\\CurrentVersion\\Console",
        L"Microsoft\\Windows NT\\CurrentVersion\\FontDpi",
        L"Microsoft\\Windows NT\\CurrentVersion\\FontLink",
        L"Microsoft\\Windows NT\\CurrentVersion\\FontMapper",
        L"Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        L"Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
        L"Microsoft\\Windows NT\\CurrentVersion\\Gre_Initialize",
        L"Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
        L"Microsoft\\Windows NT\\CurrentVersion\\LanguagePack",
        L"Microsoft\\Windows NT\\CurrentVersion\\NetworkCards",
        L"Microsoft\\Windows NT\\CurrentVersion\\Perflib",
        L"Microsoft\\Windows NT\\CurrentVersion\\Ports",
        L"Microsoft\\Windows NT\\CurrentVersion\\Print",
        L"Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
        L"Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
        L"Policies",
        L"RegisteredApplications",
    };
    WCHAR Link[128];
    WCHAR Target[128];
    UNICODE_STRING Name;
    HANDLE KeyHandle;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Parents); i++)
    {
        RtlInitUnicodeString(&Name, Parents[i]);
        if (!NT_SUCCESS(CmpCreateStartupKey(&Name, REG_OPTION_NON_VOLATILE, &KeyHandle))) return;
        ZwClose(KeyHandle);
    }

    CmpCreateBootLink(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Wow6432Node\\Classes",
                      L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Wow6432Node");

    for (i = 0; i < RTL_NUMBER_OF(SharedClasses); i++)
    {
        if (NT_SUCCESS(RtlStringCchPrintfW(Link, RTL_NUMBER_OF(Link),
                                           L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Wow6432Node\\%ls",
                                           SharedClasses[i])) &&
            NT_SUCCESS(RtlStringCchPrintfW(Target, RTL_NUMBER_OF(Target),
                                           L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\%ls",
                                           SharedClasses[i])))
        {
            CmpCreateBootLink(Link, Target);
        }
    }

    for (i = 0; i < RTL_NUMBER_OF(SharedKeys); i++)
        CmpLinkWow64SharedKey(SharedKeys[i]);
}

/*
 * Writes down how the system was started, which is the only record of the
 * boot options once the loader is gone.
 */
CODE_SEG("INIT")
VOID
NTAPI
CmpSetSystemValues(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING ControlPath = RTL_CONSTANT_STRING(
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control");
    UNICODE_STRING BootDevice;
    ANSI_STRING Narrow;
    HANDLE KeyHandle;
    NTSTATUS Status;

    Status = CmpCreateVolatileKey(&ControlPath, &KeyHandle);
    if (!NT_SUCCESS(Status)) return;

    CmpWriteStringValue(KeyHandle,
                        L"SystemStartOptions",
                        &CmpSystemStartOptions);

    if (LoaderBlock->ArcBootDeviceName != NULL)
    {
        RtlInitAnsiString(&Narrow, LoaderBlock->ArcBootDeviceName);

        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&BootDevice,
                                                    &Narrow,
                                                    TRUE)))
        {
            CmpWriteStringValue(KeyHandle, L"SystemBootDevice", &BootDevice);
            RtlFreeUnicodeString(&BootDevice);
        }
    }

    ZwClose(KeyHandle);
}

/*
 * What the system says about itself: the version the rest of the world reads
 * out of the software hive.
 */
VOID
NTAPI
CmpSetVersionData(VOID)
{
    UNICODE_STRING VersionPath = RTL_CONSTANT_STRING(
        L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    UNICODE_STRING BuildLab;
    UNICODE_STRING Minor;
    UNICODE_STRING Text;
    ANSI_STRING Narrow;
    WCHAR MinorBuffer[16];
    WCHAR Buffer[64];
    HANDLE KeyHandle;
    NTSTATUS Status;

    Status = CmpCreateStartupKey(&VersionPath,
                                 REG_OPTION_NON_VOLATILE,
                                 &KeyHandle);
    if (!NT_SUCCESS(Status)) return;

    Text.Buffer = Buffer;
    Text.Length = 0;
    Text.MaximumLength = sizeof(Buffer);

    Minor.Buffer = MinorBuffer;
    Minor.Length = 0;
    Minor.MaximumLength = sizeof(MinorBuffer);

    if (NT_SUCCESS(RtlIntegerToUnicodeString(NtMajorVersion, 10, &Text)) &&
        NT_SUCCESS(RtlIntegerToUnicodeString(NtMinorVersion, 10, &Minor)))
    {
        RtlAppendUnicodeToString(&Text, L".");
        RtlAppendUnicodeStringToString(&Text, &Minor);
        CmpWriteStringValue(KeyHandle, L"CurrentVersion", &Text);
    }

    CmpWriteNumberAsString(KeyHandle, L"CurrentBuildNumber", NtBuildNumber);
    CmpWriteNumberAsString(KeyHandle, L"CurrentBuild", NtBuildNumber);

    RtlInitAnsiString(&Narrow, NtBuildLab);

    if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&BuildLab, &Narrow, TRUE)))
    {
        CmpWriteStringValue(KeyHandle, L"BuildLab", &BuildLab);
        RtlFreeUnicodeString(&BuildLab);
    }

    if (CmCSDVersionString.Length != 0)
    {
        CmpWriteStringValue(KeyHandle, L"CSDVersion", &CmCSDVersionString);
    }

    CmpWriteStringValue(KeyHandle, L"SystemRoot", &NtSystemRoot);

    ZwClose(KeyHandle);
}

/*
 * Reads in every hive the machine is made of that is not already in memory.
 * Only possible once a file system is running, so it is asked for again from
 * more than one place and does its work the first time only.
 */
VOID
NTAPI
CmpInitializeHiveList(VOID)
{
    PHIVE_LIST_ENTRY Entry;
    UNICODE_STRING HiveName;
    UNICODE_STRING FileName;
    PCMHIVE Hive;
    HCELL_INDEX RootCell;
    BOOLEAN Allocate;
    NTSTATUS Status;
    ULONG i;

    if (CmpHiveListLoaded) return;

    ExAcquirePushLockExclusive(&CmpLoadHiveLock);

    if (CmpHiveListLoaded)
    {
        ExReleasePushLockExclusive(&CmpLoadHiveLock);
        return;
    }

    CmpHiveListLoaded = TRUE;

    for (i = 0; i < CM_NUMBER_OF_MACHINE_HIVES; i++)
    {
        Entry = &CmpMachineHiveList[i];
        RtlInitUnicodeString(&HiveName, Entry->Name);

        if (i == CMP_SYSTEM_HIVE)
        {
            if (Entry->CmHive == NULL) continue;

            RtlInitUnicodeString(&FileName, Entry->BaseName);
            CmpAttachHiveFile(Entry->CmHive, &FileName);
            CmpCopyPathInto(&Entry->CmHive->HiveRootPath, &HiveName);
            CmpCopyPathInto(&Entry->CmHive->FileFullPath, &FileName);
            CmpAddToHiveFileList(Entry->CmHive);
            CmpNotifyMachineHiveLoaded(&HiveName, Entry->CmHive);
            continue;
        }

        if (Entry->BaseName == NULL) continue;

        Allocate = TRUE;
        RtlInitUnicodeString(&FileName, Entry->BaseName);

        Status = CmpInitHiveFromFile(&FileName,
                                     Entry->HHiveFlags,
                                     &Hive,
                                     &Allocate,
                                     CM_CHECK_REGISTRY_VALIDATE_HIVE |
                                     CM_CHECK_REGISTRY_PURGE_VOLATILES);
        if (!NT_SUCCESS(Status)) continue;

        if (Allocate && !CmpCreateRootNode(&Hive->Hive, L"", &RootCell))
        {
            CmpDestroyHive(Hive);
            continue;
        }

        Hive->Flags |= Entry->CmHiveFlags;
        Entry->CmHive = Hive;

        Status = CmpLinkHiveToMaster(&HiveName,
                                     NULL,
                                     Hive,
                                     FALSE,
                                     CmpHiveRootSecurityDescriptor());
        if (!NT_SUCCESS(Status))
        {
            Entry->CmHive = NULL;
            CmpDestroyHive(Hive);
            continue;
        }

        RtlInitUnicodeString(&FileName, Entry->BaseName);
        CmpCopyPathInto(&Hive->HiveRootPath, &HiveName);
        CmpCopyPathInto(&Hive->FileFullPath, &FileName);

        CmpAddToHiveFileList(Hive);
        CmpNotifyMachineHiveLoaded(&HiveName, Hive);
    }

    ExReleasePushLockExclusive(&CmpLoadHiveLock);

    if (sizeof(PVOID) == sizeof(ULONGLONG)) CmpCreateWow64Links();

    CmpSetVersionData();
}

/*
 * The drivers the control set says to start at this point of the boot, in the
 * order it wants them, as registry paths the loader can be pointed at.
 */
CODE_SEG("INIT")
PUNICODE_STRING*
NTAPI
CmGetSystemDriverList(VOID)
{
    UNICODE_STRING CurrentName = RTL_CONSTANT_STRING(L"Current");
    PBOOT_DRIVER_NODE DriverNode;
    PUNICODE_STRING *Array;
    PUNICODE_STRING Name;
    HCELL_INDEX ControlSet;
    LIST_ENTRY DriverList;
    BOOLEAN AutoSelect;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Index = 0;
    PHHIVE Hive;

    if (CmpMachineHiveList[CMP_SYSTEM_HIVE].CmHive == NULL) return NULL;

    Hive = &CmpMachineHiveList[CMP_SYSTEM_HIVE].CmHive->Hive;

    ControlSet = CmpFindControlSet(Hive,
                                   Hive->BaseBlock->RootCell,
                                   &CurrentName,
                                   &AutoSelect);
    if (ControlSet == HCELL_NIL) return NULL;

    InitializeListHead(&DriverList);

    if (!CmpFindDrivers(Hive, ControlSet, SystemLoad, NULL, &DriverList))
    {
        return NULL;
    }

    CmpSortDriverList(Hive, ControlSet, &DriverList);
    CmpResolveDriverDependencies(&DriverList);

    for (Entry = DriverList.Flink; Entry != &DriverList; Entry = Entry->Flink)
    {
        Count++;
    }

    Array = ExAllocatePoolZero(NonPagedPool,
                               (Count + 1) * sizeof(PUNICODE_STRING),
                               TAG_CM);
    if (Array == NULL)
    {
        CmpFreeDriverList(Hive, &DriverList);
        return NULL;
    }

    for (Entry = DriverList.Flink; Entry != &DriverList; Entry = Entry->Flink)
    {
        DriverNode = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);

        Name = ExAllocatePoolZero(NonPagedPool,
                                  sizeof(UNICODE_STRING),
                                  TAG_CM);
        if (Name == NULL) break;

        if (!NT_SUCCESS(RtlDuplicateUnicodeString(
                RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                &DriverNode->ListEntry.RegistryPath,
                Name)))
        {
            ExFreePool(Name);
            break;
        }

        Array[Index] = Name;
        Index++;
    }

    Array[Index] = NULL;

    CmpFreeDriverList(Hive, &DriverList);
    return Array;
}

/*
 * The whole registry comes into being here: its lock, its caches, its object
 * type, the volatile hive that holds the namespace together, and the system
 * hive the boot loader left in memory.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
CmInitSystem1(VOID)
{
    HCELL_INDEX RootCell;
    PCMHIVE Master;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (ExpInTextModeSetup) CmpShareSystemHives = TRUE;
    if (InitIsWinPEMode) CmpMiniNTBoot = TRUE;

    CmpSystemProcess = PsGetCurrentProcess();

    if (KeLoaderBlock != NULL && KeLoaderBlock->LoadOptions != NULL)
    {
        ANSI_STRING Options;

        RtlInitAnsiString(&Options, KeLoaderBlock->LoadOptions);

        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&CmpLoadOptions,
                                                    &Options,
                                                    TRUE)))
        {
            CmpSystemStartOptions = CmpLoadOptions;
        }
    }

    CmpInitRegistryLock();
    CmpInitCmPrivateAlloc();
    CmpInitCmPrivateDelayAlloc();
    CmpInitializeCache();
    CmpInitializeDelayedCloseTable();
    CmpInitDelayDerefKCBEngine();
    CmpInitCallback();
    CmpInitNotify();
    CmpInitAppHives();
    CmpInitTransactions();

    Status = CmpCreateObjectTypes();
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = CmpInitializeHive(&Master,
                               HINIT_CREATE,
                               HIVE_VOLATILE,
                               HFILE_TYPE_PRIMARY,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               0);
    if (!NT_SUCCESS(Status)) return FALSE;

    if (!CmpCreateRootNode(&Master->Hive, L"REGISTRY", &RootCell))
    {
        return FALSE;
    }

    CmiVolatileHive = Master;

    Status = CmpCreateRegistryRoot();
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = CmpCreateVolatileKey(&CmpMachineName, &KeyHandle);
    if (!NT_SUCCESS(Status)) return FALSE;
    ZwClose(KeyHandle);

    Status = CmpCreateVolatileKey(&CmpUserName, &KeyHandle);
    if (!NT_SUCCESS(Status)) return FALSE;
    ZwClose(KeyHandle);

    /* TODO: the hardware description lives in the master hive, so the hive
       list does not name it */
    if (!NT_SUCCESS(CmpCreateVolatilePath(L"\\REGISTRY\\MACHINE\\HARDWARE", NULL)) ||
        !NT_SUCCESS(CmpCreateVolatilePath(L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION", NULL)) ||
        !NT_SUCCESS(CmpCreateVolatilePath(L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\SYSTEM", NULL)) ||
        !NT_SUCCESS(CmpCreateVolatilePath(L"\\REGISTRY\\MACHINE\\HARDWARE\\DEVICEMAP", NULL)) ||
        !NT_SUCCESS(CmpCreateVolatilePath(L"\\REGISTRY\\MACHINE\\HARDWARE\\RESOURCEMAP", NULL)))
    {
        return FALSE;
    }

    if (!CmpInitializeSystemHive(KeLoaderBlock)) return FALSE;

    Status = CmpCreateControlSet(
        &CmpMachineHiveList[CMP_SYSTEM_HIVE].CmHive->Hive);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = CmpCreateCurrentProfileLink();
    if (!NT_SUCCESS(Status)) DPRINT1("CM: no current hardware profile link (0x%08lx)\n", Status);

    Status = CmpInitializeHardwareConfiguration(KeLoaderBlock);
    if (!NT_SUCCESS(Status)) return FALSE;

    CmpSetSystemValues(KeLoaderBlock);

    CmpCmdInit(ExpInTextModeSetup);
    return TRUE;
}
