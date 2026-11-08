/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kdbg/kdb_symbols.c
 * PURPOSE:         Getting symbol information...
 *
 * PROGRAMMERS:     David Welch (welch@cwcom.net)
 *                  Colin Finck (colin@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include "kdb.h"

#define NDEBUG
#include "debug.h"

/* GLOBALS ******************************************************************/

#define KDB_MAX_MODULES             4096
#define KDB_MAX_IMAGE_SECTIONS      96
#define KDB_MAX_SYMBOL_SCAN         (8 * 1024 * 1024)
#define KDB_MAX_SYMBOL_NAME         512

/* FUNCTIONS ****************************************************************/

static
BOOLEAN
KdbpSymSearchModuleList(
    IN PLIST_ENTRY current_entry,
    IN PLIST_ENTRY end_entry,
    IN PLONG Count,
    IN PVOID Address,
    IN INT Index,
    OUT PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    ULONG Iterations = 0;

    while (current_entry && current_entry != end_entry)
    {
        LIST_ENTRY Links;
        LDR_DATA_TABLE_ENTRY Entry;
        PLDR_DATA_TABLE_ENTRY LdrEntry;

        /* Bound the walk: the list is read lock-free from the debugger and may be torn */
        if (++Iterations > KDB_MAX_MODULES)
            break;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, current_entry, sizeof(Links))))
            break;

        LdrEntry = CONTAINING_RECORD(current_entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, LdrEntry, sizeof(Entry))))
            break;

        if ((Address &&
             (ULONG_PTR)Address >= (ULONG_PTR)Entry.DllBase &&
             (ULONG_PTR)Address - (ULONG_PTR)Entry.DllBase < Entry.SizeOfImage) ||
            (Index >= 0 && (*Count)++ == Index))
        {
            *pLdrEntry = LdrEntry;
            return TRUE;
        }

        if (Links.Flink == current_entry)
            break;
        current_entry = Links.Flink;
    }

    return FALSE;
}

/*! \brief Find a module...
 *
 * \param Address      If \a Address is not NULL the module containing \a Address
 *                     is searched.
 * \param Name         If \a Name is not NULL the module named \a Name will be
 *                     searched.
 * \param Index        If \a Index is >= 0 the Index'th module will be returned.
 * \param pLdrEntry    Pointer to a PLDR_DATA_TABLE_ENTRY which is filled.
 *
 * \retval TRUE    Module was found, \a pLdrEntry was filled.
 * \retval FALSE   No module was found.
 */
BOOLEAN
KdbpSymFindModule(
    IN PVOID Address  OPTIONAL,
    IN INT Index  OPTIONAL,
    OUT PLDR_DATA_TABLE_ENTRY* pLdrEntry)
{
    LONG Count = 0;
    PEPROCESS CurrentProcess;
    PPEB PebAddress;
    PEB Peb;
    PPEB_LDR_DATA LdrAddress;
    PEB_LDR_DATA Ldr;
    PLIST_ENTRY LdrListHead;
    LIST_ENTRY KernelListHead;
    EPROCESS ProcessSnapshot;

    /*
     * First try to look up the module in the kernel module list.
     * NOTE: deliberately lock-free. KDB runs with the machine frozen; taking
     * PsLoadedModuleSpinLock here deadlocks the debugger whenever the lock
     * was held at exception time (common for faults at IRQL >= DISPATCH).
     * The walk is bounded in KdbpSymSearchModuleList to survive a torn list.
     */
    if (NT_SUCCESS(KdbpSafeReadMemory(&KernelListHead, &PsLoadedModuleList, sizeof(KernelListHead))) &&
        KdbpSymSearchModuleList(KernelListHead.Flink, &PsLoadedModuleList, &Count, Address, Index, pLdrEntry))
    {
        return TRUE;
    }

    /* That didn't succeed. Try the module list of the current process now. */
    CurrentProcess = PsGetCurrentProcess();

    if (!CurrentProcess)
        return FALSE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, CurrentProcess, sizeof(ProcessSnapshot))))
    {
        return FALSE;
    }

    PebAddress = ProcessSnapshot.Peb;
    if (!PebAddress ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Peb, PebAddress, sizeof(Peb))))
    {
        return FALSE;
    }

    LdrAddress = Peb.Ldr;
    if (!LdrAddress ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Ldr, LdrAddress, sizeof(Ldr))))
    {
        return FALSE;
    }

    LdrListHead = (PLIST_ENTRY)((PUCHAR)LdrAddress + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList));

    return KdbpSymSearchModuleList(Ldr.InLoadOrderModuleList.Flink, LdrListHead, &Count, Address, Index, pLdrEntry);
}

static
PCHAR
NTAPI
KdbpSymUnicodeToAnsi(IN PUNICODE_STRING Unicode,
                     OUT PCHAR Ansi,
                     IN ULONG Length)
{
    WCHAR Wide[128];
    PCHAR p;
    PWCHAR pw;
    ULONG i;

    if (Length == 0)
        return Ansi;

    Ansi[0] = ANSI_NULL;
    if (Unicode == NULL || Unicode->Buffer == NULL ||
        (Unicode->Length & (sizeof(WCHAR) - 1)) != 0)
    {
        return Ansi;
    }

    /* Set length and normalize it */
    i = Unicode->Length / sizeof(WCHAR);
    i = min(i, Length - 1);
    i = min(i, RTL_NUMBER_OF(Wide));

    if (i != 0 &&
        !NT_SUCCESS(KdbpSafeReadMemory(Wide, Unicode->Buffer, i * sizeof(WCHAR))))
    {
        return Ansi;
    }

    /* Set source and destination, and copy */
    p = Ansi;
    pw = Wide;
    while (i--) *p++ = (CHAR)*pw++;

    /* Null terminate and return */
    *p = ANSI_NULL;
    return Ansi;
}

/*! \brief Print address...
 *
 * Tries to lookup line number, file name and function name for the given
 * address and prints it.
 * If no such information is found the address is printed in the format
 * <module: offset>, otherwise the format will be
 * <module: offset (filename:linenumber (functionname))>
 *
 * \retval TRUE  Module containing \a Address was found, \a Address was printed.
 * \retval FALSE  No module containing \a Address was found, nothing was printed.
 */
BOOLEAN
KdbSymPrintAddress(
    IN PVOID Address,
    IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntryAddress;
    LDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    ULONG_PTR SymbolAddress;
    ULONG Line;
    CHAR ModuleNameAnsi[64];
    CHAR FunctionName[256];
    CHAR FileName[256];

    UNREFERENCED_PARAMETER(Context);

    if (!KdbpSymFindModule(Address, -1, &LdrEntryAddress) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&LdrEntry, LdrEntryAddress, sizeof(LdrEntry))))
    {
        return FALSE;
    }

    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry.DllBase;

    KdbpSymUnicodeToAnsi(&LdrEntry.BaseDllName, ModuleNameAnsi, sizeof(ModuleNameAnsi));

    /* Print the module and offset first so the line is visible even if the
     * symbol data lookup below faults or stalls inside the frozen debugger */
    KdbPrintf("<%s:%Ix", ModuleNameAnsi, RelativeAddress);

    if (KdbpSymzDescribe(&LdrEntry, RelativeAddress, FunctionName, sizeof(FunctionName), &SymbolAddress, FileName, sizeof(FileName), &Line))
    {
        if (Line != 0)
            KdbPrintf(" (%s:%lu (%s+0x%Ix))", FileName, Line, FunctionName, RelativeAddress - SymbolAddress);
        else
            KdbPrintf(" (%s+0x%Ix)", FunctionName, RelativeAddress - SymbolAddress);
    }

    KdbPrintf(">");
    return TRUE;
}

static
BOOLEAN
KdbpSymGlobMatch(IN PCSTR Pattern, IN PCSTR String)
{
    PCSTR Star = NULL;
    PCSTR Retry = NULL;

    while (*String)
    {
        if (*Pattern == '?' ||
            (*Pattern != ANSI_NULL &&
             tolower((UCHAR)*Pattern) == tolower((UCHAR)*String)))
        {
            Pattern++;
            String++;
        }
        else if (*Pattern == '*')
        {
            Star = Pattern++;
            Retry = String;
        }
        else if (Star != NULL)
        {
            Pattern = Star + 1;
            String = ++Retry;
        }
        else
        {
            return FALSE;
        }
    }

    while (*Pattern == '*')
        Pattern++;
    return *Pattern == ANSI_NULL;
}

typedef struct _KDB_SYMBOL_ENUM_CONTEXT
{
    PCSTR ModulePattern;
    PCSTR SymbolPattern;
    ULONG MaximumMatches;
    ULONG Matches;
    ULONG Scanned;
    BOOLEAN Truncated;
    BOOLEAN Stop;
    PKDB_SYMBOL_ENUM_CALLBACK Callback;
    PVOID CallbackContext;
} KDB_SYMBOL_ENUM_CONTEXT, *PKDB_SYMBOL_ENUM_CONTEXT;

typedef struct _KDB_SYMBOL_MODULE_CONTEXT
{
    PKDB_SYMBOL_ENUM_CONTEXT Enum;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PCSTR ModuleName;
} KDB_SYMBOL_MODULE_CONTEXT, *PKDB_SYMBOL_MODULE_CONTEXT;

static
BOOLEAN
KdbpSymEnumerateSymbol(PVOID Context, ULONG Rva, const char *Name)
{
    PKDB_SYMBOL_MODULE_CONTEXT Module = Context;
    PKDB_SYMBOL_ENUM_CONTEXT Enum = Module->Enum;

    if (Enum->Scanned++ >= KDB_MAX_SYMBOL_SCAN)
    {
        Enum->Truncated = TRUE;
        Enum->Stop = TRUE;
        return FALSE;
    }

    if (!KdbpSymGlobMatch(Enum->SymbolPattern, Name) ||
        Rva >= Module->LdrEntry->SizeOfImage)
    {
        return TRUE;
    }

    if (Enum->Matches >= Enum->MaximumMatches)
    {
        Enum->Truncated = TRUE;
        Enum->Stop = TRUE;
        return FALSE;
    }

    Enum->Matches++;
    if (!Enum->Callback((ULONG_PTR)Module->LdrEntry->DllBase + Rva, Module->ModuleName, Name, "", 0, Enum->CallbackContext))
    {
        Enum->Stop = TRUE;
        return FALSE;
    }

    return TRUE;
}

static
VOID
KdbpSymEnumerateModule(IN PLDR_DATA_TABLE_ENTRY LdrEntryAddress, IN PLDR_DATA_TABLE_ENTRY LdrEntry, IN OUT PKDB_SYMBOL_ENUM_CONTEXT Enum)
{
    KDB_SYMBOL_MODULE_CONTEXT Module;
    CHAR ModuleName[128];

    UNREFERENCED_PARAMETER(LdrEntryAddress);

    KdbpSymUnicodeToAnsi(&LdrEntry->BaseDllName, ModuleName, sizeof(ModuleName));
    if (Enum->Stop || ModuleName[0] == ANSI_NULL ||
        !KdbpSymGlobMatch(Enum->ModulePattern, ModuleName))
    {
        return;
    }

    Module.Enum = Enum;
    Module.LdrEntry = LdrEntry;
    Module.ModuleName = ModuleName;
    if (!KdbpSymzEnumerate(LdrEntry, KdbpSymEnumerateSymbol, &Module) && !Enum->Stop)
        Enum->Truncated = TRUE;
}

static
VOID
KdbpSymEnumerateModuleList(IN PLIST_ENTRY CurrentEntry, IN PLIST_ENTRY EndEntry, IN OUT PKDB_SYMBOL_ENUM_CONTEXT Enum)
{
    ULONG Iterations = 0;

    while (!Enum->Stop && CurrentEntry != NULL && CurrentEntry != EndEntry)
    {
        LIST_ENTRY Links;
        LDR_DATA_TABLE_ENTRY Entry;
        PLDR_DATA_TABLE_ENTRY EntryAddress;

        if (++Iterations > KDB_MAX_MODULES ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Links, CurrentEntry, sizeof(Links))))
        {
            Enum->Truncated = TRUE;
            return;
        }

        EntryAddress = CONTAINING_RECORD(CurrentEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, EntryAddress, sizeof(Entry))))
        {
            Enum->Truncated = TRUE;
            return;
        }

        KdbpSymEnumerateModule(EntryAddress, &Entry, Enum);
        if (Links.Flink == CurrentEntry)
        {
            Enum->Truncated = TRUE;
            return;
        }
        CurrentEntry = Links.Flink;
    }
}

NTSTATUS
KdbSymEnumerate(IN PCSTR ModulePattern, IN PCSTR SymbolPattern, IN ULONG MaximumMatches, IN PKDB_SYMBOL_ENUM_CALLBACK Callback, IN PVOID Context OPTIONAL, OUT PULONG MatchCount, OUT PBOOLEAN Truncated)
{
    KDB_SYMBOL_ENUM_CONTEXT Enum;
    PEPROCESS Process;
    PEB Peb;
    PEB_LDR_DATA Ldr;
    PPEB PebAddress;
    PPEB_LDR_DATA LdrAddress;
    PLIST_ENTRY LdrListHead;
    LIST_ENTRY KernelListHead;
    EPROCESS ProcessSnapshot;

    if (ModulePattern == NULL || SymbolPattern == NULL ||
        MaximumMatches == 0 || Callback == NULL ||
        MatchCount == NULL || Truncated == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Enum, sizeof(Enum));
    Enum.ModulePattern = ModulePattern;
    Enum.SymbolPattern = SymbolPattern;
    Enum.MaximumMatches = MaximumMatches;
    Enum.Callback = Callback;
    Enum.CallbackContext = Context;

    if (NT_SUCCESS(KdbpSafeReadMemory(&KernelListHead, &PsLoadedModuleList, sizeof(KernelListHead))))
    {
        KdbpSymEnumerateModuleList(KernelListHead.Flink, &PsLoadedModuleList, &Enum);
    }
    else
    {
        Enum.Truncated = TRUE;
    }

    Process = PsGetCurrentProcess();
    if (!Enum.Stop && Process != NULL)
    {
        if (NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
            PebAddress = ProcessSnapshot.Peb;
        else
            PebAddress = NULL;
        if (PebAddress != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&Peb, PebAddress, sizeof(Peb))))
        {
            LdrAddress = Peb.Ldr;
            if (LdrAddress != NULL &&
                NT_SUCCESS(KdbpSafeReadMemory(&Ldr, LdrAddress, sizeof(Ldr))))
            {
                LdrListHead = (PLIST_ENTRY)((PUCHAR)LdrAddress + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList));
                KdbpSymEnumerateModuleList(Ldr.InLoadOrderModuleList.Flink, LdrListHead, &Enum);
            }
        }
    }

    *MatchCount = Enum.Matches;
    *Truncated = Enum.Truncated;
    return STATUS_SUCCESS;
}

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
    PLDR_DATA_TABLE_ENTRY LdrEntryAddress;
    LDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    ULONG_PTR SymbolAddress;

    if ((ModuleName == NULL) || (ModuleNameLength == 0) || (FunctionName == NULL) || (FunctionNameLength == 0) || (Displacement == NULL))
        return FALSE;

    ModuleName[0] = ANSI_NULL;
    FunctionName[0] = ANSI_NULL;
    *Displacement = 0;
    if (FileName && FileNameLength != 0)
        FileName[0] = ANSI_NULL;
    if (Line)
        *Line = 0;

    if (!KdbpSymFindModule(Address, -1, &LdrEntryAddress) || !NT_SUCCESS(KdbpSafeReadMemory(&LdrEntry, LdrEntryAddress, sizeof(LdrEntry))))
        return FALSE;

    KdbpSymUnicodeToAnsi(&LdrEntry.BaseDllName, ModuleName, ModuleNameLength);
    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry.DllBase;
    *Displacement = RelativeAddress;

    if (KdbpSymzDescribe(&LdrEntry, RelativeAddress, FunctionName, FunctionNameLength, &SymbolAddress, FileName, FileNameLength, Line))
        *Displacement = RelativeAddress - SymbolAddress;
    else
        FunctionName[0] = ANSI_NULL;

    return TRUE;
}

BOOLEAN
KdbSymPrintNearest(IN PVOID Address, IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntryAddress;
    LDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG_PTR RelativeAddress;
    ULONG_PTR SymbolAddress;
    ULONG Line;
    CHAR ModuleName[128];
    CHAR FunctionName[KDB_MAX_SYMBOL_NAME];
    CHAR FileName[KDB_MAX_SYMBOL_NAME];

    UNREFERENCED_PARAMETER(Context);

    if (!KdbpSymFindModule(Address, -1, &LdrEntryAddress) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&LdrEntry, LdrEntryAddress, sizeof(LdrEntry))))
    {
        return FALSE;
    }

    KdbpSymUnicodeToAnsi(&LdrEntry.BaseDllName, ModuleName, sizeof(ModuleName));
    RelativeAddress = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry.DllBase;

    if (KdbpSymzDescribe(&LdrEntry, RelativeAddress, FunctionName, sizeof(FunctionName), &SymbolAddress, FileName, sizeof(FileName), &Line))
        KdbpPrint("%p %s!%s+0x%Ix [%s:%lu]\n", Address, ModuleName, FunctionName, RelativeAddress - SymbolAddress, Line ? FileName : "?", Line);
    else
        KdbpPrint("%p %s+0x%Ix (no symbol)\n", Address, ModuleName, RelativeAddress);

    return TRUE;
}
