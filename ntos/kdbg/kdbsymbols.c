/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turning addresses into names, and names back into addresses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define KDB_MODULE_NAME_MAX     64
#define KDB_SYMBOL_NAME_MAX     256
#define KDB_FILE_NAME_MAX       128
#define KDB_MAXIMUM_MODULES     4096

typedef struct _KDB_SYM_LOOKUP
{
    PCSTR Name;
    ULONG Rva;
    BOOLEAN Found;
} KDB_SYM_LOOKUP, *PKDB_SYM_LOOKUP;

typedef struct _KDB_SYM_ENUM
{
    PCSTR SymbolPattern;
    PCSTR ModuleName;
    ULONG_PTR ImageBase;
    ULONG MaximumMatches;
    ULONG MatchCount;
    BOOLEAN Truncated;
    PKDB_SYMBOL_ENUM_CALLBACK Callback;
    PVOID CallbackContext;
} KDB_SYM_ENUM, *PKDB_SYM_ENUM;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether a name matches a pattern, where a star stands for any run of
 * characters and a question mark for any one of them. Case is ignored,
 * because nobody types a symbol name the way the compiler wrote it.
 */
static
BOOLEAN
KdbpMatchPattern(
    _In_ PCSTR Pattern,
    _In_ PCSTR Name)
{
    PCSTR Star = NULL;
    PCSTR Mark = Name;

    if (Pattern == NULL)
        return TRUE;

    while (*Name != ANSI_NULL)
    {
        CHAR Left = *Pattern;
        CHAR Right = *Name;

        if ((Left >= 'A') && (Left <= 'Z'))
            Left = Left - 'A' + 'a';
        if ((Right >= 'A') && (Right <= 'Z'))
            Right = Right - 'A' + 'a';

        if ((*Pattern == '?') || ((*Pattern != ANSI_NULL) && (Left == Right)))
        {
            Pattern++;
            Name++;
            continue;
        }

        if (*Pattern == '*')
        {
            Star = Pattern++;
            Mark = Name;
            continue;
        }

        /* No match here: go back to just after the last star and try again */
        if (Star == NULL)
            return FALSE;

        Pattern = Star + 1;
        Name = ++Mark;
    }

    while (*Pattern == '*')
        Pattern++;

    return (*Pattern == ANSI_NULL);
}

/*
 * The name of a loaded image, without its path, as a plain string.
 */
static
VOID
KdbpModuleName(
    _In_ PKLDR_DATA_TABLE_ENTRY LdrEntry,
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    WCHAR Wide[KDB_MODULE_NAME_MAX];
    ULONG Length;
    ULONG Index;

    if (Size == 0)
        return;

    Buffer[0] = ANSI_NULL;

    Length = min((ULONG)(LdrEntry->BaseDllName.Length / sizeof(WCHAR)), min(Size - 1, (ULONG)RTL_NUMBER_OF(Wide)));
    if ((LdrEntry->BaseDllName.Buffer == NULL) ||
        !NT_SUCCESS(KdbpSafeReadMemory(Wide, LdrEntry->BaseDllName.Buffer, Length * sizeof(WCHAR))))
    {
        return;
    }

    for (Index = 0; Index < Length; Index++)
        Buffer[Index] = ((Wide[Index] >= L' ') && (Wide[Index] < 0x7F)) ? (CHAR)Wide[Index] : '?';

    Buffer[Length] = ANSI_NULL;
}

/*
 * Whether a module is the one a pattern names: by its file name, by that name
 * without its extension, or as nt for the kernel, which is the first image of
 * the list.
 */
static
BOOLEAN
KdbpModuleMatches(
    _In_opt_ PCSTR Pattern,
    _In_ PCSTR ModuleName,
    _In_ BOOLEAN Kernel)
{
    CHAR Stem[KDB_MODULE_NAME_MAX];
    PCSTR Dot;

    if (KdbpMatchPattern(Pattern, ModuleName))
        return TRUE;

    if (Kernel && (Pattern != NULL) && (_stricmp(Pattern, "nt") == 0))
        return TRUE;

    Dot = strrchr(ModuleName, '.');
    if ((Dot == NULL) || ((SIZE_T)(Dot - ModuleName) >= sizeof(Stem)))
        return FALSE;

    RtlCopyMemory(Stem, ModuleName, (SIZE_T)(Dot - ModuleName));
    Stem[Dot - ModuleName] = ANSI_NULL;
    return KdbpMatchPattern(Pattern, Stem);
}

/*
 * The next loaded image, read through a guarded read: a list damaged by
 * whatever stopped the machine ends the walk rather than the debugger.
 */
static
PKLDR_DATA_TABLE_ENTRY
KdbpNextModule(
    _In_opt_ PLIST_ENTRY Link,
    _Inout_ PULONG Count,
    _Out_ PKLDR_DATA_TABLE_ENTRY Snapshot)
{
    PKLDR_DATA_TABLE_ENTRY LdrEntry;

    if ((Link == NULL) || (Link == &PsLoadedModuleList) || (*Count >= KDB_MAXIMUM_MODULES))
        return NULL;

    LdrEntry = CONTAINING_RECORD(Link, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
    if (!NT_SUCCESS(KdbpSafeReadMemory(Snapshot, LdrEntry, sizeof(*Snapshot))))
        return NULL;

    (*Count)++;
    return LdrEntry;
}

static
BOOLEAN
KdbpAddressInModule(
    _In_ PKLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PVOID Address)
{
    ULONG_PTR Base = (ULONG_PTR)LdrEntry->DllBase;

    return (((ULONG_PTR)Address >= Base) &&
            ((ULONG_PTR)Address < Base + LdrEntry->SizeOfImage));
}

/*
 * One symbol seen while walking an image's table, for a lookup by name.
 */
static
BOOLEAN
KdbpSymLookupRoutine(
    _In_ PVOID Context,
    _In_ ULONG Rva,
    _In_ const char *Name)
{
    PKDB_SYM_LOOKUP Lookup = (PKDB_SYM_LOOKUP)Context;

    if (_stricmp(Name, Lookup->Name) != 0)
        return TRUE;

    Lookup->Rva = Rva;
    Lookup->Found = TRUE;

    /* Found it: there is no reason to read the rest of the table */
    return FALSE;
}

static
BOOLEAN
KdbpSymEnumRoutine(
    _In_ PVOID Context,
    _In_ ULONG Rva,
    _In_ const char *Name)
{
    PKDB_SYM_ENUM Enum = (PKDB_SYM_ENUM)Context;

    if (!KdbpMatchPattern(Enum->SymbolPattern, Name))
        return TRUE;

    if (Enum->MatchCount >= Enum->MaximumMatches)
    {
        Enum->Truncated = TRUE;
        return FALSE;
    }

    Enum->MatchCount++;

    return Enum->Callback(Enum->ImageBase + Rva,
                          Enum->ModuleName,
                          Name,
                          NULL,
                          0,
                          Enum->CallbackContext);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Finds a loaded image, either the one an address falls in or the one at a
 * given place in the list. Only the system's images are searched: a stopped
 * machine is not necessarily in the address space that owns a user image.
 */
BOOLEAN
KdbpSymFindModule(
    _In_opt_ PVOID Address,
    _In_opt_ INT Index,
    _Out_ PKLDR_DATA_TABLE_ENTRY *pLdrEntry)
{
    static KLDR_DATA_TABLE_ENTRY Snapshot;
    PKLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG Count = 0;
    INT Current = 0;

    *pLdrEntry = NULL;

    for (LdrEntry = KdbpNextModule(PsLoadedModuleList.Flink, &Count, &Snapshot);
         LdrEntry != NULL;
         LdrEntry = KdbpNextModule(Snapshot.InLoadOrderLinks.Flink, &Count, &Snapshot))
    {
        if (Address != NULL)
        {
            if (KdbpAddressInModule(&Snapshot, Address))
            {
                *pLdrEntry = LdrEntry;
                return TRUE;
            }
        }
        else if (Current == Index)
        {
            *pLdrEntry = LdrEntry;
            return TRUE;
        }

        Current++;
    }

    return FALSE;
}

/*
 * Everything that is known about an address: which image it is in, which
 * function, how far into it, and where that function was written.
 */
BOOLEAN
KdbSymDescribeAddress(
    _In_ PVOID Address,
    _Out_writes_z_(ModuleNameLength) PCHAR ModuleName,
    _In_ ULONG ModuleNameLength,
    _Out_writes_z_(FunctionNameLength) PCHAR FunctionName,
    _In_ ULONG FunctionNameLength,
    _Out_ PULONG_PTR Displacement,
    _Out_writes_opt_z_(FileNameLength) PCHAR FileName,
    _In_ ULONG FileNameLength,
    _Out_opt_ PULONG Line)
{
    PKLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR SymbolAddress = 0;

    if (ModuleNameLength != 0)
        ModuleName[0] = ANSI_NULL;
    if (FunctionNameLength != 0)
        FunctionName[0] = ANSI_NULL;
    if ((FileName != NULL) && (FileNameLength != 0))
        FileName[0] = ANSI_NULL;
    if (Line != NULL)
        *Line = 0;

    *Displacement = 0;

    /* No image holds address zero, and the lookup would read it as an index */
    if ((Address == NULL) || !KdbpSymFindModule(Address, 0, &LdrEntry))
        return FALSE;

    KdbpModuleName(LdrEntry, ModuleName, ModuleNameLength);

    /* How far into the image, which is what the symbol table is indexed by */
    *Displacement = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;

    if (!KdbpSymzDescribe(LdrEntry,
                          *Displacement,
                          FunctionName,
                          FunctionNameLength,
                          &SymbolAddress,
                          FileName,
                          FileNameLength,
                          Line))
    {
        /* The image is known but has no symbols; the offset still holds */
        return TRUE;
    }

    /* Now that a function is known, the offset is measured from it */
    *Displacement = (ULONG_PTR)Address -
                    ((ULONG_PTR)LdrEntry->DllBase + SymbolAddress);

    return TRUE;
}

/*
 * Says where an address is, in the form a person reads at a prompt.
 */
BOOLEAN
KdbSymPrintAddress(
    _In_ PVOID Address,
    _In_ PCONTEXT Context)
{
    CHAR ModuleName[KDB_MODULE_NAME_MAX];
    CHAR FunctionName[KDB_SYMBOL_NAME_MAX];
    CHAR FileName[KDB_FILE_NAME_MAX];
    ULONG_PTR Displacement = 0;
    ULONG Line = 0;

    UNREFERENCED_PARAMETER(Context);

    if (!KdbSymDescribeAddress(Address,
                               ModuleName,
                               sizeof(ModuleName),
                               FunctionName,
                               sizeof(FunctionName),
                               &Displacement,
                               FileName,
                               sizeof(FileName),
                               &Line))
    {
        return FALSE;
    }

    if (FunctionName[0] == ANSI_NULL)
    {
        KdbPrintf("<%s+0x%Ix>", ModuleName, Displacement);
        return TRUE;
    }

    if ((FileName[0] != ANSI_NULL) && (Line != 0))
    {
        KdbPrintf("<%s!%s+0x%Ix (%s:%lu)>",
                  ModuleName, FunctionName, Displacement, FileName, Line);
        return TRUE;
    }

    KdbPrintf("<%s!%s+0x%Ix>", ModuleName, FunctionName, Displacement);
    return TRUE;
}

/* A return address belongs to the call before it: the name and line are those of the call */
BOOLEAN
KdbSymPrintReturnAddress(
    _In_ PVOID Address)
{
    CHAR ModuleName[KDB_MODULE_NAME_MAX];
    CHAR FunctionName[KDB_SYMBOL_NAME_MAX];
    CHAR FileName[KDB_FILE_NAME_MAX];
    ULONG_PTR Displacement = 0;
    ULONG Line = 0;

    if (!KdbSymDescribeAddress((PUCHAR)Address - 1,
                               ModuleName,
                               sizeof(ModuleName),
                               FunctionName,
                               sizeof(FunctionName),
                               &Displacement,
                               FileName,
                               sizeof(FileName),
                               &Line))
    {
        return FALSE;
    }

    if (FunctionName[0] == ANSI_NULL)
    {
        KdbPrintf("<%s+0x%Ix>", ModuleName, Displacement + 1);
        return TRUE;
    }

    if ((FileName[0] != ANSI_NULL) && (Line != 0))
    {
        KdbPrintf("<%s!%s+0x%Ix (%s:%lu)>",
                  ModuleName, FunctionName, Displacement + 1, FileName, Line);
        return TRUE;
    }

    KdbPrintf("<%s!%s+0x%Ix>", ModuleName, FunctionName, Displacement + 1);
    return TRUE;
}

/*
 * The same, for an address that is probably not the start of anything: what is
 * printed is the nearest name at or before it.
 */
BOOLEAN
KdbSymPrintNearest(
    _In_ PVOID Address,
    _In_ PCONTEXT Context)
{
    if (KdbSymPrintAddress(Address, Context))
        return TRUE;

    KdbPrintf("<%p>", Address);
    return FALSE;
}

/*
 * Walks the symbols of every image whose name matches, handing each matching
 * symbol to the caller. Stops once the caller has had as many as it asked
 * for, and says so.
 */
NTSTATUS
KdbSymEnumerate(
    _In_ PCSTR ModulePattern,
    _In_ PCSTR SymbolPattern,
    _In_ ULONG MaximumMatches,
    _In_ PKDB_SYMBOL_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PULONG MatchCount,
    _Out_ PBOOLEAN Truncated)
{
    static KLDR_DATA_TABLE_ENTRY Snapshot;
    PKLDR_DATA_TABLE_ENTRY LdrEntry;
    KDB_SYM_ENUM Enum;
    CHAR ModuleName[KDB_MODULE_NAME_MAX];
    ULONG Count = 0;

    *MatchCount = 0;
    *Truncated = FALSE;

    if ((Callback == NULL) || (MaximumMatches == 0))
        return STATUS_INVALID_PARAMETER;

    Enum.SymbolPattern = SymbolPattern;
    Enum.MaximumMatches = MaximumMatches;
    Enum.MatchCount = 0;
    Enum.Truncated = FALSE;
    Enum.Callback = Callback;
    Enum.CallbackContext = Context;

    for (LdrEntry = KdbpNextModule(PsLoadedModuleList.Flink, &Count, &Snapshot);
         LdrEntry != NULL;
         LdrEntry = KdbpNextModule(Snapshot.InLoadOrderLinks.Flink, &Count, &Snapshot))
    {
        KdbpModuleName(&Snapshot, ModuleName, sizeof(ModuleName));

        if (!KdbpModuleMatches(ModulePattern, ModuleName, (Count == 1)))
            continue;

        Enum.ModuleName = ModuleName;
        Enum.ImageBase = (ULONG_PTR)Snapshot.DllBase;

        KdbpSymzEnumerate(LdrEntry, KdbpSymEnumRoutine, &Enum);

        if (Enum.Truncated)
            break;
    }

    *MatchCount = Enum.MatchCount;
    *Truncated = Enum.Truncated;

    return (Enum.MatchCount != 0) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/*
 * The address a name stands for. A name with no image in front of it is looked
 * for in every image, the system's own first, since that is where a person
 * typing a bare name most often means.
 */
BOOLEAN
KdbpSymLookupName(
    _In_ PCSTR Name,
    _Out_ PULONG_PTR Address)
{
    static KLDR_DATA_TABLE_ENTRY Snapshot;
    PKLDR_DATA_TABLE_ENTRY LdrEntry;
    KDB_SYM_LOOKUP Lookup;
    CHAR ModuleName[KDB_MODULE_NAME_MAX];
    CHAR Wanted[KDB_MODULE_NAME_MAX];
    PCSTR SymbolName = Name;
    PCSTR Separator;
    ULONG Count = 0;

    *Address = 0;

    if ((Name == NULL) || (*Name == ANSI_NULL))
        return FALSE;

    /* `module!symbol` says which image to look in */
    Separator = strchr(Name, '!');
    if (Separator != NULL)
    {
        SIZE_T Length = (SIZE_T)(Separator - Name);

        if (Length >= sizeof(Wanted))
            return FALSE;

        RtlCopyMemory(Wanted, Name, Length);
        Wanted[Length] = ANSI_NULL;
        SymbolName = Separator + 1;
    }

    for (LdrEntry = KdbpNextModule(PsLoadedModuleList.Flink, &Count, &Snapshot);
         LdrEntry != NULL;
         LdrEntry = KdbpNextModule(Snapshot.InLoadOrderLinks.Flink, &Count, &Snapshot))
    {
        if (Separator != NULL)
        {
            KdbpModuleName(&Snapshot, ModuleName, sizeof(ModuleName));
            if (!KdbpModuleMatches(Wanted, ModuleName, (Count == 1)))
                continue;
        }

        Lookup.Name = SymbolName;
        Lookup.Rva = 0;
        Lookup.Found = FALSE;

        KdbpSymzEnumerate(LdrEntry, KdbpSymLookupRoutine, &Lookup);

        if (Lookup.Found)
        {
            *Address = (ULONG_PTR)Snapshot.DllBase + Lookup.Rva;
            return TRUE;
        }
    }

    return FALSE;
}

/* EOF */
