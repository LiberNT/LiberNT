/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for PfxInitialize, PfxInsertPrefix, PfxRemovePrefix and PfxFindPrefix
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef VOID (NTAPI *FN_PfxInitialize)(PPREFIX_TABLE);
typedef BOOLEAN (NTAPI *FN_PfxInsertPrefix)(PPREFIX_TABLE, PSTRING, PPREFIX_TABLE_ENTRY);
typedef VOID (NTAPI *FN_PfxRemovePrefix)(PPREFIX_TABLE, PPREFIX_TABLE_ENTRY);
typedef PPREFIX_TABLE_ENTRY (NTAPI *FN_PfxFindPrefix)(PPREFIX_TABLE, PSTRING);

static FN_PfxInitialize pPfxInitialize;
static FN_PfxInsertPrefix pPfxInsertPrefix;
static FN_PfxRemovePrefix pPfxRemovePrefix;
static FN_PfxFindPrefix pPfxFindPrefix;

static
PPREFIX_TABLE_ENTRY
FindName(
    _In_ PPREFIX_TABLE Table,
    _In_ PCSTR Name)
{
    STRING FullName;

    RtlInitString(&FullName, Name);
    return pPfxFindPrefix(Table, &FullName);
}

static
VOID
TestBasicTable(VOID)
{
    PREFIX_TABLE Table;
    PREFIX_TABLE_ENTRY EntryA, EntryAB, EntryCDE, EntryDuplicate, EntryRoot;
    STRING NameA, NameAB, NameCDE, NameDuplicate, NameRoot;
    CHAR DuplicateBuffer[] = "\\a";

    RtlInitString(&NameA, "\\a");
    RtlInitString(&NameAB, "\\a\\b");
    RtlInitString(&NameCDE, "\\c\\d\\e");
    RtlInitString(&NameDuplicate, DuplicateBuffer);
    RtlInitString(&NameRoot, "\\");

    pPfxInitialize(&Table);
    ok(FindName(&Table, "\\a") == NULL, "Empty table returned an entry\n");
    ok(FindName(&Table, "\\") == NULL, "Empty table returned an entry for the root\n");

    ok(pPfxInsertPrefix(&Table, &NameA, &EntryA) == TRUE, "Insert \\a failed\n");
    ok(pPfxInsertPrefix(&Table, &NameAB, &EntryAB) == TRUE, "Insert \\a\\b failed\n");
    ok(pPfxInsertPrefix(&Table, &NameCDE, &EntryCDE) == TRUE, "Insert \\c\\d\\e failed\n");
    ok(pPfxInsertPrefix(&Table, &NameDuplicate, &EntryDuplicate) == FALSE, "Duplicate \\a was inserted\n");

    ok(FindName(&Table, "\\a") == &EntryA, "\\a did not find its own entry\n");
    ok(FindName(&Table, "\\a\\b") == &EntryAB, "\\a\\b did not find its own entry\n");
    ok(FindName(&Table, "\\a\\b\\c") == &EntryAB, "\\a\\b\\c did not find the longest prefix \\a\\b\n");
    ok(FindName(&Table, "\\a\\x") == &EntryA, "\\a\\x did not find the prefix \\a\n");
    ok(FindName(&Table, "\\a\\bc") == &EntryA, "\\a\\bc matched inside a name component\n");
    ok(FindName(&Table, "\\ab") == NULL, "\\ab matched inside a name component\n");
    ok(FindName(&Table, "\\c\\d") == NULL, "\\c\\d matched the longer prefix \\c\\d\\e\n");
    ok(FindName(&Table, "\\c\\d\\e\\f\\g") == &EntryCDE, "\\c\\d\\e\\f\\g did not find \\c\\d\\e\n");
    ok(FindName(&Table, "\\A") == NULL, "\\A matched \\a without case sensitivity\n");
    ok(FindName(&Table, "\\x") == NULL, "\\x matched an entry\n");
    ok(EntryA.Prefix == &NameA, "Entry prefix is %p, expected %p\n", EntryA.Prefix, &NameA);

    ok(pPfxInsertPrefix(&Table, &NameRoot, &EntryRoot) == TRUE, "Insert \\ failed\n");
    ok(FindName(&Table, "\\x\\y") == &EntryRoot, "\\x\\y did not find the root prefix\n");
    ok(FindName(&Table, "\\") == &EntryRoot, "\\ did not find its own entry\n");
    ok(FindName(&Table, "\\a\\b\\c") == &EntryAB, "Root prefix hid the longer prefix \\a\\b\n");

    pPfxRemovePrefix(&Table, &EntryAB);
    ok(FindName(&Table, "\\a\\b\\c") == &EntryA, "\\a\\b\\c did not fall back to \\a after removal\n");
    pPfxRemovePrefix(&Table, &EntryA);
    ok(FindName(&Table, "\\a\\b") == &EntryRoot, "\\a\\b did not fall back to the root after removal\n");
    ok(FindName(&Table, "\\c\\d\\e") == &EntryCDE, "Removal lost \\c\\d\\e\n");
    pPfxRemovePrefix(&Table, &EntryRoot);
    ok(FindName(&Table, "\\a") == NULL, "\\a found an entry in a table without a matching prefix\n");

    ok(pPfxInsertPrefix(&Table, &NameDuplicate, &EntryDuplicate) == TRUE, "Reinsert of \\a after removal failed\n");
    ok(FindName(&Table, "\\a\\b") == &EntryDuplicate, "Reinserted \\a not found\n");
    pPfxRemovePrefix(&Table, &EntryDuplicate);
    pPfxRemovePrefix(&Table, &EntryCDE);
    ok(FindName(&Table, "\\c\\d\\e") == NULL, "Emptied table returned an entry\n");
}

static
VOID
TestManyEntries(VOID)
{
    static const UCHAR Order[] = { 7, 3, 11, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14, 15 };
    PREFIX_TABLE Table;
    PREFIX_TABLE_ENTRY Entries[16];
    STRING Names[16];
    CHAR Buffers[16][8];
    CHAR Lookup[16];
    ULONG Index;

    pPfxInitialize(&Table);
    for (Index = 0; Index < RTL_NUMBER_OF(Order); Index++)
    {
        StringCchPrintfA(Buffers[Order[Index]], sizeof(Buffers[0]), "\\d%02u", Order[Index]);
        RtlInitString(&Names[Order[Index]], Buffers[Order[Index]]);
        ok(pPfxInsertPrefix(&Table, &Names[Order[Index]], &Entries[Order[Index]]) == TRUE,
           "Insert %s failed\n", Buffers[Order[Index]]);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Entries); Index++)
    {
        StringCchPrintfA(Lookup, sizeof(Lookup), "\\d%02lu\\file", Index);
        ok(FindName(&Table, Lookup) == &Entries[Index], "%s did not find its prefix\n", Lookup);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Entries); Index += 2)
        pPfxRemovePrefix(&Table, &Entries[Index]);

    for (Index = 0; Index < RTL_NUMBER_OF(Entries); Index++)
    {
        StringCchPrintfA(Lookup, sizeof(Lookup), "\\d%02lu", Index);
        if (Index % 2)
            ok(FindName(&Table, Lookup) == &Entries[Index], "%s lost after removing its neighbours\n", Lookup);
        else
            ok(FindName(&Table, Lookup) == NULL, "%s found after its removal\n", Lookup);
    }
}

START_TEST(PfxPrefixTable)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    pPfxInitialize = (FN_PfxInitialize)GetProcAddress(Ntdll, "PfxInitialize");
    pPfxInsertPrefix = (FN_PfxInsertPrefix)GetProcAddress(Ntdll, "PfxInsertPrefix");
    pPfxRemovePrefix = (FN_PfxRemovePrefix)GetProcAddress(Ntdll, "PfxRemovePrefix");
    pPfxFindPrefix = (FN_PfxFindPrefix)GetProcAddress(Ntdll, "PfxFindPrefix");
    if (!pPfxInitialize || !pPfxInsertPrefix || !pPfxRemovePrefix || !pPfxFindPrefix)
    {
        skip("Pfx prefix table functions are not exported\n");
        return;
    }

    TestBasicTable();
    TestManyEntries();
}
