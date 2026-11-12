/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A live kernel dump requested through NtSystemDebugControl reaches the caller's file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

START_TEST(KdLiveDump)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    SYSDBG_LIVEDUMP_CONTROL Control;
    LARGE_INTEGER Size;
    CHAR Signature[8];
    DWORD Read;
    BOOLEAN Old;
    NTSTATUS Status;
    HANDLE File;

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0, "GetTempPathW failed\n");
    ok(GetTempFileNameW(Directory, L"kld", 0, Path) != 0, "GetTempFileNameW failed\n");
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "create failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;

    Status = NtSystemDebugControl(SysDbgGetLiveKernelDump, NULL, 0, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    RtlZeroMemory(&Control, sizeof(Control));
    Control.Version = SYSDBG_LIVEDUMP_CONTROL_VERSION;
    Control.BugCheckCode = 0x161;
    Control.DumpFileHandle = File;
    Status = NtSystemDebugControl(SysDbgGetLiveKernelDump, &Control, sizeof(Control), NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok(GetFileSizeEx(File, &Size), "GetFileSizeEx failed with %lu\n", GetLastError());
    ok(Size.QuadPart > 0x2000, "live dump is 0x%I64x bytes\n", Size.QuadPart);

    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ok(ReadFile(File, Signature, sizeof(Signature), &Read, NULL) && Read == sizeof(Signature),
       "ReadFile failed with %lu\n", GetLastError());
    ok(RtlCompareMemory(Signature, "PAGEDU64", sizeof(Signature)) == sizeof(Signature),
       "live dump does not start with PAGEDU64\n");

    CloseHandle(File);
    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, Old, FALSE, &Old);
}
