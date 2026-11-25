/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KMTQAITYPE_QUERYREGISTRY statuses, sizes and the overflow/retry protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include <winreg.h>

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);

#define QREG_SENTINEL_STATUS 0xEE
#define QREG_SENTINEL_SIZE 0xEEEE
#define QREG_HEADER_SIZE FIELD_OFFSET(D3DDDI_QUERYREGISTRY_INFO, OutputDword)

static D3DKMT_HANDLE QregAdapter;
static BYTE QregBuffer[4096];

static PFN_KMT1
QregWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static NTSTATUS
QregQuery(
    _In_ D3DDDI_QUERYREGISTRY_TYPE Type,
    _In_ UINT Flags,
    _In_opt_ PCWSTR Name,
    _In_ ULONG ValueType,
    _In_ UINT PhysicalAdapterIndex,
    _In_ UINT Size,
    _Out_ D3DDDI_QUERYREGISTRY_INFO **Result)
{
    D3DDDI_QUERYREGISTRY_INFO *Info = (D3DDDI_QUERYREGISTRY_INFO *)QregBuffer;
    D3DKMT_QUERYADAPTERINFO Query;

    memset(QregBuffer, 0xCC, sizeof(QregBuffer));
    memset(Info, 0, sizeof(*Info));
    Info->QueryType = Type;
    Info->QueryFlags.Value = Flags;
    Info->ValueType = ValueType;
    Info->PhysicalAdapterIndex = PhysicalAdapterIndex;
    if (Name != NULL)
        lstrcpynW(Info->ValueName, Name, MAX_PATH);
    Info->OutputValueSize = QREG_SENTINEL_SIZE;
    Info->Status = QREG_SENTINEL_STATUS;
    ZeroMemory(&Query, sizeof(Query));
    Query.hAdapter = QregAdapter;
    Query.Type = KMTQAITYPE_QUERYREGISTRY;
    Query.pPrivateDriverData = Info;
    Query.PrivateDriverDataSize = Size;
    *Result = Info;
    return QregWin32u("NtGdiDdDDIQueryAdapterInfo")(&Query);
}

static VOID
QregRefused(
    _In_ PCSTR What,
    _In_ D3DDDI_QUERYREGISTRY_TYPE Type,
    _In_ UINT Flags,
    _In_opt_ PCWSTR Name,
    _In_ ULONG ValueType,
    _In_ UINT PhysicalAdapterIndex,
    _In_ UINT Size,
    _In_ NTSTATUS Expected)
{
    D3DDDI_QUERYREGISTRY_INFO *Info;
    NTSTATUS Status;

    Status = QregQuery(Type, Flags, Name, ValueType, PhysicalAdapterIndex, Size, &Info);
    ok(Status == Expected, "%s: status 0x%08lx, expected 0x%08lx\n", What, Status, Expected);
    ok(Info->Status == QREG_SENTINEL_STATUS && Info->OutputValueSize == QREG_SENTINEL_SIZE,
       "%s: descriptor written (status %u, size %lu)\n", What, Info->Status, Info->OutputValueSize);
}

/* Asks with the bare descriptor, checks the overflow answer, then retries with the size it reported. */
static ULONG
QregString(
    _In_ PCSTR What,
    _In_ D3DDDI_QUERYREGISTRY_TYPE Type,
    _In_ UINT Flags,
    _In_opt_ PCWSTR Name,
    _In_ ULONG ValueType,
    _In_ ULONG Pad,
    _In_ BOOLEAN CountsNul,
    _Out_writes_(Chars) PWSTR Copy,
    _In_ ULONG Chars)
{
    D3DDDI_QUERYREGISTRY_INFO *Info;
    ULONG Needed;
    ULONG Bytes;
    NTSTATUS Status;

    Copy[0] = UNICODE_NULL;
    Status = QregQuery(Type, Flags, Name, ValueType, 0, sizeof(*Info), &Info);
    ok(Status == STATUS_SUCCESS, "%s: first status 0x%08lx\n", What, Status);
    ok(Info->Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW, "%s: first query status %u\n", What, Info->Status);
    if (Status != STATUS_SUCCESS || Info->Status != D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW)
        return 0;
    Needed = Info->OutputValueSize;
    ok(Needed > Pad && (Needed % sizeof(WCHAR)) == 0 && QREG_HEADER_SIZE + Needed <= sizeof(QregBuffer),
       "%s: overflow size %lu\n", What, Needed);
    if (Needed <= Pad || QREG_HEADER_SIZE + Needed > sizeof(QregBuffer))
        return 0;

    Status = QregQuery(Type, Flags, Name, ValueType, 0, QREG_HEADER_SIZE + Needed, &Info);
    ok(Status == STATUS_SUCCESS, "%s: retry status 0x%08lx\n", What, Status);
    ok(Info->Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS, "%s: retry query status %u\n", What, Info->Status);
    if (Status != STATUS_SUCCESS || Info->Status != D3DDDI_QUERYREGISTRY_STATUS_SUCCESS)
        return 0;
    Bytes = Info->OutputValueSize;
    ok(Bytes >= sizeof(WCHAR) && Bytes <= Needed - Pad && (Bytes % sizeof(WCHAR)) == 0, "%s: retry size %lu for %lu\n", What, Bytes, Needed);
    if (Bytes < sizeof(WCHAR) || Bytes > Needed || Bytes / sizeof(WCHAR) >= Chars)
        return 0;
    if (CountsNul)
    {
        ok(Bytes + Pad == Needed, "%s: retry size %lu, overflow size %lu\n", What, Bytes, Needed);
        ok(Info->OutputString[Bytes / sizeof(WCHAR) - 1] == UNICODE_NULL, "%s: value not terminated\n", What);
        ok(wcslen(Info->OutputString) == Bytes / sizeof(WCHAR) - 1, "%s: length %Iu for %lu bytes\n", What, wcslen(Info->OutputString), Bytes);
    }
    else
    {
        ok(Info->OutputString[Bytes / sizeof(WCHAR)] == UNICODE_NULL, "%s: path not terminated\n", What);
        ok(wcslen(Info->OutputString) == Bytes / sizeof(WCHAR), "%s: length %Iu for %lu bytes\n", What, wcslen(Info->OutputString), Bytes);
    }
    memcpy(Copy, Info->OutputString, Bytes);
    Copy[Bytes / sizeof(WCHAR)] = UNICODE_NULL;

    if (Needed > Pad + sizeof(WCHAR))
    {
        Status = QregQuery(Type, Flags, Name, ValueType, 0, QREG_HEADER_SIZE + Needed - Pad - 2 * sizeof(WCHAR), &Info);
        ok(Status == STATUS_SUCCESS && Info->Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW,
           "%s: short retry status 0x%08lx query status %u\n", What, Status, Info->Status);
    }
    return Bytes;
}

static VOID
QregServiceKey(VOID)
{
    D3DDDI_QUERYREGISTRY_INFO *Info;
    WCHAR Image[MAX_PATH * 2];
    WCHAR Translated[MAX_PATH * 2];
    ULONG Bytes, TranslatedBytes;
    NTSTATUS Status;

    Status = QregQuery(D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_DWORD, 0, sizeof(*Info), &Info);
    ok(Status == STATUS_SUCCESS, "Start: status 0x%08lx\n", Status);
    ok(Info->Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS, "Start: query status %u\n", Info->Status);
    ok(Info->OutputValueSize == sizeof(DWORD), "Start: size %lu\n", Info->OutputValueSize);
    Status = QregQuery(D3DDDI_QUERYREGISTRY_SERVICEKEY, 2, L"Start", REG_DWORD, 0, sizeof(*Info), &Info);
    ok(Status == STATUS_SUCCESS && Info->Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS && Info->OutputValueSize == sizeof(DWORD),
       "Start mutable: status 0x%08lx query status %u size %lu\n", Status, Info->Status, Info->OutputValueSize);
    Status = QregQuery(D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_DWORD, 0, QREG_HEADER_SIZE + 4, &Info);
    ok(Status == STATUS_INVALID_PARAMETER, "Start in a buffer below the descriptor: status 0x%08lx\n", Status);

    QregRefused("Start as REG_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("Start as REG_QWORD", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_QWORD, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("Start as REG_MULTI_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_MULTI_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("Group as REG_MULTI_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Group", REG_MULTI_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("Group as REG_EXPAND_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Group", REG_EXPAND_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("ImagePath as REG_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"ImagePath", REG_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("missing value", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"QregNoSuchValue", REG_SZ, 0, sizeof(*Info), STATUS_OBJECT_NAME_NOT_FOUND);
    QregRefused("missing multi-string value with TranslatePath", D3DDDI_QUERYREGISTRY_SERVICEKEY, 1, L"QregNoSuchValue", REG_MULTI_SZ, 0, sizeof(*Info), STATUS_OBJECT_NAME_NOT_FOUND);
    QregRefused("empty value name", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"", REG_DWORD, 0, sizeof(*Info), STATUS_OBJECT_NAME_NOT_FOUND);
    QregRefused("physical adapter 1", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Type", REG_DWORD, 1, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("value type 99", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Type", 99, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("reserved flag", D3DDDI_QUERYREGISTRY_SERVICEKEY, 4, L"Type", REG_DWORD, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("descriptor one byte short", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Type", REG_DWORD, 0, sizeof(*Info) - 1, STATUS_INVALID_PARAMETER);
    QregRefused("query type 4", (D3DDDI_QUERYREGISTRY_TYPE)4, 0, L"Type", REG_DWORD, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);

    Bytes = QregString("ImagePath", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"ImagePath", REG_EXPAND_SZ, 0, TRUE, Image, RTL_NUMBER_OF(Image));
    TranslatedBytes = QregString("ImagePath with TranslatePath", D3DDDI_QUERYREGISTRY_SERVICEKEY, 1, L"ImagePath", REG_EXPAND_SZ, 16, TRUE,
                                 Translated, RTL_NUMBER_OF(Translated));
    ok(Bytes != 0 && TranslatedBytes == Bytes && !wcscmp(Image, Translated), "TranslatePath changed '%ls' to '%ls'\n", Image, Translated);
}

static VOID
QregPaths(VOID)
{
    D3DDDI_QUERYREGISTRY_INFO *Info;
    WCHAR Image[MAX_PATH * 2];
    WCHAR Store[MAX_PATH * 2];
    WCHAR Named[MAX_PATH * 2];
    ULONG ImageBytes, StoreBytes, NamedBytes;
    NTSTATUS Status;

    ImageBytes = QregString("driver image path", D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 0, NULL, 0, 16, FALSE, Image, RTL_NUMBER_OF(Image));
    ok(ImageBytes > 4 * sizeof(WCHAR) && !_wcsicmp(Image + ImageBytes / sizeof(WCHAR) - 4, L".sys"), "driver image path '%ls'\n", Image);

    Status = QregQuery(D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0, NULL, 0, 0, sizeof(*Info), &Info);
    ok(Status == STATUS_SUCCESS && Info->Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW,
       "driver store path: status 0x%08lx query status %u\n", Status, Info->Status);
    ok(ImageBytes == 0 || Info->OutputValueSize == ImageBytes + sizeof(WCHAR) + 16,
       "driver store path overflow size %lu for an image of %lu bytes\n", Info->OutputValueSize, ImageBytes);

    StoreBytes = QregString("driver store path", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0, NULL, 0, 16, FALSE, Store, RTL_NUMBER_OF(Store));
    ok(StoreBytes != 0 && StoreBytes < ImageBytes && !_wcsnicmp(Store, Image, StoreBytes / sizeof(WCHAR)) &&
       Image[StoreBytes / sizeof(WCHAR)] == L'\\', "driver store path '%ls' does not hold the image '%ls'\n", Store, Image);
    NamedBytes = QregString("driver store path with a value name", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0, L"ImagePath", 0, 16, FALSE,
                            Named, RTL_NUMBER_OF(Named));
    ok(NamedBytes == StoreBytes && !wcscmp(Named, Store), "value name changed the store path to '%ls'\n", Named);

    QregRefused("driver store path as REG_SZ", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0, NULL, REG_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("driver store path with TranslatePath", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 1, NULL, 0, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("driver image path as REG_SZ", D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 0, NULL, REG_SZ, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
    QregRefused("driver image path with TranslatePath", D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 1, NULL, 0, 0, sizeof(*Info), STATUS_INVALID_PARAMETER);
}

START_TEST(queryregistry)
{
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DDDI_QUERYREGISTRY_INFO *Info;
    UINT Index;

    LoadLibraryA("gdi32.dll");
    ok(sizeof(D3DDDI_QUERYREGISTRY_INFO) == 552, "descriptor size %Iu\n", sizeof(D3DDDI_QUERYREGISTRY_INFO));
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(QregWin32u("NtGdiDdDDIEnumAdapters2")(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no adapter\n");
        return;
    }
    for (Index = 0; Index < Enum.NumAdapters; ++Index)
    {
        QregAdapter = Adapters[Index].hAdapter;
        trace("adapter %u\n", Index);
        QregServiceKey();
        QregPaths();
        QregRefused("missing adapter value", D3DDDI_QUERYREGISTRY_ADAPTERKEY, 1, L"QregNoSuchValue", REG_MULTI_SZ, 0, sizeof(*Info),
                    STATUS_OBJECT_NAME_NOT_FOUND);
    }
}
