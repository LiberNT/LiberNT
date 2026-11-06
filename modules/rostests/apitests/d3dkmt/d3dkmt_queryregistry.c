/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KMTQAITYPE_QUERYREGISTRY registry and driver path queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <winreg.h>

#define QREG_STATUS_SENTINEL ((D3DDDI_QUERYREGISTRY_STATUS)0x5A5A5A5A)
#define QREG_SIZE_SENTINEL 0xA5A5A5A5u
#define QREG_MIN_SIZE ((UINT)sizeof(D3DDDI_QUERYREGISTRY_INFO))

typedef struct _QREG_BUFFER
{
    D3DDDI_QUERYREGISTRY_INFO Info;
    WCHAR Extra[2 * MAX_PATH];
} QREG_BUFFER;

static PFND3DKMT_QUERYADAPTERINFO pfnQueryAdapterInfo;

static NTSTATUS
QregQuery(D3DKMT_HANDLE hAdapter, QREG_BUFFER *Buffer, D3DDDI_QUERYREGISTRY_TYPE Type, UINT Flags,
          PCWSTR Name, ULONG ValueType, UINT PhysicalAdapterIndex, UINT Size)
{
    D3DKMT_QUERYADAPTERINFO Query;

    memset(Buffer, 0, sizeof(*Buffer));
    Buffer->Info.QueryType = Type;
    Buffer->Info.QueryFlags.Value = Flags;
    if (Name != NULL)
        lstrcpynW(Buffer->Info.ValueName, Name, MAX_PATH);
    Buffer->Info.ValueType = ValueType;
    Buffer->Info.PhysicalAdapterIndex = PhysicalAdapterIndex;
    Buffer->Info.Status = QREG_STATUS_SENTINEL;
    Buffer->Info.OutputValueSize = QREG_SIZE_SENTINEL;
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_QUERYREGISTRY;
    Query.pPrivateDriverData = Buffer;
    Query.PrivateDriverDataSize = Size;
    return pfnQueryAdapterInfo(&Query);
}

static BOOL
QregUntouched(const QREG_BUFFER *Buffer)
{
    return Buffer->Info.Status == QREG_STATUS_SENTINEL && Buffer->Info.OutputValueSize == QREG_SIZE_SENTINEL;
}

static void
QregRefused(D3DKMT_HANDLE hAdapter, LUID Luid, const char *What, D3DDDI_QUERYREGISTRY_TYPE Type, UINT Flags,
            PCWSTR Name, ULONG ValueType, UINT PhysicalAdapterIndex, UINT Size, NTSTATUS Expected)
{
    QREG_BUFFER Buffer;
    NTSTATUS Status = QregQuery(hAdapter, &Buffer, Type, Flags, Name, ValueType, PhysicalAdapterIndex, Size);

    ok(Status == Expected, "Adapter %08lx:%08lx: %s returned 0x%08lx, expected 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, What, (ULONG)Status, (ULONG)Expected);
    ok(QregUntouched(&Buffer), "Adapter %08lx:%08lx: %s wrote status %u size 0x%x into a refused query\n",
       (ULONG)Luid.HighPart, Luid.LowPart, What, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize);
}

static void
QregRegistryValues(D3DKMT_HANDLE hAdapter, LUID Luid)
{
    QREG_BUFFER Buffer;
    NTSTATUS Status;
    UINT InstanceBytes;
    DWORD Start;

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_DWORD, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS &&
       Buffer.Info.OutputValueSize == sizeof(DWORD),
       "Adapter %08lx:%08lx: service Start returned 0x%08lx status %u size %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize);
    Start = Buffer.Info.OutputDword;

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 2, L"Start", REG_DWORD, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS &&
       Buffer.Info.OutputDword == Start,
       "Adapter %08lx:%08lx: mutable service Start returned 0x%08lx status %u value %lu, immutable %lu\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, Buffer.Info.OutputDword, Start);

    QregRefused(hAdapter, Luid, "service Start as REG_SZ", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_SZ, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    QregRefused(hAdapter, Luid, "missing service value", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"LiberNTNoSuchValue",
                REG_DWORD, 0, sizeof(Buffer), STATUS_OBJECT_NAME_NOT_FOUND);
    QregRefused(hAdapter, Luid, "missing adapter value", D3DDDI_QUERYREGISTRY_ADAPTERKEY, 0, L"LiberNTNoSuchValue",
                REG_DWORD, 0, sizeof(Buffer), STATUS_OBJECT_NAME_NOT_FOUND);
    QregRefused(hAdapter, Luid, "service value named with a leading backslash", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0,
                L"\\Start", REG_DWORD, 0, sizeof(Buffer), STATUS_OBJECT_NAME_NOT_FOUND);

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Enum\\Count", REG_DWORD, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS && Buffer.Info.OutputDword >= 1,
       "Adapter %08lx:%08lx: service Enum\\Count returned 0x%08lx status %u value %lu\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, Buffer.Info.OutputDword);
    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"\\Enum\\Count", REG_DWORD, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS && Buffer.Info.OutputDword >= 1,
       "Adapter %08lx:%08lx: service \\Enum\\Count returned 0x%08lx status %u value %lu\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, Buffer.Info.OutputDword);

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Enum\\0", REG_SZ, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS,
       "Adapter %08lx:%08lx: service Enum\\0 returned 0x%08lx status %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status);
    if (Status != STATUS_SUCCESS || Buffer.Info.Status != D3DDDI_QUERYREGISTRY_STATUS_SUCCESS)
        return;
    InstanceBytes = Buffer.Info.OutputValueSize;
    ok(InstanceBytes == (lstrlenW(Buffer.Info.OutputString) + 1) * sizeof(WCHAR),
       "Adapter %08lx:%08lx: Enum\\0 size %u for \"%ls\"\n", (ULONG)Luid.HighPart, Luid.LowPart, InstanceBytes,
       Buffer.Info.OutputString);

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Enum\\0", REG_SZ, 0, QREG_MIN_SIZE);
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW &&
       Buffer.Info.OutputValueSize == InstanceBytes,
       "Adapter %08lx:%08lx: short Enum\\0 returned 0x%08lx status %u size %u, expected %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize, InstanceBytes);
    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_SERVICEKEY, 1, L"Enum\\0", REG_SZ, 0, QREG_MIN_SIZE);
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW &&
       Buffer.Info.OutputValueSize == InstanceBytes + 16,
       "Adapter %08lx:%08lx: short translated Enum\\0 returned 0x%08lx status %u size %u, expected %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize,
       InstanceBytes + 16);
}

static void
QregDriverPaths(D3DKMT_HANDLE hAdapter, LUID Luid)
{
    QREG_BUFFER Image, Buffer;
    NTSTATUS Status;
    UINT ImageBytes, StoreChars;
    PCWSTR Repository, Tail;

    Status = QregQuery(hAdapter, &Image, D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 0, NULL, 0, 0, sizeof(Image));
    ok(Status == STATUS_SUCCESS && Image.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS,
       "Adapter %08lx:%08lx: image path returned 0x%08lx status %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Image.Info.Status);
    if (Status != STATUS_SUCCESS || Image.Info.Status != D3DDDI_QUERYREGISTRY_STATUS_SUCCESS)
        return;
    trace("Adapter %08lx:%08lx: image path \"%ls\" size %u\n", (ULONG)Luid.HighPart, Luid.LowPart,
          Image.Info.OutputString, (UINT)Image.Info.OutputValueSize);
    ImageBytes = lstrlenW(Image.Info.OutputString) * sizeof(WCHAR);
    ok(Image.Info.OutputValueSize == ImageBytes, "Adapter %08lx:%08lx: image path size %u, string bytes %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (UINT)Image.Info.OutputValueSize, ImageBytes);
    ok(ImageBytes > 4 * sizeof(WCHAR) && _wcsnicmp(Image.Info.OutputString, L"\\SystemRoot\\", 12) == 0 &&
       lstrcmpiW(Image.Info.OutputString + ImageBytes / sizeof(WCHAR) - 4, L".sys") == 0,
       "Adapter %08lx:%08lx: image path \"%ls\" is not the loaded driver image path\n",
       (ULONG)Luid.HighPart, Luid.LowPart, Image.Info.OutputString);

    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 2, NULL, 0, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS &&
       Buffer.Info.OutputValueSize == ImageBytes,
       "Adapter %08lx:%08lx: mutable image path returned 0x%08lx status %u size %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize);
    QregRefused(hAdapter, Luid, "translated image path", D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 1, NULL, 0, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    QregRefused(hAdapter, Luid, "image path with a value type", D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 0, NULL, REG_SZ, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_DRIVERIMAGEPATH, 0, NULL, 0, 0, QREG_MIN_SIZE);
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW &&
       Buffer.Info.OutputValueSize == ImageBytes + 16,
       "Adapter %08lx:%08lx: short image path returned 0x%08lx status %u size %u, expected %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize,
       ImageBytes + 16);

    QregRefused(hAdapter, Luid, "translated store path", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 1, NULL, 0, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0, NULL, 0, 0, QREG_MIN_SIZE);
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW &&
       Buffer.Info.OutputValueSize == ImageBytes + sizeof(WCHAR) + 16,
       "Adapter %08lx:%08lx: short store path returned 0x%08lx status %u size %u, expected %u\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize,
       ImageBytes + (UINT)sizeof(WCHAR) + 16);

    Repository = wcsstr(Image.Info.OutputString, L"\\FileRepository\\");
    if (Repository == NULL)
    {
        QregRefused(hAdapter, Luid, "store path of a driver outside the store", D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 0,
                    NULL, 0, 0, sizeof(Buffer), STATUS_INVALID_PARAMETER);
        return;
    }
    Tail = wcschr(Repository + lstrlenW(L"\\FileRepository\\"), L'\\');
    StoreChars = Tail != NULL ? (UINT)(Tail - Image.Info.OutputString) : 0;
    Status = QregQuery(hAdapter, &Buffer, D3DDDI_QUERYREGISTRY_DRIVERSTOREPATH, 2, NULL, 0, 0, sizeof(Buffer));
    ok(Status == STATUS_SUCCESS && Buffer.Info.Status == D3DDDI_QUERYREGISTRY_STATUS_SUCCESS &&
       Buffer.Info.OutputValueSize == StoreChars * sizeof(WCHAR) &&
       (UINT)lstrlenW(Buffer.Info.OutputString) == StoreChars &&
       _wcsnicmp(Buffer.Info.OutputString, Image.Info.OutputString, StoreChars) == 0,
       "Adapter %08lx:%08lx: store path returned 0x%08lx status %u size %u \"%ls\", expected the %u-character package folder\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (UINT)Buffer.Info.Status, (UINT)Buffer.Info.OutputValueSize,
       Buffer.Info.OutputString, StoreChars);
}

static void
QregAdapter(D3DKMT_HANDLE hAdapter, LUID Luid)
{
    QREG_BUFFER Buffer;

    QregRefused(hAdapter, Luid, "short request", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_DWORD, 0,
                QREG_MIN_SIZE - sizeof(WCHAR), STATUS_INVALID_PARAMETER);
    QregRefused(hAdapter, Luid, "reserved flag", D3DDDI_QUERYREGISTRY_SERVICEKEY, 4, L"Start", REG_DWORD, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    QregRefused(hAdapter, Luid, "query type past the last", (D3DDDI_QUERYREGISTRY_TYPE)4, 0, L"Start", REG_DWORD, 0,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    QregRefused(hAdapter, Luid, "second physical adapter", D3DDDI_QUERYREGISTRY_SERVICEKEY, 0, L"Start", REG_DWORD, 1,
                sizeof(Buffer), STATUS_INVALID_PARAMETER);
    QregRegistryValues(hAdapter, Luid);
    QregDriverPaths(hAdapter, Luid);
}

START_TEST(queryregistry)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnumAdapters2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER pfnCloseAdapter = (PFND3DKMT_CLOSEADAPTER)LoadD3DKMTProc("D3DKMTCloseAdapter");
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    NTSTATUS Status;
    ULONG Index;

    pfnQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (pfnEnumAdapters2 == NULL || pfnCloseAdapter == NULL || pfnQueryAdapterInfo == NULL)
    {
        skip("D3DKMTEnumAdapters2, D3DKMTCloseAdapter or D3DKMTQueryAdapterInfo is not exported\n");
        return;
    }
    memset(&Enum, 0, sizeof(Enum));
    Status = pfnEnumAdapters2(&Enum);
    ok(NT_SUCCESS(Status) && Enum.NumAdapters != 0, "Adapter count query returned 0x%08lx, %lu adapters\n",
       (ULONG)Status, Enum.NumAdapters);
    if (!NT_SUCCESS(Status) || Enum.NumAdapters == 0)
        return;
    Adapters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Enum.NumAdapters * sizeof(*Adapters));
    if (Adapters == NULL)
        return;
    Enum.pAdapters = Adapters;
    Status = pfnEnumAdapters2(&Enum);
    ok(NT_SUCCESS(Status), "Adapter enumeration returned 0x%08lx\n", (ULONG)Status);
    for (Index = 0; NT_SUCCESS(Status) && Index < Enum.NumAdapters; ++Index)
    {
        QregAdapter(Adapters[Index].hAdapter, Adapters[Index].AdapterLuid);
        memset(&Close, 0, sizeof(Close));
        Close.hAdapter = Adapters[Index].hAdapter;
        pfnCloseAdapter(&Close);
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
}
