/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the NTFS API tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_MissingSecurity(void);
extern void func_IndexMetadata(void);
extern void func_FullBaseGrowth(void);
extern void func_FreeClusters(void);
extern void func_DismountWrite(void);
extern void func_ConcurrentIo(void);
extern void func_CreateRace(void);
extern void func_PagingWriteGap(void);
extern void func_DirectIo(void);
extern void func_ResidentWrite(void);
extern void func_UnalignedWriteThrough(void);
extern void func_CreateSecurity(void);
extern void func_DeleteAfterChange(void);
extern void func_AppendWrite(void);
extern void func_ColdDirectory(void);

const struct test winetest_testlist[] =
{
    { "MissingSecurity", func_MissingSecurity },
    { "IndexMetadata", func_IndexMetadata },
    { "FullBaseGrowth", func_FullBaseGrowth },
    { "FreeClusters", func_FreeClusters },
    { "DismountWrite", func_DismountWrite },
    { "ConcurrentIo", func_ConcurrentIo },
    { "CreateRace", func_CreateRace },
    { "PagingWriteGap", func_PagingWriteGap },
    { "DirectIo", func_DirectIo },
    { "ResidentWrite", func_ResidentWrite },
    { "UnalignedWriteThrough", func_UnalignedWriteThrough },
    { "CreateSecurity", func_CreateSecurity },
    { "DeleteAfterChange", func_DeleteAfterChange },
    { "AppendWrite", func_AppendWrite },
    { "ColdDirectory", func_ColdDirectory },
    { 0, 0 }
};
