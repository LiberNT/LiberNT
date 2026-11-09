/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The global atom table, where a string stands in for a number
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVOID GlobalAtomTable = NULL;

PKWIN32_GLOBALATOMTABLE_CALLOUT ExpGlobalAtomTableCallout = NULL;

/* PRIVATE DATA ***************************************************************/

#define EXP_ATOM_TAG 'motA'

/* How many buckets a table hashes its names into */
#define EXP_ATOM_BUCKETS 37

/* The longest name an atom may be given, not counting its terminator */
#define EXP_ATOM_MAXIMUM_NAME 255

/*
 * The numbers below this are not handed out: a number that small is a name in
 * its own right, which a caller may use without ever adding it to a table.
 */
#define EXP_ATOM_FIRST 0xC000

/* The number above which there is nothing left to hand out */
#define EXP_ATOM_LAST 0xFFFF

/* A name whose count has reached this is kept until the table itself goes */
#define EXP_ATOM_PINNED 0xFFFF

/*
 * One name in the table. The name follows the record, so a name and everything
 * known about it are one allocation and go away together.
 */
typedef struct _EXP_ATOM_ENTRY
{
    LIST_ENTRY HashLink;
    RTL_ATOM Atom;
    USHORT ReferenceCount;
    BOOLEAN Pinned;
    USHORT NameLength;
    WCHAR Name[ANYSIZE_ARRAY];
} EXP_ATOM_ENTRY, *PEXP_ATOM_ENTRY;

/*
 * A table of names. The names are reached by their hash when a caller gives a
 * name, and by their number through the handle table, so neither direction
 * costs a walk of the whole table.
 */
typedef struct _EXP_ATOM_TABLE
{
    ERESOURCE Lock;
    PHANDLE_TABLE Handles;
    ULONG NumberOfBuckets;
    LIST_ENTRY Buckets[EXP_ATOM_BUCKETS];
} EXP_ATOM_TABLE, *PEXP_ATOM_TABLE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Names are matched without regard to case, so they hash without regard to it
 * either. The hash is taken over the upcased characters for that reason.
 */
static
ULONG
ExpAtomHash(
    _In_ PEXP_ATOM_TABLE Table,
    _In_reads_(Length) PCWCH Name,
    _In_ USHORT Length)
{
    ULONG Hash = 0;
    USHORT Index;

    for (Index = 0; Index < Length; Index++)
    {
        Hash = (Hash << 1) + RtlUpcaseUnicodeChar(Name[Index]);
    }

    return Hash % Table->NumberOfBuckets;
}

static
BOOLEAN
ExpAtomNameEqual(
    _In_ PEXP_ATOM_ENTRY Entry,
    _In_reads_(Length) PCWCH Name,
    _In_ USHORT Length)
{
    USHORT Index;

    if (Entry->NameLength != Length)
    {
        return FALSE;
    }

    for (Index = 0; Index < Length; Index++)
    {
        if (RtlUpcaseUnicodeChar(Entry->Name[Index]) !=
            RtlUpcaseUnicodeChar(Name[Index]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
PEXP_ATOM_ENTRY
ExpFindAtomByName(
    _In_ PEXP_ATOM_TABLE Table,
    _In_reads_(Length) PCWCH Name,
    _In_ USHORT Length)
{
    ULONG Bucket = ExpAtomHash(Table, Name, Length);
    PLIST_ENTRY Entry;

    for (Entry = Table->Buckets[Bucket].Flink;
         Entry != &Table->Buckets[Bucket];
         Entry = Entry->Flink)
    {
        PEXP_ATOM_ENTRY Atom = CONTAINING_RECORD(Entry,
                                                 EXP_ATOM_ENTRY,
                                                 HashLink);

        if (ExpAtomNameEqual(Atom, Name, Length))
        {
            return Atom;
        }
    }

    return NULL;
}

/*
 * The number a name was given is a handle in the table's own handle table, so
 * the name behind a number is found without searching.
 */
static
PEXP_ATOM_ENTRY
ExpFindAtomByNumber(
    _In_ PEXP_ATOM_TABLE Table,
    _In_ RTL_ATOM Atom)
{
    PHANDLE_TABLE_ENTRY Entry;
    PEXP_ATOM_ENTRY Found;

    if (Atom < EXP_ATOM_FIRST)
    {
        return NULL;
    }

    Entry = ExMapHandleToPointer(Table->Handles,
                                 (HANDLE)(ULONG_PTR)
                                 ((Atom - EXP_ATOM_FIRST + 1) << 2));
    if (Entry == NULL)
    {
        return NULL;
    }

    Found = (PEXP_ATOM_ENTRY)((ULONG_PTR)Entry->Object &
                              ~(ULONG_PTR)OBJ_HANDLE_ATTRIBUTES);

    ExUnlockHandleTableEntry(Table->Handles, Entry);

    return Found;
}

static
VOID
ExpFreeAtomEntry(
    _In_ PEXP_ATOM_TABLE Table,
    _In_ PEXP_ATOM_ENTRY Atom)
{
    RemoveEntryList(&Atom->HashLink);

    ExDestroyHandle(Table->Handles,
                    (HANDLE)(ULONG_PTR)((Atom->Atom - EXP_ATOM_FIRST + 1) << 2),
                    NULL);

    ExFreePoolWithTag(Atom, EXP_ATOM_TAG);
}

static
BOOLEAN
NTAPI
ExpDestroyAtomCallback(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_opt_ PVOID Context)
{
    PEXP_ATOM_ENTRY Atom;

    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Context);

    Atom = (PEXP_ATOM_ENTRY)((ULONG_PTR)HandleTableEntry->Object &
                             ~(ULONG_PTR)OBJ_HANDLE_ATTRIBUTES);
    if (Atom != NULL)
    {
        RemoveEntryList(&Atom->HashLink);
        ExFreePoolWithTag(Atom, EXP_ATOM_TAG);
    }

    return FALSE;
}

static
VOID
ExpLockAtomTable(
    _In_ PEXP_ATOM_TABLE Table,
    _In_ BOOLEAN Exclusive)
{
    KeEnterCriticalRegion();

    if (Exclusive)
    {
        ExAcquireResourceExclusiveLite(&Table->Lock, TRUE);
        return;
    }

    ExAcquireResourceSharedLite(&Table->Lock, TRUE);
}

static
VOID
ExpUnlockAtomTable(
    _In_ PEXP_ATOM_TABLE Table)
{
    ExReleaseResourceLite(&Table->Lock);
    KeLeaveCriticalRegion();
}

/*
 * Copies a name a caller handed in, checking as it goes that it is a name an
 * atom may be given: not empty, not longer than a name may be, and made of
 * whole characters.
 */
static
NTSTATUS
ExpCaptureAtomName(
    _Out_writes_(EXP_ATOM_MAXIMUM_NAME + 1) PWCHAR Buffer,
    _Out_ PUSHORT Length,
    _In_opt_ PWSTR Name,
    _In_ ULONG NameLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    *Length = 0;

    if (Name == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((NameLength == 0) || ((NameLength & 1) != 0) ||
        (NameLength > EXP_ATOM_MAXIMUM_NAME * sizeof(WCHAR)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ExGetPreviousMode() == KernelMode)
    {
        RtlCopyMemory(Buffer, Name, NameLength);
    }
    else
    {
        _SEH2_TRY
        {
            ProbeForRead(Name, NameLength, sizeof(WCHAR));
            RtlCopyMemory(Buffer, Name, NameLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Buffer[NameLength / sizeof(WCHAR)] = UNICODE_NULL;
    *Length = (USHORT)(NameLength / sizeof(WCHAR));

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ExpCreateAtomTable(
    _Out_ PVOID *AtomTable)
{
    PEXP_ATOM_TABLE Table;
    ULONG Index;

    PAGED_CODE();

    Table = ExAllocatePoolZero(NonPagedPool,
                               sizeof(EXP_ATOM_TABLE),
                               EXP_ATOM_TAG);
    if (Table == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ExInitializeResourceLite(&Table->Lock);

    Table->NumberOfBuckets = EXP_ATOM_BUCKETS;

    for (Index = 0; Index < EXP_ATOM_BUCKETS; Index++)
    {
        InitializeListHead(&Table->Buckets[Index]);
    }

    Table->Handles = ExCreateHandleTable(NULL, FALSE);
    if (Table->Handles == NULL)
    {
        ExDeleteResourceLite(&Table->Lock);
        ExFreePoolWithTag(Table, EXP_ATOM_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *AtomTable = Table;

    return STATUS_SUCCESS;
}

VOID
NTAPI
ExpDestroyAtomTable(
    _In_ PVOID AtomTable)
{
    PEXP_ATOM_TABLE Table = AtomTable;

    PAGED_CODE();

    ExSweepHandleTable(Table->Handles, ExpDestroyAtomCallback, NULL);
    ExDestroyHandleTable(Table->Handles, NULL);

    ExDeleteResourceLite(&Table->Lock);

    ExFreePoolWithTag(Table, EXP_ATOM_TAG);
}

/*
 * The table a caller reaches depends on where it is calling from: a process
 * with a window station of its own uses that session's table, which the window
 * manager hands back, and anything else uses the one table the kernel keeps.
 */
PVOID
NTAPI
ExpGetGlobalAtomTable(VOID)
{
    PVOID Table;

    PAGED_CODE();

    if (ExpGlobalAtomTableCallout != NULL)
    {
        Table = ExpGlobalAtomTableCallout();
        if (Table != NULL)
        {
            return Table;
        }
    }

    if (GlobalAtomTable != NULL)
    {
        return GlobalAtomTable;
    }

    if (!NT_SUCCESS(ExpCreateAtomTable(&Table)))
    {
        return NULL;
    }

    /*
     * Two callers may arrive here at once; the first one's table is the one
     * that is kept and the other is thrown away.
     */
    if (InterlockedCompareExchangePointer(&GlobalAtomTable,
                                          Table,
                                          NULL) != NULL)
    {
        ExpDestroyAtomTable(Table);
    }

    return GlobalAtomTable;
}

/*
 * Gives a name a number, or finds the number it already has. A name that is
 * already there has one more holder; a name that is new starts with one.
 */
NTSTATUS
NTAPI
ExpAddAtom(
    _In_ PVOID AtomTable,
    _In_reads_(Length) PCWCH Name,
    _In_ USHORT Length,
    _Out_ PRTL_ATOM Atom)
{
    PEXP_ATOM_TABLE Table = AtomTable;
    PEXP_ATOM_ENTRY Entry;
    HANDLE_TABLE_ENTRY HandleEntry;
    HANDLE Handle;
    ULONG Bucket;

    PAGED_CODE();

    ExpLockAtomTable(Table, TRUE);

    Entry = ExpFindAtomByName(Table, Name, Length);
    if (Entry != NULL)
    {
        if (!Entry->Pinned && (Entry->ReferenceCount < EXP_ATOM_PINNED))
        {
            Entry->ReferenceCount++;
        }

        *Atom = Entry->Atom;

        ExpUnlockAtomTable(Table);
        return STATUS_SUCCESS;
    }

    Entry = ExAllocatePoolZero(PagedPool,
                               FIELD_OFFSET(EXP_ATOM_ENTRY, Name) +
                               (Length + 1) * sizeof(WCHAR),
                               EXP_ATOM_TAG);
    if (Entry == NULL)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->NameLength = Length;
    Entry->ReferenceCount = 1;
    Entry->Pinned = FALSE;
    RtlCopyMemory(Entry->Name, Name, Length * sizeof(WCHAR));
    Entry->Name[Length] = UNICODE_NULL;

    RtlZeroMemory(&HandleEntry, sizeof(HandleEntry));
    HandleEntry.Object = Entry;
    HandleEntry.GrantedAccess = GENERIC_ALL;

    Handle = ExCreateHandle(Table->Handles, &HandleEntry);
    if (Handle == NULL)
    {
        ExFreePoolWithTag(Entry, EXP_ATOM_TAG);
        ExpUnlockAtomTable(Table);
        return STATUS_NO_MEMORY;
    }

    /*
     * The handle's index becomes the number the name is known by, offset so the
     * numbers start where numbers that stand for themselves end.
     */
    Entry->Atom = (RTL_ATOM)(EXP_ATOM_FIRST +
                             (((ULONG_PTR)Handle >> 2) - 1));

    if (Entry->Atom > EXP_ATOM_LAST)
    {
        ExDestroyHandle(Table->Handles, Handle, NULL);
        ExFreePoolWithTag(Entry, EXP_ATOM_TAG);
        ExpUnlockAtomTable(Table);
        return STATUS_NO_MEMORY;
    }

    Bucket = ExpAtomHash(Table, Name, Length);
    InsertTailList(&Table->Buckets[Bucket], &Entry->HashLink);

    *Atom = Entry->Atom;

    ExpUnlockAtomTable(Table);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpLookupAtom(
    _In_ PVOID AtomTable,
    _In_reads_(Length) PCWCH Name,
    _In_ USHORT Length,
    _Out_ PRTL_ATOM Atom)
{
    PEXP_ATOM_TABLE Table = AtomTable;
    PEXP_ATOM_ENTRY Entry;

    PAGED_CODE();

    ExpLockAtomTable(Table, FALSE);

    Entry = ExpFindAtomByName(Table, Name, Length);
    if (Entry == NULL)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *Atom = Entry->Atom;

    ExpUnlockAtomTable(Table);

    return STATUS_SUCCESS;
}

/*
 * Gives up one holder's claim on a name. The name goes away with the last
 * claim, unless it has been pinned, in which case it stays for good.
 */
NTSTATUS
NTAPI
ExpDeleteAtom(
    _In_ PVOID AtomTable,
    _In_ RTL_ATOM Atom)
{
    PEXP_ATOM_TABLE Table = AtomTable;
    PEXP_ATOM_ENTRY Entry;

    PAGED_CODE();

    /* A number that stands for itself was never in the table to begin with. */
    if (Atom < EXP_ATOM_FIRST)
    {
        return STATUS_SUCCESS;
    }

    ExpLockAtomTable(Table, TRUE);

    Entry = ExpFindAtomByNumber(Table, Atom);
    if (Entry == NULL)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_INVALID_HANDLE;
    }

    if (Entry->Pinned)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_WAS_LOCKED;
    }

    if (--Entry->ReferenceCount == 0)
    {
        ExpFreeAtomEntry(Table, Entry);
    }

    ExpUnlockAtomTable(Table);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpPinAtom(
    _In_ PVOID AtomTable,
    _In_ RTL_ATOM Atom)
{
    PEXP_ATOM_TABLE Table = AtomTable;
    PEXP_ATOM_ENTRY Entry;

    PAGED_CODE();

    if (Atom < EXP_ATOM_FIRST)
    {
        return STATUS_SUCCESS;
    }

    ExpLockAtomTable(Table, TRUE);

    Entry = ExpFindAtomByNumber(Table, Atom);
    if (Entry == NULL)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_INVALID_HANDLE;
    }

    Entry->Pinned = TRUE;

    ExpUnlockAtomTable(Table);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAddAtom(
    _In_reads_bytes_opt_(AtomNameLength) PWSTR AtomName,
    _In_ ULONG AtomNameLength,
    _Out_opt_ PRTL_ATOM Atom)
{
    WCHAR Name[EXP_ATOM_MAXIMUM_NAME + 1];
    PVOID Table;
    RTL_ATOM Result = 0;
    USHORT Length;
    NTSTATUS Status;

    PAGED_CODE();

    Table = ExpGetGlobalAtomTable();
    if (Table == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if ((Atom != NULL) && (ExGetPreviousMode() != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForWriteUshort(Atom);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpCaptureAtomName(Name, &Length, AtomName, AtomNameLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpAddAtom(Table, Name, Length, &Result);
    if (!NT_SUCCESS(Status) || (Atom == NULL))
    {
        return Status;
    }

    _SEH2_TRY
    {
        *Atom = Result;
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
NtFindAtom(
    _In_reads_bytes_opt_(AtomNameLength) PWSTR AtomName,
    _In_ ULONG AtomNameLength,
    _Out_opt_ PRTL_ATOM Atom)
{
    WCHAR Name[EXP_ATOM_MAXIMUM_NAME + 1];
    PVOID Table;
    RTL_ATOM Result = 0;
    USHORT Length;
    NTSTATUS Status;

    PAGED_CODE();

    Table = ExpGetGlobalAtomTable();
    if (Table == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if ((Atom != NULL) && (ExGetPreviousMode() != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForWriteUshort(Atom);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpCaptureAtomName(Name, &Length, AtomName, AtomNameLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpLookupAtom(Table, Name, Length, &Result);
    if (!NT_SUCCESS(Status) || (Atom == NULL))
    {
        return Status;
    }

    _SEH2_TRY
    {
        *Atom = Result;
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
NtDeleteAtom(
    _In_ RTL_ATOM Atom)
{
    PVOID Table;

    PAGED_CODE();

    Table = ExpGetGlobalAtomTable();
    if (Table == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return ExpDeleteAtom(Table, Atom);
}

/*
 * Says what is known about one name, or lists the numbers the table has handed
 * out. Both are written into room the caller set aside.
 */
NTSTATUS
NTAPI
NtQueryInformationAtom(
    _In_ RTL_ATOM Atom,
    _In_ ATOM_INFORMATION_CLASS AtomInformationClass,
    _Out_ PVOID AtomInformation,
    _In_ ULONG AtomInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEXP_ATOM_TABLE Table;
    PEXP_ATOM_ENTRY Entry;
    ULONG Required;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    Table = ExpGetGlobalAtomTable();
    if (Table == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if ((AtomInformationClass != AtomBasicInformation) &&
        (AtomInformationClass != AtomTableInformation))
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(AtomInformation,
                          AtomInformationLength,
                          sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (AtomInformationClass == AtomTableInformation)
    {
        PATOM_TABLE_INFORMATION Info = AtomInformation;
        ULONG Capacity;
        ULONG Count = 0;
        ULONG Bucket;

        if (AtomInformationLength <
            FIELD_OFFSET(ATOM_TABLE_INFORMATION, Atoms))
        {
            return STATUS_INFO_LENGTH_MISMATCH;
        }

        Capacity = (AtomInformationLength -
                    (ULONG)FIELD_OFFSET(ATOM_TABLE_INFORMATION, Atoms)) /
                   (ULONG)sizeof(RTL_ATOM);

        ExpLockAtomTable(Table, FALSE);

        _SEH2_TRY
        {
            for (Bucket = 0; Bucket < Table->NumberOfBuckets; Bucket++)
            {
                PLIST_ENTRY Link;

                for (Link = Table->Buckets[Bucket].Flink;
                     Link != &Table->Buckets[Bucket];
                     Link = Link->Flink)
                {
                    Entry = CONTAINING_RECORD(Link,
                                              EXP_ATOM_ENTRY,
                                              HashLink);

                    if (Count < Capacity)
                    {
                        Info->Atoms[Count] = Entry->Atom;
                    }

                    Count++;
                }
            }

            Info->NumberOfAtoms = Count;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ExpUnlockAtomTable(Table);

        Required = (ULONG)FIELD_OFFSET(ATOM_TABLE_INFORMATION, Atoms) +
                   Count * (ULONG)sizeof(RTL_ATOM);

        if (ReturnLength != NULL)
        {
            _SEH2_TRY
            {
                *ReturnLength = Required;
            }
            _SEH2_EXCEPT(ExSystemExceptionFilter())
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }

        if (NT_SUCCESS(Status) && (Count > Capacity))
        {
            return STATUS_INFO_LENGTH_MISMATCH;
        }

        return Status;
    }

    if (AtomInformationLength < sizeof(ATOM_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    ExpLockAtomTable(Table, FALSE);

    Entry = ExpFindAtomByNumber(Table, Atom);
    if (Entry == NULL)
    {
        ExpUnlockAtomTable(Table);
        return STATUS_INVALID_HANDLE;
    }

    Required = (ULONG)FIELD_OFFSET(ATOM_BASIC_INFORMATION, Name) +
               (Entry->NameLength + 1) * (ULONG)sizeof(WCHAR);

    _SEH2_TRY
    {
        PATOM_BASIC_INFORMATION Info = AtomInformation;
        ULONG Room;
        ULONG Fits;

        Info->UsageCount = Entry->ReferenceCount;
        Info->Flags = Entry->Pinned ? 1 : 0;
        Info->NameLength = (USHORT)(Entry->NameLength * sizeof(WCHAR));

        Room = AtomInformationLength -
               (ULONG)FIELD_OFFSET(ATOM_BASIC_INFORMATION, Name);
        Fits = Room / (ULONG)sizeof(WCHAR);

        if (Fits != 0)
        {
            ULONG Copy = Entry->NameLength;

            if (Copy > Fits - 1)
            {
                Copy = Fits - 1;
                Info->NameLength = (USHORT)(Copy * sizeof(WCHAR));
            }

            RtlCopyMemory(Info->Name, Entry->Name, Copy * sizeof(WCHAR));
            Info->Name[Copy] = UNICODE_NULL;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExpUnlockAtomTable(Table);

    if (ReturnLength != NULL)
    {
        _SEH2_TRY
        {
            *ReturnLength = Required;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    return Status;
}

/* EOF */
