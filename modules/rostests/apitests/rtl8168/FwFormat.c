/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin Realtek rtl_nic firmware header checksum and opcode bounds
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>
#include <stdio.h>

#define RTL8168_FW_MIN_SIZE 0x2D
#define RTL8168_FW_START_OFFSET 0x24
#define RTL8168_FW_LEN_OFFSET 0x28
#define RL_FW_PHY_DELAY_MS 0xe

static BOOL
Rtl8168FwValidate(
    _In_reads_bytes_(Length) const BYTE *Buffer,
    _In_ DWORD Length,
    _Out_opt_ DWORD *OpCountOut)
{
    DWORD Start;
    DWORD OpCount;
    DWORD i;
    BYTE Sum = 0;

    if (OpCountOut)
        *OpCountOut = 0;

    if (Length < RTL8168_FW_MIN_SIZE)
        return FALSE;

    for (i = 0; i < Length; i++)
        Sum = (BYTE)(Sum + Buffer[i]);
    if (Sum != 0)
        return FALSE;

    CopyMemory(&Start, Buffer + RTL8168_FW_START_OFFSET, sizeof(Start));
    CopyMemory(&OpCount, Buffer + RTL8168_FW_LEN_OFFSET, sizeof(OpCount));
    if ((Start & 3) != 0 ||
        Start < RTL8168_FW_MIN_SIZE ||
        OpCount == 0 ||
        Start > Length ||
        OpCount > (Length - Start) / sizeof(DWORD))
    {
        return FALSE;
    }

    if (OpCountOut)
        *OpCountOut = OpCount;
    return TRUE;
}

static void
test_synthetic_firmware(void)
{
    BYTE Buffer[0x90];
    DWORD OpCount = 0;
    DWORD Start = 0x80;
    DWORD Len = 1;
    DWORD Opcode = (RL_FW_PHY_DELAY_MS << 28) | 1;
    DWORD i;

    ZeroMemory(Buffer, sizeof(Buffer));
    CopyMemory(Buffer + 4, "rtl8168h-2_test", 15);
    CopyMemory(Buffer + RTL8168_FW_START_OFFSET, &Start, sizeof(Start));
    CopyMemory(Buffer + RTL8168_FW_LEN_OFFSET, &Len, sizeof(Len));
    CopyMemory(Buffer + Start, &Opcode, sizeof(Opcode));

    for (i = 0; i < sizeof(Buffer) - 1; i++)
        Buffer[sizeof(Buffer) - 1] = (BYTE)(Buffer[sizeof(Buffer) - 1] - Buffer[i]);

    ok(Rtl8168FwValidate(Buffer, sizeof(Buffer), &OpCount) == TRUE,
       "synthetic firmware should validate\n");
    ok(OpCount == 1, "got %lu ops\n", OpCount);

    Buffer[sizeof(Buffer) - 1]++;
    ok(Rtl8168FwValidate(Buffer, sizeof(Buffer), &OpCount) == FALSE,
       "bad checksum must fail\n");
}

static void
test_installed_firmware(void)
{
    static const WCHAR *Names[] =
    {
        L"rtl8168h-2.fw",
        L"rtl8168h-1.fw",
        L"rtl8168g-2.fw",
        L"rtl8168g-3.fw",
        L"rtl8168fp-3.fw",
        L"rtl8411-2.fw",
    };
    WCHAR Path[MAX_PATH];
    DWORD Found = 0;
    DWORD i;

    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        HANDLE File;
        DWORD Size;
        DWORD Read;
        BYTE *Buffer;
        DWORD OpCount = 0;

        if (!GetSystemDirectoryW(Path, ARRAYSIZE(Path)))
            continue;
        if (wcslen(Path) + 16 + wcslen(Names[i]) + 1 >= ARRAYSIZE(Path))
            continue;
        wcscat(Path, L"\\drivers\\realtek\\");
        wcscat(Path, Names[i]);

        File = CreateFileW(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
        if (File == INVALID_HANDLE_VALUE)
            continue;

        Size = GetFileSize(File, NULL);
        ok(Size != INVALID_FILE_SIZE && Size >= RTL8168_FW_MIN_SIZE,
           "%ls size %lu\n", Names[i], Size);
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        ok(Buffer != NULL, "alloc\n");
        if (!Buffer)
        {
            CloseHandle(File);
            continue;
        }

        ok(ReadFile(File, Buffer, Size, &Read, NULL) && Read == Size,
           "%ls read\n", Names[i]);
        CloseHandle(File);

        ok(Rtl8168FwValidate(Buffer, Size, &OpCount) == TRUE,
           "%ls failed validation\n", Names[i]);
        ok(OpCount > 0, "%ls opcount\n", Names[i]);
        if (wcscmp(Names[i], L"rtl8168h-2.fw") == 0)
            ok(OpCount == 211, "rtl8168h-2.fw ops %lu\n", OpCount);

        HeapFree(GetProcessHeap(), 0, Buffer);
        Found++;
    }

    if (!Found)
        skip("no rtl8168 firmware installed under System32\\drivers\\realtek\n");
    else
        ok(Found >= 1, "found %lu firmware file(s)\n", Found);
}

START_TEST(FwFormat)
{
    test_synthetic_firmware();
    test_installed_firmware();
}
