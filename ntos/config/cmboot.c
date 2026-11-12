/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading out of a hive which drivers to load and in what order
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* The boot loader reads the same hive with none of the kernel around it */
#ifdef _BLDR_
#undef CODE_SEG
#define CODE_SEG(...)
#include <cmlib.h>
#include "internal/cmboot.h"

/* The loader keeps the safe-boot choice here; in the kernel it is the executive's */
ULONG InitSafeBootMode = 0;
#endif

/* PRIVATE DATA ***************************************************************/

#define CMP_DRIVER_PATH_PREFIX \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\"

#define CMP_DRIVER_FILE_PREFIX L"system32\\drivers\\"
#define CMP_DRIVER_FILE_SUFFIX L".sys"

static const WCHAR CmpControlSetPrefix[] = L"ControlSet";

/*
 * What one driver says it needs before it can start. BOOT_DRIVER_NODE has no
 * room for this and is shared with the boot loader, so it is kept beside the
 * list instead, built while the list is and thrown away with it.
 */
typedef struct _CM_DRIVER_DEPENDENCY
{
    PBOOT_DRIVER_NODE Node;
    PWCHAR Services;
    ULONG ServicesLength;
    PWCHAR Groups;
    ULONG GroupsLength;
} CM_DRIVER_DEPENDENCY, *PCM_DRIVER_DEPENDENCY;

#define CMP_DEPENDENCY_GROW 32

static PCM_DRIVER_DEPENDENCY CmpDriverDependencies = NULL;
static ULONG CmpDriverDependencyCount = 0;
static ULONG CmpDriverDependencySize = 0;

/* PRIVATE FUNCTIONS **********************************************************/

static
PCM_DRIVER_DEPENDENCY
CmpAddDependencyRecord(
    _In_ PBOOT_DRIVER_NODE Node)
{
    PCM_DRIVER_DEPENDENCY Table;
    ULONG Size;

    if (CmpDriverDependencyCount == CmpDriverDependencySize)
    {
        Size = CmpDriverDependencySize + CMP_DEPENDENCY_GROW;

        Table = CmpAllocate(Size * sizeof(CM_DRIVER_DEPENDENCY), TRUE, TAG_CM);
        if (Table == NULL) return NULL;

        RtlZeroMemory(Table, Size * sizeof(CM_DRIVER_DEPENDENCY));

        if (CmpDriverDependencies != NULL)
        {
            RtlCopyMemory(Table,
                          CmpDriverDependencies,
                          CmpDriverDependencyCount *
                              sizeof(CM_DRIVER_DEPENDENCY));
            CmpFree(CmpDriverDependencies, 0);
        }

        CmpDriverDependencies = Table;
        CmpDriverDependencySize = Size;
    }

    Table = &CmpDriverDependencies[CmpDriverDependencyCount];
    CmpDriverDependencyCount++;

    Table->Node = Node;
    return Table;
}

static
PCM_DRIVER_DEPENDENCY
CmpFindDependencyRecord(
    _In_ PBOOT_DRIVER_NODE Node)
{
    ULONG i;

    for (i = 0; i < CmpDriverDependencyCount; i++)
    {
        if (CmpDriverDependencies[i].Node == Node)
        {
            return &CmpDriverDependencies[i];
        }
    }

    return NULL;
}

static
VOID
CmpFreeDependencyRecords(VOID)
{
    ULONG i;

    for (i = 0; i < CmpDriverDependencyCount; i++)
    {
        if (CmpDriverDependencies[i].Services != NULL)
        {
            CmpFree(CmpDriverDependencies[i].Services, 0);
        }

        if (CmpDriverDependencies[i].Groups != NULL)
        {
            CmpFree(CmpDriverDependencies[i].Groups, 0);
        }
    }

    if (CmpDriverDependencies != NULL) CmpFree(CmpDriverDependencies, 0);

    CmpDriverDependencies = NULL;
    CmpDriverDependencyCount = 0;
    CmpDriverDependencySize = 0;
}

static
PWCHAR
CmpCopyMultiString(
    _In_opt_ PCELL_DATA Data,
    _In_ ULONG Length,
    _Out_ PULONG Copied)
{
    PWCHAR Buffer;

    *Copied = 0;

    if (Data == NULL || Length < sizeof(WCHAR)) return NULL;

    Buffer = CmpAllocate(Length + sizeof(WCHAR), TRUE, TAG_CM);
    if (Buffer == NULL) return NULL;

    RtlZeroMemory(Buffer, Length + sizeof(WCHAR));
    RtlCopyMemory(Buffer, Data, Length);

    *Copied = Length;
    return Buffer;
}

static
USHORT
CmpStringLength(
    _In_ PCWSTR String)
{
    USHORT Length = 0;

    while (String[Length] != UNICODE_NULL) Length++;
    return (USHORT)(Length * sizeof(WCHAR));
}

/* Finds one value under a key and hands back where its data sits in the hive */
static
PCELL_DATA
CmpReadValue(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Length,
    _Out_opt_ PULONG Type)
{
    UNICODE_STRING Name;
    PCM_KEY_VALUE Value;
    HCELL_INDEX Cell;
    PCELL_DATA Data;

    *Length = 0;
    if (Type != NULL) *Type = REG_NONE;

    RtlInitUnicodeString(&Name, ValueName);

    Cell = CmpFindValueByName(Hive, Node, &Name);
    if (Cell == HCELL_NIL) return NULL;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (Value == NULL) return NULL;

    if (Type != NULL) *Type = Value->Type;

    Data = CmpValueToData(Hive, Value, Length);
    HvReleaseCell(Hive, Cell);
    return Data;
}

static
BOOLEAN
CmpReadDword(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Result)
{
    PCELL_DATA Data;
    ULONG Length;

    *Result = 0;

    Data = CmpReadValue(Hive, Node, ValueName, &Length, NULL);
    if (Data == NULL || Length < sizeof(ULONG)) return FALSE;

    *Result = *(PULONG)Data;
    return TRUE;
}

static
BOOLEAN
CmpReadDwordByName(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ PCUNICODE_STRING ValueName,
    _Out_ PULONG Result)
{
    PCM_KEY_VALUE Value;
    PCELL_DATA Data;
    HCELL_INDEX Cell;
    ULONG Length;

    *Result = 0;

    Cell = CmpFindValueByName(Hive, Node, ValueName);
    if (Cell == HCELL_NIL) return FALSE;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (Value == NULL) return FALSE;

    Data = CmpValueToData(Hive, Value, &Length);
    HvReleaseCell(Hive, Cell);

    if (Data == NULL || Length < sizeof(ULONG)) return FALSE;

    *Result = *(PULONG)Data;
    return TRUE;
}

/* The name of a key as characters, however the hive chose to store it */
static
USHORT
CmpKeyNameLength(
    _In_ PCM_KEY_NODE Node)
{
    if (Node->Flags & KEY_COMP_NAME)
    {
        return (USHORT)(Node->NameLength * sizeof(WCHAR));
    }

    return Node->NameLength;
}

static
VOID
CmpReadKeyName(
    _In_ PCM_KEY_NODE Node,
    _Out_writes_bytes_(Size) PWCHAR Buffer,
    _In_ USHORT Size)
{
    if (Node->Flags & KEY_COMP_NAME)
    {
        CmpCopyCompressedName(Buffer, Size, Node->Name, Node->NameLength);
        return;
    }

    RtlCopyMemory(Buffer, Node->Name, Size);
}

static
BOOLEAN
CmpIsDriverServiceType(
    _In_ ULONG Type)
{
    return Type == SERVICE_KERNEL_DRIVER ||
           Type == SERVICE_FILE_SYSTEM_DRIVER ||
           Type == SERVICE_RECOGNIZER_DRIVER;
}

/*
 * Builds one driver node out of one service key. The node owns every string
 * it holds, laid out behind it in the same allocation.
 */
static
BOOLEAN
CmpAddDriverToList(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX DriverCell,
    _Inout_ PLIST_ENTRY DriverListHead)
{
    PBOOT_DRIVER_NODE DriverNode;
    PCM_KEY_NODE Node;
    PCELL_DATA ImagePath;
    PCELL_DATA Group;
    USHORT NameLength;
    USHORT GroupLength = 0;
    USHORT FileLength;
    USHORT RegistryLength;
    ULONG ImageLength = 0;
    ULONG GroupDataLength = 0;
    ULONG ErrorControl = SERVICE_ERROR_NORMAL;
    PCM_DRIVER_DEPENDENCY Record;
    ULONG Tag = 0;
    ULONG Length;
    PCELL_DATA Data;
    SIZE_T Size;
    PUCHAR Room;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, DriverCell);
    if (Node == NULL) return FALSE;

    NameLength = CmpKeyNameLength(Node);
    if (NameLength == 0)
    {
        HvReleaseCell(Hive, DriverCell);
        return FALSE;
    }

    ImagePath = CmpReadValue(Hive, Node, L"ImagePath", &ImageLength, NULL);
    Group = CmpReadValue(Hive, Node, L"Group", &GroupDataLength, NULL);

    CmpReadDword(Hive, Node, L"Tag", &Tag);
    CmpReadDword(Hive, Node, L"ErrorControl", &ErrorControl);

    if (ImagePath != NULL && ImageLength >= sizeof(WCHAR))
    {
        FileLength = (USHORT)(ImageLength - sizeof(WCHAR));
    }
    else
    {
        FileLength = CmpStringLength(CMP_DRIVER_FILE_PREFIX) +
                     NameLength +
                     CmpStringLength(CMP_DRIVER_FILE_SUFFIX);
    }

    if (Group != NULL && GroupDataLength >= sizeof(WCHAR))
    {
        GroupLength = (USHORT)(GroupDataLength - sizeof(WCHAR));
    }

    RegistryLength = CmpStringLength(CMP_DRIVER_PATH_PREFIX) + NameLength;

    Size = sizeof(BOOT_DRIVER_NODE) +
           NameLength + sizeof(WCHAR) +
           GroupLength + sizeof(WCHAR) +
           FileLength + sizeof(WCHAR) +
           RegistryLength + sizeof(WCHAR);

    DriverNode = CmpAllocate(Size, TRUE, TAG_CM);
    if (DriverNode == NULL)
    {
        HvReleaseCell(Hive, DriverCell);
        return FALSE;
    }

    RtlZeroMemory(DriverNode, Size);
    Room = (PUCHAR)(DriverNode + 1);

    DriverNode->Name.Buffer = (PWCHAR)Room;
    DriverNode->Name.MaximumLength = NameLength + sizeof(WCHAR);
    DriverNode->Name.Length = NameLength;
    CmpReadKeyName(Node, DriverNode->Name.Buffer, NameLength);
    Room += DriverNode->Name.MaximumLength;

    DriverNode->Group.Buffer = (PWCHAR)Room;
    DriverNode->Group.MaximumLength = GroupLength + sizeof(WCHAR);
    DriverNode->Group.Length = GroupLength;
    if (GroupLength != 0)
    {
        RtlCopyMemory(DriverNode->Group.Buffer, Group, GroupLength);
    }
    Room += DriverNode->Group.MaximumLength;

    DriverNode->ListEntry.FilePath.Buffer = (PWCHAR)Room;
    DriverNode->ListEntry.FilePath.MaximumLength = FileLength + sizeof(WCHAR);
    DriverNode->ListEntry.FilePath.Length = 0;

    if (ImagePath != NULL && ImageLength >= sizeof(WCHAR))
    {
        RtlCopyMemory(DriverNode->ListEntry.FilePath.Buffer,
                      ImagePath,
                      FileLength);
        DriverNode->ListEntry.FilePath.Length = FileLength;
    }
    else
    {
        RtlAppendUnicodeToString(&DriverNode->ListEntry.FilePath,
                                 CMP_DRIVER_FILE_PREFIX);
        RtlAppendUnicodeStringToString(&DriverNode->ListEntry.FilePath,
                                       &DriverNode->Name);
        RtlAppendUnicodeToString(&DriverNode->ListEntry.FilePath,
                                 CMP_DRIVER_FILE_SUFFIX);
    }

    Room += DriverNode->ListEntry.FilePath.MaximumLength;

    DriverNode->ListEntry.RegistryPath.Buffer = (PWCHAR)Room;
    DriverNode->ListEntry.RegistryPath.MaximumLength =
        RegistryLength + sizeof(WCHAR);
    DriverNode->ListEntry.RegistryPath.Length = 0;

    RtlAppendUnicodeToString(&DriverNode->ListEntry.RegistryPath,
                             CMP_DRIVER_PATH_PREFIX);
    RtlAppendUnicodeStringToString(&DriverNode->ListEntry.RegistryPath,
                                   &DriverNode->Name);

    DriverNode->Tag = Tag;
    DriverNode->ErrorControl = ErrorControl;

    Record = CmpAddDependencyRecord(DriverNode);

    if (Record != NULL)
    {
        Data = CmpReadValue(Hive, Node, L"DependOnService", &Length, NULL);
        Record->Services = CmpCopyMultiString(Data,
                                              Length,
                                              &Record->ServicesLength);

        Data = CmpReadValue(Hive, Node, L"DependOnGroup", &Length, NULL);
        Record->Groups = CmpCopyMultiString(Data,
                                            Length,
                                            &Record->GroupsLength);
    }

    HvReleaseCell(Hive, DriverCell);

    InsertTailList(DriverListHead, &DriverNode->ListEntry.Link);
    return TRUE;
}

/*
 * The tags of one group, in the order the group wants them started. A driver
 * whose tag is not named here starts after every driver whose tag is.
 */
static
ULONG
CmpTagRank(
    _In_opt_ PCELL_DATA Order,
    _In_ ULONG OrderLength,
    _In_ ULONG Tag)
{
    PULONG Tags;
    ULONG Count;
    ULONG i;

    if (Order == NULL || OrderLength < sizeof(ULONG)) return MAXULONG;
    if (Tag == 0) return MAXULONG;

    Tags = (PULONG)Order;
    Count = Tags[0];

    if (Count > (OrderLength / sizeof(ULONG)) - 1)
    {
        Count = (OrderLength / sizeof(ULONG)) - 1;
    }

    for (i = 0; i < Count; i++)
    {
        if (Tags[i + 1] == Tag) return i;
    }

    return MAXULONG;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Picks the control set the system is to run from. The caller names the value
 * under the select key, Current, Default or LastKnownGood, whose number names
 * the control set every later lookup goes to.
 */
CODE_SEG("INIT")
HCELL_INDEX
NTAPI
CmpFindControlSet(
    _In_ PHHIVE SystemHive,
    _In_ HCELL_INDEX RootCell,
    _In_ PCUNICODE_STRING SelectKeyName,
    _Out_ PBOOLEAN AutoSelect)
{
    UNICODE_STRING SelectName = RTL_CONSTANT_STRING(L"Select");
    WCHAR NameBuffer[sizeof("ControlSet000")];
    UNICODE_STRING ControlSetName;
    PCM_KEY_NODE SelectNode;
    PCM_KEY_NODE RootNode;
    HCELL_INDEX SelectCell;
    HCELL_INDEX ControlSet;
    ULONG Number = 0;
    ULONG Value = 1;
    ULONG i;

    *AutoSelect = TRUE;

    RootNode = (PCM_KEY_NODE)HvGetCell(SystemHive, RootCell);
    if (RootNode == NULL) return HCELL_NIL;

    SelectCell = CmpFindSubKeyByName(SystemHive, RootNode, &SelectName);
    HvReleaseCell(SystemHive, RootCell);

    if (SelectCell == HCELL_NIL) return HCELL_NIL;

    SelectNode = (PCM_KEY_NODE)HvGetCell(SystemHive, SelectCell);
    if (SelectNode == NULL) return HCELL_NIL;

    if (!CmpReadDwordByName(SystemHive, SelectNode, SelectKeyName, &Number) ||
        Number == 0)
    {
        HvReleaseCell(SystemHive, SelectCell);
        return HCELL_NIL;
    }

    if (CmpReadDword(SystemHive, SelectNode, L"AutoSelect", &Value))
    {
        *AutoSelect = (Value != 0);
    }

    HvReleaseCell(SystemHive, SelectCell);

    for (i = 0; CmpControlSetPrefix[i] != UNICODE_NULL; i++)
    {
        NameBuffer[i] = CmpControlSetPrefix[i];
    }

    NameBuffer[i + 0] = (WCHAR)(L'0' + ((Number / 100) % 10));
    NameBuffer[i + 1] = (WCHAR)(L'0' + ((Number / 10) % 10));
    NameBuffer[i + 2] = (WCHAR)(L'0' + (Number % 10));
    NameBuffer[i + 3] = UNICODE_NULL;

    RtlInitUnicodeString(&ControlSetName, NameBuffer);

    RootNode = (PCM_KEY_NODE)HvGetCell(SystemHive, RootCell);
    if (RootNode == NULL) return HCELL_NIL;

    ControlSet = CmpFindSubKeyByName(SystemHive, RootNode, &ControlSetName);
    HvReleaseCell(SystemHive, RootCell);

    return ControlSet;
}

/*
 * Collects every service under the control set that says it starts at this
 * point of the boot and that is a driver rather than a program.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpFindDrivers(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX ControlSet,
    _In_ SERVICE_LOAD_TYPE LoadType,
    _In_opt_ PCWSTR BootFileSystem,
    _Inout_ PLIST_ENTRY DriverListHead)
{
    UNICODE_STRING ServicesName = RTL_CONSTANT_STRING(L"Services");
    UNICODE_STRING BootFileSystemName;
    PCM_KEY_NODE ServicesNode;
    PCM_KEY_NODE ControlNode;
    PCM_KEY_NODE DriverNode;
    HCELL_INDEX ServicesCell;
    HCELL_INDEX DriverCell;
    ULONG Start;
    ULONG Type;
    ULONG i;

    ControlNode = (PCM_KEY_NODE)HvGetCell(Hive, ControlSet);
    if (ControlNode == NULL) return FALSE;

    ServicesCell = CmpFindSubKeyByName(Hive, ControlNode, &ServicesName);
    HvReleaseCell(Hive, ControlSet);

    if (ServicesCell == HCELL_NIL) return FALSE;

    ServicesNode = (PCM_KEY_NODE)HvGetCell(Hive, ServicesCell);
    if (ServicesNode == NULL) return FALSE;

    for (i = 0; ; i++)
    {
        DriverCell = CmpFindSubKeyByNumber(Hive, ServicesNode, i);
        if (DriverCell == HCELL_NIL) break;

        DriverNode = (PCM_KEY_NODE)HvGetCell(Hive, DriverCell);
        if (DriverNode == NULL) continue;

        if (!CmpReadDword(Hive, DriverNode, L"Start", &Start) ||
            !CmpReadDword(Hive, DriverNode, L"Type", &Type))
        {
            HvReleaseCell(Hive, DriverCell);
            continue;
        }

        HvReleaseCell(Hive, DriverCell);

        if (Start != (ULONG)LoadType) continue;
        if (!CmpIsDriverServiceType(Type)) continue;

        CmpAddDriverToList(Hive, DriverCell, DriverListHead);
    }

    if (BootFileSystem != NULL)
    {
        RtlInitUnicodeString(&BootFileSystemName, BootFileSystem);

        DriverCell = CmpFindSubKeyByName(Hive,
                                         ServicesNode,
                                         &BootFileSystemName);
        if (DriverCell != HCELL_NIL)
        {
            CmpAddDriverToList(Hive, DriverCell, DriverListHead);
        }
    }

    HvReleaseCell(Hive, ServicesCell);
    return TRUE;
}

/*
 * Puts the drivers in the order the control set asks for: group by group as
 * the group order lists them, and inside a group by tag.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpSortDriverList(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX ControlSet,
    _Inout_ PLIST_ENTRY DriverListHead)
{
    UNICODE_STRING ControlName = RTL_CONSTANT_STRING(L"Control");
    UNICODE_STRING GroupOrderName = RTL_CONSTANT_STRING(L"ServiceGroupOrder");
    UNICODE_STRING TagOrderName = RTL_CONSTANT_STRING(L"GroupOrderList");
    UNICODE_STRING GroupName;
    PBOOT_DRIVER_NODE DriverNode;
    PCM_KEY_NODE ControlNode;
    PCM_KEY_NODE OrderNode;
    PCM_KEY_NODE TagNode;
    HCELL_INDEX ControlCell;
    HCELL_INDEX OrderCell;
    HCELL_INDEX TagCell;
    PCELL_DATA GroupList;
    PCELL_DATA TagOrder;
    LIST_ENTRY Sorted;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    PLIST_ENTRY Place;
    PWCHAR Group;
    PWCHAR End;
    ULONG GroupListLength = 0;
    ULONG TagOrderLength;
    ULONG Rank;

    InitializeListHead(&Sorted);

    ControlNode = (PCM_KEY_NODE)HvGetCell(Hive, ControlSet);
    if (ControlNode == NULL) return FALSE;

    ControlCell = CmpFindSubKeyByName(Hive, ControlNode, &ControlName);
    HvReleaseCell(Hive, ControlSet);

    if (ControlCell == HCELL_NIL) return TRUE;

    ControlNode = (PCM_KEY_NODE)HvGetCell(Hive, ControlCell);
    if (ControlNode == NULL) return FALSE;

    OrderCell = CmpFindSubKeyByName(Hive, ControlNode, &GroupOrderName);
    TagCell = CmpFindSubKeyByName(Hive, ControlNode, &TagOrderName);
    HvReleaseCell(Hive, ControlCell);

    if (OrderCell == HCELL_NIL) return TRUE;

    OrderNode = (PCM_KEY_NODE)HvGetCell(Hive, OrderCell);
    if (OrderNode == NULL) return FALSE;

    GroupList = CmpReadValue(Hive, OrderNode, L"List", &GroupListLength, NULL);

    if (GroupList == NULL || GroupListLength < sizeof(WCHAR))
    {
        HvReleaseCell(Hive, OrderCell);
        return TRUE;
    }

    TagNode = NULL;
    if (TagCell != HCELL_NIL)
    {
        TagNode = (PCM_KEY_NODE)HvGetCell(Hive, TagCell);
    }

    Group = (PWCHAR)GroupList;
    End = Group + GroupListLength / sizeof(WCHAR);

    while (Group < End && *Group != UNICODE_NULL)
    {
        RtlInitUnicodeString(&GroupName, Group);

        TagOrder = NULL;
        TagOrderLength = 0;

        if (TagNode != NULL)
        {
            TagOrder = CmpReadValue(Hive,
                                    TagNode,
                                    Group,
                                    &TagOrderLength,
                                    NULL);
        }

        for (Entry = DriverListHead->Flink;
             Entry != DriverListHead;
             Entry = Next)
        {
            Next = Entry->Flink;
            DriverNode = CONTAINING_RECORD(Entry,
                                           BOOT_DRIVER_NODE,
                                           ListEntry.Link);

            if (RtlCompareUnicodeString(&DriverNode->Group,
                                        &GroupName,
                                        TRUE) != 0)
            {
                continue;
            }

            Rank = CmpTagRank(TagOrder, TagOrderLength, DriverNode->Tag);

            for (Place = Sorted.Blink; Place != &Sorted; Place = Place->Blink)
            {
                PBOOT_DRIVER_NODE Placed =
                    CONTAINING_RECORD(Place, BOOT_DRIVER_NODE, ListEntry.Link);

                if (RtlCompareUnicodeString(&Placed->Group,
                                            &GroupName,
                                            TRUE) != 0)
                {
                    break;
                }

                if (CmpTagRank(TagOrder,
                               TagOrderLength,
                               Placed->Tag) <= Rank)
                {
                    break;
                }
            }

            RemoveEntryList(Entry);
            InsertHeadList(Place, Entry);
        }

        Group += wcslen(Group) + 1;
    }

    if (TagNode != NULL) HvReleaseCell(Hive, TagCell);
    HvReleaseCell(Hive, OrderCell);

    while (!IsListEmpty(DriverListHead))
    {
        Entry = RemoveHeadList(DriverListHead);
        InsertTailList(&Sorted, Entry);
    }

    while (!IsListEmpty(&Sorted))
    {
        Entry = RemoveHeadList(&Sorted);
        InsertTailList(DriverListHead, Entry);
    }

    return TRUE;
}

/* Where one driver sits in the list, counted from the front */
static
ULONG
CmpDriverPosition(
    _In_ PLIST_ENTRY DriverListHead,
    _In_ PBOOT_DRIVER_NODE Node)
{
    PLIST_ENTRY Entry;
    ULONG Position = 0;

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link) == Node)
        {
            return Position;
        }

        Position++;
    }

    return MAXULONG;
}

static
PBOOT_DRIVER_NODE
CmpFindDriverByName(
    _In_ PLIST_ENTRY DriverListHead,
    _In_ PCUNICODE_STRING Name)
{
    PBOOT_DRIVER_NODE Node;
    PLIST_ENTRY Entry;

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Entry->Flink)
    {
        Node = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);

        if (RtlCompareUnicodeString(&Node->Name, Name, TRUE) == 0) return Node;
    }

    return NULL;
}

static
PBOOT_DRIVER_NODE
CmpFindLastOfGroup(
    _In_ PLIST_ENTRY DriverListHead,
    _In_ PCUNICODE_STRING Group)
{
    PBOOT_DRIVER_NODE Found = NULL;
    PBOOT_DRIVER_NODE Node;
    PLIST_ENTRY Entry;

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Entry->Flink)
    {
        Node = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);

        if (RtlCompareUnicodeString(&Node->Group, Group, TRUE) == 0)
        {
            Found = Node;
        }
    }

    return Found;
}

/* Moves one driver to just behind another it has to wait for */
static
BOOLEAN
CmpMoveDriverAfter(
    _In_ PLIST_ENTRY DriverListHead,
    _In_ PBOOT_DRIVER_NODE Node,
    _In_opt_ PBOOT_DRIVER_NODE After)
{
    if (After == NULL || After == Node) return FALSE;

    if (CmpDriverPosition(DriverListHead, After) <
        CmpDriverPosition(DriverListHead, Node))
    {
        return FALSE;
    }

    RemoveEntryList(&Node->ListEntry.Link);
    InsertHeadList(&After->ListEntry.Link, &Node->ListEntry.Link);
    return TRUE;
}

/*
 * Puts every driver behind whatever it said it needs. A driver names what it
 * waits for by service or by group, and both are honoured; a chain that comes
 * back around to itself is left as it stands rather than looped on forever.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpResolveDriverDependencies(
    _Inout_ PLIST_ENTRY DriverListHead)
{
    PCM_DRIVER_DEPENDENCY Record;
    PBOOT_DRIVER_NODE DriverNode;
    PBOOT_DRIVER_NODE Needed;
    UNICODE_STRING Name;
    LIST_ENTRY Tail;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    BOOLEAN Changed;
    ULONG Passes = 0;
    ULONG Count = 0;
    PWCHAR Current;
    PWCHAR End;

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Entry->Flink)
    {
        Count++;
    }

    do
    {
        Changed = FALSE;

        for (Entry = DriverListHead->Flink;
             Entry != DriverListHead;
             Entry = Next)
        {
            Next = Entry->Flink;
            DriverNode = CONTAINING_RECORD(Entry,
                                           BOOT_DRIVER_NODE,
                                           ListEntry.Link);

            Record = CmpFindDependencyRecord(DriverNode);
            if (Record == NULL) continue;

            if (Record->Services != NULL)
            {
                Current = Record->Services;
                End = Current + Record->ServicesLength / sizeof(WCHAR);

                while (Current < End && *Current != UNICODE_NULL)
                {
                    RtlInitUnicodeString(&Name, Current);

                    Needed = CmpFindDriverByName(DriverListHead, &Name);
                    if (CmpMoveDriverAfter(DriverListHead,
                                           DriverNode,
                                           Needed))
                    {
                        Changed = TRUE;
                        Next = DriverListHead->Flink;
                    }

                    Current += wcslen(Current) + 1;
                }
            }

            if (Record->Groups == NULL) continue;

            Current = Record->Groups;
            End = Current + Record->GroupsLength / sizeof(WCHAR);

            while (Current < End && *Current != UNICODE_NULL)
            {
                RtlInitUnicodeString(&Name, Current);

                Needed = CmpFindLastOfGroup(DriverListHead, &Name);
                if (CmpMoveDriverAfter(DriverListHead, DriverNode, Needed))
                {
                    Changed = TRUE;
                    Next = DriverListHead->Flink;
                }

                Current += wcslen(Current) + 1;
            }
        }

        Passes++;
    } while (Changed && Passes <= Count);

    InitializeListHead(&Tail);

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Next)
    {
        Next = Entry->Flink;
        DriverNode = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);

        if (DriverNode->Group.Length != 0) continue;
        if (CmpFindDependencyRecord(DriverNode) != NULL &&
            CmpFindDependencyRecord(DriverNode)->Services != NULL)
        {
            continue;
        }

        RemoveEntryList(Entry);
        InsertTailList(&Tail, Entry);
    }

    while (!IsListEmpty(&Tail))
    {
        Entry = RemoveHeadList(&Tail);
        InsertTailList(DriverListHead, Entry);
    }

    return TRUE;
}

CODE_SEG("INIT")
VOID
NTAPI
CmpFreeDriverList(
    _In_ PHHIVE Hive,
    _Inout_ PLIST_ENTRY DriverListHead)
{
    PBOOT_DRIVER_NODE DriverNode;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Hive);

    while (!IsListEmpty(DriverListHead))
    {
        Entry = RemoveHeadList(DriverListHead);
        DriverNode = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);
        CmpFree(DriverNode, 0);
    }

    CmpFreeDependencyRecords();
}

#ifdef _BLDR_

CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpIsDriverInList(
    _In_ PLIST_ENTRY DriverListHead,
    _In_ PCUNICODE_STRING DriverName,
    _Out_opt_ PBOOT_DRIVER_NODE* FoundDriver)
{
    PBOOT_DRIVER_NODE DriverNode;
    PLIST_ENTRY Entry;

    if (FoundDriver != NULL) *FoundDriver = NULL;

    for (Entry = DriverListHead->Flink;
         Entry != DriverListHead;
         Entry = Entry->Flink)
    {
        DriverNode = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);

        if (RtlCompareUnicodeString(&DriverNode->Name,
                                    DriverName,
                                    TRUE) != 0)
        {
            continue;
        }

        if (FoundDriver != NULL) *FoundDriver = DriverNode;
        return TRUE;
    }

    return FALSE;
}

#endif /* _BLDR_ */
