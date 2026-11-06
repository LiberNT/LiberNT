/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify the process API-set namespace mapping
 * COPYRIGHT:   Copyright 2026 OpenAI <support@openai.com>
 */

#include "precomp.h"

typedef struct _TEST_API_SET_NAMESPACE
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Count;
    ULONG EntryOffset;
    ULONG HashOffset;
    ULONG HashFactor;
} TEST_API_SET_NAMESPACE;

START_TEST(ApiSetMap)
{
    const TEST_API_SET_NAMESPACE *Map = (const void *)NtCurrentPeb()->ApiSetMap;
    MEMORY_BASIC_INFORMATION Memory;
    NTSTATUS Status;

    ok(Map != NULL, "PEB API-set map is NULL\n");
    if (!Map) return;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)Map, MemoryBasicInformation,
                                  &Memory, sizeof(Memory), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok_hex(Memory.State, MEM_COMMIT);
    ok_hex(Memory.Type, MEM_MAPPED);
    ok_hex(Memory.Protect, PAGE_READONLY);
    if (Memory.State != MEM_COMMIT || !(Memory.Protect & PAGE_READONLY)) return;

    ok(Map->Version == 6, "unexpected namespace version %lu\n", Map->Version);
    ok(Map->Size >= sizeof(*Map), "namespace size %lu\n", Map->Size);
    ok(Map->Count != 0, "namespace has no entries\n");
    ok(Map->EntryOffset < Map->Size, "entry offset %lu beyond size %lu\n", Map->EntryOffset, Map->Size);
    ok(Map->HashOffset < Map->Size, "hash offset %lu beyond size %lu\n", Map->HashOffset, Map->Size);
    trace("map %p, version %lu, size %lu, entries %lu, type 0x%lx, protect 0x%lx\n",
          Map, Map->Version, Map->Size, Map->Count, Memory.Type, Memory.Protect);
}
