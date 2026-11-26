/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RtlRb* and the loader's base address index of the loaded modules
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <ndk/rtlfuncs.h>
#include <ndk/ldrtypes.h>
#include <ndk/pstypes.h>

typedef struct _RB_TREE_ { PRTL_BALANCED_NODE Root; PRTL_BALANCED_NODE Min; } RB_TREE_, *PRB_TREE_;
typedef VOID (NTAPI *PINSERT)(PRB_TREE_, PRTL_BALANCED_NODE, BOOLEAN, PRTL_BALANCED_NODE);
typedef VOID (NTAPI *PREMOVE)(PRB_TREE_, PRTL_BALANCED_NODE);

typedef struct { RTL_BALANCED_NODE Node; int Key; } ITEM;

static int Walk(PRTL_BALANCED_NODE n, int *last, int *ok_order)
{
    int c = 0;
    if (!n) return 0;
    c += Walk(n->Left, last, ok_order);
    if (((ITEM *)n)->Key < *last) *ok_order = 0;
    *last = ((ITEM *)n)->Key;
    c++;
    c += Walk(n->Right, last, ok_order);
    return c;
}

static int WalkLdr(PRTL_BALANCED_NODE n, ULONG_PTR *last, int *ok_order, int depth)
{
    int c = 0;
    PLDR_DATA_TABLE_ENTRY e;
    if (!n || depth > 64) return 0;
    c += WalkLdr(n->Left, last, ok_order, depth + 1);
    e = CONTAINING_RECORD(n, LDR_DATA_TABLE_ENTRY, BaseAddressIndexNode);
    if ((ULONG_PTR)e->DllBase < *last) *ok_order = 0;
    *last = (ULONG_PTR)e->DllBase;
    c++;
    c += WalkLdr(n->Right, last, ok_order, depth + 1);
    return c;
}

START_TEST(LdrBaseAddressIndex)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PINSERT Insert = (PINSERT)GetProcAddress(Ntdll, "RtlRbInsertNodeEx");
    PREMOVE Remove = (PREMOVE)GetProcAddress(Ntdll, "RtlRbRemoveNode");
    static ITEM Items[64];
    RB_TREE_ Tree = { 0 };
    int i, last, order, count, modules = 0;
    PLIST_ENTRY Head, Entry;
    PRTL_BALANCED_NODE Root, n;
    ULONG_PTR lastBase;

    ok(Insert != NULL, "RtlRbInsertNodeEx is not exported\n");
    ok(Remove != NULL, "RtlRbRemoveNode is not exported\n");
    if (Insert && Remove)
    {
        for (i = 0; i < 64; i++)
        {
            int key = (i * 37) % 64;
            PRTL_BALANCED_NODE Parent = NULL;
            BOOLEAN Right = FALSE;
            Items[i].Key = key;
            n = Tree.Root;
            while (n)
            {
                Parent = n;
                Right = key > ((ITEM *)n)->Key;
                n = Right ? n->Right : n->Left;
            }
            Insert(&Tree, Parent, Right, &Items[i].Node);
        }
        last = -1; order = 1;
        count = Walk(Tree.Root, &last, &order);
        ok(count == 64 && order, "after inserts: %d nodes, ordered %d\n", count, order);
        for (i = 0; i < 64; i += 2)
            Remove(&Tree, &Items[i].Node);
        last = -1; order = 1;
        count = Walk(Tree.Root, &last, &order);
        ok(count == 32 && order, "after removals: %d nodes, ordered %d\n", count, order);
    }

    {
        static const PCWSTR Dlls[] = { L"ws2_32.dll", L"crypt32.dll", L"ole32.dll", L"shlwapi.dll", L"imagehlp.dll" };
        HMODULE Loaded[5];
        int round, d, bad = 0;
        for (round = 0; round < 20 && !bad; round++)
        {
            for (d = 0; d < 5; d++)
                Loaded[d] = LoadLibraryW(Dlls[d]);
            for (d = 4; d >= 0; d--)
                if (Loaded[d] && (round & 1 || d & 1))
                    FreeLibrary(Loaded[d]);
            Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
            modules = 0; Root = NULL;
            for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
            {
                PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                modules++;
                n = &e->BaseAddressIndexNode;
                for (i = 0; i < 64 && (n->ParentValue & ~(ULONG_PTR)3); i++)
                    n = (PRTL_BALANCED_NODE)(n->ParentValue & ~(ULONG_PTR)3);
                if (!Root) Root = n; else if (Root != n) bad++;
            }
            lastBase = 0; order = 1;
            count = WalkLdr(Root, &lastBase, &order, 0);
            if (count != modules || !order) bad++;
            if (bad) ok(0, "round %d: %d of %d modules indexed, ordered %d\n", round, count, modules, order);
        }
        ok(!bad, "the base address index broke while DLLs were loaded and freed\n");
        modules = 0;
    }

    Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    Root = NULL;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        modules++;
        n = &e->BaseAddressIndexNode;
        for (i = 0; i < 64 && (n->ParentValue & ~(ULONG_PTR)3); i++)
            n = (PRTL_BALANCED_NODE)(n->ParentValue & ~(ULONG_PTR)3);
        if (!Root)
            Root = n;
        else if (Root != n)
            ok(0, "%wZ: its index node leads to another root\n", &e->BaseDllName);
    }
    lastBase = 0; order = 1;
    count = WalkLdr(Root, &lastBase, &order, 0);
    ok(count == modules && order, "base address index: %d of %d modules, ordered %d\n", count, modules, order);
}
