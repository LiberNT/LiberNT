/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for System Firmware functions
 * COPYRIGHT:   Copyright 2018 Stanislav Motylkov
 *              Copyright 2018 Mark Jansen
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <pseh/pseh2.h>

static UINT (WINAPI * pEnumSystemFirmwareTables)(DWORD, PVOID, DWORD);
static UINT (WINAPI * pGetSystemFirmwareTable)(DWORD, DWORD, PVOID, DWORD);

typedef struct ENTRY
{
    DWORD Signature;
    DWORD ErrInsuff;
    DWORD ErrSuccess;
} ENTRY;

static UINT
CallNt(IN DWORD FirmwareTableProviderSignature,
       IN DWORD FirmwareTableID,
       OUT PVOID pFirmwareTableBuffer,
       IN DWORD BufferSize,
       IN SYSTEM_FIRMWARE_TABLE_ACTION Action)
{
    SYSTEM_FIRMWARE_TABLE_INFORMATION* SysFirmwareInfo;
    ULONG Result = 0, ReturnedSize;
    ULONG TotalSize = BufferSize + sizeof(SYSTEM_FIRMWARE_TABLE_INFORMATION);
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    SysFirmwareInfo = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, TotalSize);
    if (!SysFirmwareInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    _SEH2_TRY
    {
        SysFirmwareInfo->ProviderSignature = FirmwareTableProviderSignature;
        SysFirmwareInfo->TableID = FirmwareTableID;
        SysFirmwareInfo->Action = Action;
        SysFirmwareInfo->TableBufferLength = BufferSize;

        Status = NtQuerySystemInformation(SystemFirmwareTableInformation, SysFirmwareInfo, TotalSize, &ReturnedSize);

        if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
            Result = SysFirmwareInfo->TableBufferLength;

        if (NT_SUCCESS(Status) && pFirmwareTableBuffer)
        {
            memcpy(pFirmwareTableBuffer, SysFirmwareInfo->TableBuffer, SysFirmwareInfo->TableBufferLength);
        }
    }
    _SEH2_FINALLY
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, SysFirmwareInfo);
    }
    _SEH2_END;

    SetLastError(RtlNtStatusToDosError(Status));
    return Result;
}

UINT
WINAPI
fEnumSystemFirmwareTables(IN DWORD FirmwareTableProviderSignature,
                          OUT PVOID pFirmwareTableBuffer,
                          IN DWORD BufferSize)
{
    return CallNt(FirmwareTableProviderSignature, 0, pFirmwareTableBuffer, BufferSize, SystemFirmwareTable_Enumerate);
}

UINT
WINAPI
fGetSystemFirmwareTable(IN DWORD FirmwareTableProviderSignature,
                        IN DWORD FirmwareTableID,
                        OUT PVOID pFirmwareTableBuffer,
                        IN DWORD BufferSize)
{
    return CallNt(FirmwareTableProviderSignature, FirmwareTableID, pFirmwareTableBuffer, BufferSize, SystemFirmwareTable_Get);
}

static
VOID
test_EnumBuffer(
    DWORD Signature,
    PVOID Buffer,
    DWORD dwSize,
    UINT * pTableCount,
    DWORD * pFirstTableID,
    DWORD ErrInsuff,
    DWORD ErrSuccess
)
{
    DWORD dwError;
    DWORD dwBufferSize;
    DWORD dwException;
    UINT uResultSize;

    dwException = Buffer && IsBadWritePtr(Buffer, dwSize) ? STATUS_ACCESS_VIOLATION : STATUS_SUCCESS;

    // Test size = 0
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    SetLastError(0xbeeffeed);
    dwError = GetLastError();
    dwBufferSize = 0;
    uResultSize = 0;
    StartSeh()
        uResultSize = pEnumSystemFirmwareTables(Signature, Buffer, dwBufferSize);
        dwError = GetLastError();
    EndSeh(STATUS_SUCCESS);

    if (uResultSize > 0)
    {
        ok(dwError == ErrInsuff,
           "GetLastError() returned %ld, expected %ld\n",
           dwError, ErrInsuff);
    }
    else
    {
        ok(dwError == ErrSuccess || dwError == ERROR_INVALID_FUNCTION,
           "GetLastError() returned %ld, expected %ld\n",
           dwError, ErrSuccess);
    }
    if (ErrSuccess == ERROR_SUCCESS)
    {
        ok(uResultSize % sizeof(DWORD) == 0,
           "uResultSize is %u, expected %% sizeof(DWORD)\n",
           uResultSize);
    }
    else
    {
        ok(uResultSize == 0,
           "uResultSize is %u, expected == 0\n",
           uResultSize);
    }
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        ok(*(BYTE *)Buffer == 0xFF,
           "Buffer should be clean at offset 0, got %x\n",
           *(BYTE *)Buffer);
    }

    // Test size = 2
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    SetLastError(0xbeeffeed);
    dwError = GetLastError();
    dwBufferSize = 2;
    uResultSize = 0;
    StartSeh()
        uResultSize = pEnumSystemFirmwareTables(Signature, Buffer, dwBufferSize);
        dwError = GetLastError();
    EndSeh(STATUS_SUCCESS);

    if (uResultSize > 0)
    {
        ok(dwError == ErrInsuff,
           "GetLastError() returned %ld, expected %ld\n",
           dwError, ErrInsuff);
    }
    else
    {
        ok(dwError == ErrSuccess || dwError == ERROR_INVALID_FUNCTION,
           "GetLastError() returned %ld, expected %ld\n",
           dwError, ErrSuccess);
    }
    if (ErrSuccess == ERROR_SUCCESS)
    {
        ok(uResultSize % sizeof(DWORD) == 0,
           "uResultSize is %u, expected %% sizeof(DWORD)\n",
           uResultSize);
    }
    else
    {
        ok(uResultSize == 0,
           "uResultSize is %u, expected == 0\n",
           uResultSize);
    }
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        ok(*(WORD *)Buffer == 0xFFFF,
           "Buffer should be clean at offset 0, got %x\n",
           *(WORD *)Buffer);
    }

    // Test full size
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    if (uResultSize > 0)
    {
        SetLastError(0xbeeffeed);
        dwError = GetLastError();
        dwBufferSize = uResultSize;
        uResultSize = 0;
        StartSeh()
            uResultSize = pEnumSystemFirmwareTables(Signature, Buffer, dwBufferSize);
            dwError = GetLastError();
        EndSeh(dwError == ERROR_SUCCESS ? dwException : STATUS_SUCCESS);
        // Windows Vista+: does not throw exception here

        if (dwException == STATUS_SUCCESS || ErrSuccess == ERROR_INVALID_FUNCTION)
        {
            ok(dwError == ErrSuccess,
               "GetLastError() returned %ld, expected %ld\n",
               dwError, ErrSuccess);
            if (ErrSuccess == ERROR_SUCCESS)
            {
                ok(uResultSize == dwBufferSize,
                   "uResultSize is not equal dwBufferSize, expected %ld\n",
                   dwBufferSize);
            }
            else
            {
                ok(uResultSize == 0,
                   "uResultSize is %u, expected == 0\n",
                   uResultSize);
            }
        }
        else
        {
            // Windows Vista+: returns ERROR_NOACCESS here
            ok(dwError == 0xbeeffeed || dwError == ERROR_NOACCESS,
               "GetLastError() returned %ld, expected %u\n",
               dwError, 0xbeeffeed);
            // Windows Vista+: returns correct size here
            ok(uResultSize == 0 || uResultSize == dwBufferSize,
               "uResultSize is %u, expected == 0\n",
               uResultSize);
        }
    }

    if (pTableCount && pFirstTableID)
    {
        if (uResultSize > 0)
        {
            if (Signature == 'RSMB')
            {
                // Raw SMBIOS have only one table with ID 0
                ok(*(DWORD *)Buffer == 0,
                   "Buffer should be filled at offset 0, got %lx\n",
                   *(DWORD *)Buffer);
            }
            else
            {
                // In other cases ID can be different
                if (ErrSuccess == ERROR_SUCCESS)
                {
                    ok(*(DWORD *)Buffer != 0xFFFFFFFF,
                       "Buffer should be filled at offset 0\n");
                }
                else
                {
                    ok(*(DWORD *)Buffer == 0xFFFFFFFF,
                       "Buffer should be clean at offset 0\n");
                }
            }
        }
        *pTableCount = uResultSize / sizeof(DWORD);
        *pFirstTableID = *(DWORD *)Buffer;
    }
}

static
VOID
test_GetBuffer(
    DWORD Signature,
    DWORD TableID,
    PVOID Buffer,
    DWORD dwSize,
    BOOL TestFakeID,
    DWORD ErrInsuff,
    DWORD ErrSuccess
)
{
    DWORD dwError;
    DWORD dwBufferSize;
    DWORD dwException;
    DWORD dwErrCase;
    UINT uResultSize;

    dwException = Buffer && IsBadWritePtr(Buffer, dwSize) ? STATUS_ACCESS_VIOLATION : STATUS_SUCCESS;
    switch (Signature)
    {
        case 'ACPI':
        {
            dwErrCase = ERROR_NOT_FOUND;
            break;
        }
        case 'FIRM':
        {
            dwErrCase = (GetNTVersion() >= 0xA00) ? ERROR_INVALID_FUNCTION : ERROR_INVALID_PARAMETER;
            break;
        }
        default:
        {
            dwErrCase = ErrInsuff;
        }
    }

    // Test size = 0
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    SetLastError(0xbeeffeed);
    dwError = GetLastError();
    dwBufferSize = 0;
    uResultSize = 0;
    StartSeh()
        uResultSize = pGetSystemFirmwareTable(Signature, TableID, Buffer, dwBufferSize);
        dwError = GetLastError();
    EndSeh(STATUS_SUCCESS);

    ok(dwError == (TestFakeID ? dwErrCase : ErrInsuff) || dwError == ERROR_INVALID_FUNCTION,
       "GetLastError() returned %ld, expected %ld\n",
       dwError, (TestFakeID ? dwErrCase : ErrInsuff));
    if (ErrSuccess == ERROR_SUCCESS && (!TestFakeID || dwErrCase == ErrInsuff))
    {
        ok(uResultSize > 0 || uResultSize == 0, // FIXME: This is broken
           "uResultSize is %u, expected > 0\n",
           uResultSize);
    }
    else
    {
        ok(uResultSize == 0,
           "uResultSize is %u, expected == 0\n",
           uResultSize);
    }
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        ok(*(BYTE *)Buffer == 0xFF,
           "Buffer should be clean at offset 0, got %x\n",
           *(BYTE *)Buffer);
    }

    // Test size = 2
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    SetLastError(0xbeeffeed);
    dwError = GetLastError();
    dwBufferSize = 2;
    uResultSize = 0;
    StartSeh()
        uResultSize = pGetSystemFirmwareTable(Signature, TableID, Buffer, dwBufferSize);
        dwError = GetLastError();
    EndSeh(STATUS_SUCCESS);

    ok(dwError == (TestFakeID ? dwErrCase : ErrInsuff) || dwError == ERROR_INVALID_FUNCTION,
       "GetLastError() returned %ld, expected %ld\n",
       dwError, (TestFakeID ? dwErrCase : ErrInsuff));
    if (ErrSuccess == ERROR_SUCCESS && (!TestFakeID || dwErrCase == ErrInsuff))
    {
        ok(uResultSize > 0 || uResultSize == 0, // FIXME: This is broken
           "uResultSize is %u, expected > 0\n",
           uResultSize);
    }
    else
    {
        ok(uResultSize == 0,
           "uResultSize is %u, expected == 0\n",
           uResultSize);
    }
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        ok(*(WORD *)Buffer == 0xFFFF,
           "Buffer should be clean at offset 0, got %x\n",
           *(WORD *)Buffer);
    }

    // Test full size
    if (Buffer && dwException == STATUS_SUCCESS)
    {
        FillMemory(Buffer, dwSize, 0xFF);
    }
    if (uResultSize == 0)
    {
        return;
    }
    SetLastError(0xbeeffeed);
    dwError = GetLastError();
    dwBufferSize = uResultSize;
    uResultSize = 0;
    StartSeh()
        uResultSize = pGetSystemFirmwareTable(Signature, TableID, Buffer, dwBufferSize);
        dwError = GetLastError();
    EndSeh(/*ErrSuccess == ERROR_SUCCESS ? dwException : STATUS_SUCCESS*/ ExceptionStatus);
    // Windows Vista+: does not throw exception here

    if (dwException == STATUS_SUCCESS || ErrSuccess == ERROR_INVALID_FUNCTION)
    {
        ok(dwError == ErrSuccess,
           "GetLastError() returned %ld, expected %ld\n",
           dwError, ErrSuccess);
        if (ErrSuccess == ERROR_SUCCESS)
        {
            ok(uResultSize == dwBufferSize,
               "uResultSize is not equal dwBufferSize, expected %ld\n",
               dwBufferSize);
        }
        else
        {
            ok(uResultSize == 0,
               "uResultSize is %u, expected == 0\n",
               uResultSize);
        }
    }
    else
    {
        // Windows Vista+: returns ERROR_NOACCESS here
        ok(dwError == 0xbeeffeed || dwError == ERROR_NOACCESS,
           "GetLastError() returned %ld, expected %u\n",
           dwError, 0xbeeffeed);
        // Windows Vista+: returns correct size here
        ok(uResultSize == 0 || uResultSize == dwBufferSize,
           "uResultSize is %u, expected == 0\n",
           uResultSize);
    }

    if (Buffer && dwException == STATUS_SUCCESS)
    {
        if (ErrSuccess == ERROR_SUCCESS)
        {
            ok(*(DWORD *)Buffer != 0xFFFFFFFF,
               "Buffer should be filled at offset 0\n");
        }
        else
        {
            ok(*(DWORD *)Buffer == 0xFFFFFFFF,
               "Buffer should be clean at offset 0\n");
        }
    }
}

static
VOID
test_Functions()
{
    static const ENTRY Entries[] =
    {
        { 'ACPI', ERROR_INSUFFICIENT_BUFFER, ERROR_SUCCESS },
        { 'FIRM', ERROR_INSUFFICIENT_BUFFER, ERROR_SUCCESS },
        { 'RSMB', ERROR_INSUFFICIENT_BUFFER, ERROR_SUCCESS },
        /* This entry should be last */
        { 0xDEAD, ERROR_INVALID_FUNCTION, ERROR_INVALID_FUNCTION },
    };
    CHAR Buffer[262144]; // 256 KiB should be enough
    CHAR Sign[sizeof(DWORD) + 1];
    UINT TableCount[_countof(Entries)];
    DWORD FirstTableID[_countof(Entries)];
    int i;

    // Test EnumSystemFirmwareTables
    for (i = 0; i < _countof(Entries); i++)
    {
        // Test with NULL buffer
        test_EnumBuffer(Entries[i].Signature, NULL, sizeof(Buffer), NULL, NULL,
                        Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with wrong buffer
        test_EnumBuffer(Entries[i].Signature, (PVOID *)(LONG_PTR)0xbeeffeed, sizeof(Buffer), NULL, NULL,
                        Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with correct buffer
        test_EnumBuffer(Entries[i].Signature, &Buffer, sizeof(Buffer), &TableCount[i], &FirstTableID[i],
                        Entries[i].ErrInsuff, Entries[i].ErrSuccess);
    }

    // Test GetSystemFirmwareTable
    for (i = 0; i < _countof(Entries); i++)
    {
        // Test with fake ID and NULL buffer
        test_GetBuffer(Entries[i].Signature, 0xbeeffeed, NULL, sizeof(Buffer),
                       TRUE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with fake ID and wrong buffer
        test_GetBuffer(Entries[i].Signature, 0xbeeffeed, (PVOID *)(LONG_PTR)0xbeeffeed, sizeof(Buffer),
                       TRUE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with fake ID and correct buffer
        test_GetBuffer(Entries[i].Signature, 0xbeeffeed, &Buffer, sizeof(Buffer),
                       TRUE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        if (TableCount[i] == 0)
        {
            if (i < _countof(Entries) - 1)
            {
                ZeroMemory(&Sign, sizeof(Sign));
                *(DWORD *)&Sign = _byteswap_ulong(Entries[i].Signature);
                skip("No tables for %s found. Skipping\n",
                     Sign);
            }
            continue;
        }
        // Test with correct ID and NULL buffer
        test_GetBuffer(Entries[i].Signature, FirstTableID[i], NULL, sizeof(Buffer),
                       FALSE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with correct ID and wrong buffer
        test_GetBuffer(Entries[i].Signature, FirstTableID[i], (PVOID *)(LONG_PTR)0xbeeffeed, sizeof(Buffer),
                       FALSE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
        // Test with correct ID and correct buffer
        test_GetBuffer(Entries[i].Signature, FirstTableID[i], &Buffer, sizeof(Buffer),
                       FALSE, Entries[i].ErrInsuff, Entries[i].ErrSuccess);
    }
}

typedef struct BIOS_VALUE
{
    UCHAR Type;
    UCHAR Offset;
    BOOL String;
    PCWSTR Name;
} BIOS_VALUE;

static const BIOS_VALUE BiosValues[] =
{
    { 2, 0x04, TRUE, L"BaseBoardManufacturer" },
    { 2, 0x05, TRUE, L"BaseBoardProduct" },
    { 2, 0x06, TRUE, L"BaseBoardVersion" },
    { 0, 0x14, FALSE, L"BiosMajorRelease" },
    { 0, 0x15, FALSE, L"BiosMinorRelease" },
    { 0, 0x08, TRUE, L"BIOSReleaseDate" },
    { 0, 0x04, TRUE, L"BIOSVendor" },
    { 0, 0x05, TRUE, L"BIOSVersion" },
    { 1, 0x1A, TRUE, L"SystemFamily" },
    { 1, 0x04, TRUE, L"SystemManufacturer" },
    { 1, 0x05, TRUE, L"SystemProductName" },
    { 1, 0x19, TRUE, L"SystemSKU" },
    { 1, 0x06, TRUE, L"SystemVersion" },
};

static
PCSTR
SmbiosString(const UCHAR *Structure, const UCHAR *End, UCHAR Number)
{
    const UCHAR *String = Structure + Structure[1];
    UCHAR Index;

    if (!Number)
        return NULL;
    for (Index = 1; String < End && *String; Index++)
    {
        if (Index == Number)
            return (PCSTR)String;
        String += strlen((PCSTR)String) + 1;
    }
    return NULL;
}

static
VOID
test_BiosRegistry(VOID)
{
    static UCHAR Raw[64 * 1024];
    const UCHAR *Structure, *End, *Next;
    UINT Size, Index, Checked = 0;
    WCHAR Expected[256], Actual[256];
    DWORD Type, Length, Number;
    HKEY Key;
    LONG Error;
    PCSTR String;

    Size = pGetSystemFirmwareTable('RSMB', 0, Raw, sizeof(Raw));
    if (Size <= 8 || Size > sizeof(Raw))
    {
        skip("No raw SMBIOS table (%u)\n", Size);
        return;
    }
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_QUERY_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "HARDWARE\\DESCRIPTION\\System\\BIOS could not be opened: %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    End = Raw + Size;
    for (Structure = Raw + 8; Structure + 4 <= End && Structure[1] >= 4 && Structure + Structure[1] <= End &&
         Structure[0] != 127; Structure = Next + 2)
    {
        for (Index = 0; Index < ARRAYSIZE(BiosValues); ++Index)
        {
            if (BiosValues[Index].Type != Structure[0] || BiosValues[Index].Offset >= Structure[1])
                continue;
            if (BiosValues[Index].String)
            {
                String = SmbiosString(Structure, End, Structure[BiosValues[Index].Offset]);
                if (!String || !*String)
                    continue;
                MultiByteToWideChar(CP_ACP, 0, String, -1, Expected, ARRAYSIZE(Expected));
                Length = sizeof(Actual);
                Error = RegQueryValueExW(Key, BiosValues[Index].Name, NULL, &Type, (PBYTE)Actual, &Length);
                ok(Error == ERROR_SUCCESS && Type == REG_SZ, "%S: error %ld, type %lu\n", BiosValues[Index].Name, Error, Type);
                if (Error == ERROR_SUCCESS && Type == REG_SZ)
                    ok(!wcscmp(Actual, Expected), "%S is '%S', SMBIOS has '%S'\n", BiosValues[Index].Name, Actual, Expected);
            }
            else
            {
                Length = sizeof(Number);
                Error = RegQueryValueExW(Key, BiosValues[Index].Name, NULL, &Type, (PBYTE)&Number, &Length);
                ok(Error == ERROR_SUCCESS && Type == REG_DWORD, "%S: error %ld, type %lu\n", BiosValues[Index].Name, Error, Type);
                if (Error == ERROR_SUCCESS && Type == REG_DWORD)
                    ok(Number == Structure[BiosValues[Index].Offset], "%S is %lu, SMBIOS has %u\n", BiosValues[Index].Name,
                       Number, Structure[BiosValues[Index].Offset]);
            }
            Checked++;
        }
        Next = Structure + Structure[1];
        while (Next + 1 < End && (Next[0] || Next[1]))
            ++Next;
        if (Next + 1 >= End)
            break;
    }
    ok(Checked >= 3, "Only %u BIOS values could be checked against SMBIOS\n", Checked);
    RegCloseKey(Key);
}

static
VOID
test_SystemPartition(VOID)
{
    WCHAR Partition[MAX_PATH] = { 0 }, Device[MAX_PATH + 16], Firmware[MAX_PATH] = { 0 };
    DWORD Type = REG_NONE, Length;
    HANDLE Handle;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_QUERY_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "SYSTEM\\Setup could not be opened: %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    Length = sizeof(Partition) - sizeof(WCHAR);
    Error = RegQueryValueExW(Key, L"SystemPartition", NULL, &Type, (PBYTE)Partition, &Length);
    RegCloseKey(Key);
    ok(Error == ERROR_SUCCESS && Type == REG_SZ, "SystemPartition: error %ld, type %lu\n", Error, Type);
    if (Error == ERROR_SUCCESS && Type == REG_SZ)
    {
        ok(!_wcsnicmp(Partition, L"\\Device\\", 8), "SystemPartition is '%S'\n", Partition);
        StringCchPrintfW(Device, ARRAYSIZE(Device), L"\\\\?\\GLOBALROOT%s", Partition);
        Handle = CreateFileW(Device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0,
                             NULL);
        ok(Handle != INVALID_HANDLE_VALUE, "%S could not be opened: %lu\n", Device, GetLastError());
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_QUERY_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "SYSTEM\\CurrentControlSet\\Control could not be opened: %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    Type = REG_NONE;
    Length = sizeof(Firmware) - sizeof(WCHAR);
    Error = RegQueryValueExW(Key, L"FirmwareBootDevice", NULL, &Type, (PBYTE)Firmware, &Length);
    RegCloseKey(Key);
    ok(Error == ERROR_SUCCESS && Type == REG_SZ, "FirmwareBootDevice: error %ld, type %lu\n", Error, Type);
    if (Error == ERROR_SUCCESS && Type == REG_SZ)
        ok(!_wcsnicmp(Firmware, L"multi(0)disk(0)rdisk(", 21) && wcsstr(Firmware, L")partition("),
           "FirmwareBootDevice is '%S'\n", Firmware);
}

START_TEST(SystemFirmware)
{
    HANDLE hKernel;

    hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel)
    {
        skip("kernel32.dll module not found. Can't proceed\n");
        return;
    }

    pEnumSystemFirmwareTables = (void *)fEnumSystemFirmwareTables;
    pGetSystemFirmwareTable = (void *)fGetSystemFirmwareTable;

    //test_Functions(); // FIXME: This test is broken!

    pEnumSystemFirmwareTables = (void *)GetProcAddress(hKernel, "EnumSystemFirmwareTables");
    pGetSystemFirmwareTable = (void *)GetProcAddress(hKernel, "GetSystemFirmwareTable");

    if (!pEnumSystemFirmwareTables)
    {
        skip("EnumSystemFirmwareTables not found. Can't proceed\n");
        return;
    }
    if (!pGetSystemFirmwareTable)
    {
        skip("GetSystemFirmwareTable not found. Can't proceed\n");
        return;
    }
    test_Functions();
    test_BiosRegistry();
    test_SystemPartition();
}
