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
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    ULONG Length = 0;

    Buffer[0] = ANSI_NULL;

    if ((LdrEntry->BaseDllName.Buffer == NULL) || (Size == 0))
        return;

    while ((Length < (ULONG)(LdrEntry->BaseDllName.Length / sizeof(WCHAR))) &&
           (Length < Size - 1))
    {
        Buffer[Length] = (CHAR)LdrEntry->BaseDllName.Buffer[Length];
        Length++;
    }

    Buffer[Length] = ANSI_NULL;
}

static
BOOLEAN
KdbpAddressInModule(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
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
    _Out_ PLDR_DATA_TABLE_ENTRY *pLdrEntry)
{
    PLIST_ENTRY Entry;
    INT Current = 0;

    *pLdrEntry = NULL;

    if (PsLoadedModuleList.Flink == NULL)
        return FALSE;

    for (Entry = PsLoadedModuleList.Flink;
         Entry != &PsLoadedModuleList;
         Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        LdrEntry = CONTAINING_RECORD(Entry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (Address != NULL)
        {
            if (KdbpAddressInModule(LdrEntry, Address))
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
    PLDR_DATA_TABLE_ENTRY LdrEntry;
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

    if (!KdbpSymFindModule(Address, 0, &LdrEntry))
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
    PLIST_ENTRY Entry;
    KDB_SYM_ENUM Enum;
    CHAR ModuleName[KDB_MODULE_NAME_MAX];

    *MatchCount = 0;
    *Truncated = FALSE;

    if ((Callback == NULL) || (MaximumMatches == 0))
        return STATUS_INVALID_PARAMETER;

    if (PsLoadedModuleList.Flink == NULL)
        return STATUS_NOT_FOUND;

    Enum.SymbolPattern = SymbolPattern;
    Enum.MaximumMatches = MaximumMatches;
    Enum.MatchCount = 0;
    Enum.Truncated = FALSE;
    Enum.Callback = Callback;
    Enum.CallbackContext = Context;

    for (Entry = PsLoadedModuleList.Flink;
         Entry != &PsLoadedModuleList;
         Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        LdrEntry = CONTAINING_RECORD(Entry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        KdbpModuleName(LdrEntry, ModuleName, sizeof(ModuleName));

        if (!KdbpMatchPattern(ModulePattern, ModuleName))
            continue;

        Enum.ModuleName = ModuleName;
        Enum.ImageBase = (ULONG_PTR)LdrEntry->DllBase;

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
    PLIST_ENTRY Entry;
    KDB_SYM_LOOKUP Lookup;
    CHAR ModuleName[KDB_MODULE_NAME_MAX];
    PCSTR SymbolName = Name;
    PCSTR Separator;

    *Address = 0;

    if ((Name == NULL) || (*Name == ANSI_NULL))
        return FALSE;

    if (PsLoadedModuleList.Flink == NULL)
        return FALSE;

    /* `module!symbol` says which image to look in */
    Separator = strchr(Name, '!');
    if (Separator != NULL)
        SymbolName = Separator + 1;

    for (Entry = PsLoadedModuleList.Flink;
         Entry != &PsLoadedModuleList;
         Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        LdrEntry = CONTAINING_RECORD(Entry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        if (Separator != NULL)
        {
            CHAR Wanted[KDB_MODULE_NAME_MAX];
            SIZE_T Length = (SIZE_T)(Separator - Name);

            if (Length >= sizeof(Wanted))
                return FALSE;

            RtlCopyMemory(Wanted, Name, Length);
            Wanted[Length] = ANSI_NULL;

            KdbpModuleName(LdrEntry, ModuleName, sizeof(ModuleName));

            if (!KdbpMatchPattern(Wanted, ModuleName))
                continue;
        }

        Lookup.Name = SymbolName;
        Lookup.Rva = 0;
        Lookup.Found = FALSE;

        KdbpSymzEnumerate(LdrEntry, KdbpSymLookupRoutine, &Lookup);

        if (Lookup.Found)
        {
            *Address = (ULONG_PTR)LdrEntry->DllBase + Lookup.Rva;
            return TRUE;
        }
    }

    return FALSE;
}

/* EOF */
