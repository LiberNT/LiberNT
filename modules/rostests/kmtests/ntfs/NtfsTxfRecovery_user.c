/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transactional NTFS commit recovery test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <strsafe.h>

#include "NtfsTxfRecovery.h"

typedef HANDLE (WINAPI *PFN_CREATE_TRANSACTION)(LPSECURITY_ATTRIBUTES, LPGUID, DWORD, DWORD, DWORD, DWORD, LPWSTR);
typedef BOOL (WINAPI *PFN_COMMIT_TRANSACTION)(HANDLE);
typedef HANDLE (WINAPI *PFN_CREATE_FILE_TRANSACTED)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE,
                                                    HANDLE, PUSHORT, PVOID);
typedef BOOL (WINAPI *PFN_DELETE_FILE_TRANSACTED)(LPCWSTR, HANDLE);
typedef BOOL (WINAPI *PFN_MOVE_FILE_TRANSACTED)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD, HANDLE);

static PFN_CREATE_TRANSACTION pCreateTransaction;
static PFN_COMMIT_TRANSACTION pCommitTransaction;
static PFN_CREATE_FILE_TRANSACTED pCreateFileTransactedW;
static PFN_DELETE_FILE_TRANSACTED pDeleteFileTransactedW;
static PFN_MOVE_FILE_TRANSACTED pMoveFileTransactedW;

static WCHAR TestDir[MAX_PATH];

static
VOID
PathOf(
    _In_ PCWSTR Name,
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", TestDir, Name);
}

static
BOOL
WriteText(
    _In_ HANDLE File,
    _In_ PCSTR Text)
{
    DWORD Written = 0;

    return WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) && Written == strlen(Text) && SetEndOfFile(File);
}

static
BOOL
PutFile(
    _In_ PCWSTR Name,
    _In_ PCSTR Text)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    BOOL Result;

    PathOf(Name, Path);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteText(File, Text);
    CloseHandle(File);
    return Result;
}

static
BOOL
ReadsBack(
    _In_ PCWSTR Name,
    _In_ PCSTR Expected)
{
    WCHAR Path[MAX_PATH];
    CHAR Buffer[16] = { 0 };
    HANDLE File;
    DWORD Read = 0;

    PathOf(Name, Path);
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL);
    CloseHandle(File);
    return Read == strlen(Expected) && !memcmp(Buffer, Expected, Read);
}

static
BOOL
Exists(
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    PathOf(Name, Path);
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static
VOID
Remove(
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    PathOf(Name, Path);
    SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(Path);
}

static
BOOL
CreateTransacted(
    _In_ PCWSTR Name,
    _In_ DWORD Disposition,
    _In_ PCSTR Text,
    _In_ HANDLE Transaction)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    BOOL Result;

    PathOf(Name, Path);
    File = pCreateFileTransactedW(Path, GENERIC_WRITE, 0, NULL, Disposition, FILE_ATTRIBUTE_NORMAL, NULL,
                                  Transaction, NULL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteText(File, Text);
    CloseHandle(File);
    return Result;
}

static
BOOL
DeleteTransacted(
    _In_ PCWSTR Name,
    _In_ HANDLE Transaction)
{
    WCHAR Path[MAX_PATH];

    PathOf(Name, Path);
    return pDeleteFileTransactedW(Path, Transaction);
}

static
BOOL
MoveTransacted(
    _In_ PCWSTR Source,
    _In_ PCWSTR Target,
    _In_ HANDLE Transaction)
{
    WCHAR SourcePath[MAX_PATH], TargetPath[MAX_PATH];

    PathOf(Source, SourcePath);
    PathOf(Target, TargetPath);
    return pMoveFileTransactedW(SourcePath, TargetPath, NULL, NULL, 0, Transaction);
}

static
DWORD
Arm(
    _In_ ULONG Kind,
    _In_ ULONG FailAt)
{
    NTFSTXF_TRAP Trap;
    DWORD Length = 0;

    Trap.Kind = Kind;
    Trap.FailAt = FailAt;
    return KmtSendBufferToDriver(IOCTL_NTFSTXF_ARM, &Trap, sizeof(Trap), &Length);
}

static
ULONG
Fired(VOID)
{
    ULONG Count = MAXULONG;
    DWORD Length = sizeof(Count);

    if (KmtSendBufferToDriver(IOCTL_NTFSTXF_QUERY, &Count, 0, &Length) != ERROR_SUCCESS)
        return MAXULONG;
    return Count;
}

static
VOID
TestInterruptedCommit(VOID)
{
    static const PCWSTR Names[] = { L"again.txt", L"new.txt", L"c.txt", L"moved.txt", L"b.txt" };
    HANDLE Transaction;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
        Remove(Names[Index]);
    if (!PutFile(L"again.txt", "first") || !PutFile(L"c.txt", "c") || !PutFile(L"b.txt", "b"))
    {
        ok(0, "cannot prepare the test files (%lu)\n", GetLastError());
        return;
    }

    Transaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(Transaction != INVALID_HANDLE_VALUE, "CreateTransaction failed with %lu\n", GetLastError());
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(DeleteTransacted(L"again.txt", Transaction), "transacted delete failed with %lu\n", GetLastError());
    ok(CreateTransacted(L"again.txt", CREATE_NEW, "second", Transaction), "transacted recreate failed with %lu\n", GetLastError());
    ok(CreateTransacted(L"new.txt", CREATE_NEW, "new", Transaction), "transacted create failed with %lu\n", GetLastError());
    ok(MoveTransacted(L"c.txt", L"moved.txt", Transaction), "transacted move failed with %lu\n", GetLastError());
    ok(CreateTransacted(L"b.txt", OPEN_EXISTING, "bb", Transaction), "transacted write failed with %lu\n", GetLastError());

    /* The fourth namespace operation of the commit, the move, fails as a lost device would */
    ok_eq_int(Arm(NTFSTXF_TRAP_NAMESPACE, 4), ERROR_SUCCESS);
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    ok_eq_ulong(Fired(), 1UL);
    ok_eq_int(KmtSendToDriver(IOCTL_NTFSTXF_DISARM), ERROR_SUCCESS);
    CloseHandle(Transaction);

    ok(ReadsBack(L"again.txt", "second"), "recovery lost or undid the recreated file\n");
    ok(ReadsBack(L"new.txt", "new"), "recovery lost the created file\n");
    ok(!Exists(L"c.txt") && ReadsBack(L"moved.txt", "c"), "recovery did not finish the move\n");
    ok(ReadsBack(L"b.txt", "bb"), "recovery did not finish the transacted write\n");

    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
        Remove(Names[Index]);
}

static
VOID
TestFailedPrepare(VOID)
{
    HANDLE Transaction;
    BOOL Result;

    Remove(L"new2.txt");
    if (!PutFile(L"d.txt", "d"))
    {
        ok(0, "cannot prepare the test file (%lu)\n", GetLastError());
        return;
    }

    Transaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(Transaction != INVALID_HANDLE_VALUE, "CreateTransaction failed with %lu\n", GetLastError());
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(CreateTransacted(L"new2.txt", CREATE_NEW, "n2", Transaction), "transacted create failed with %lu\n", GetLastError());
    ok(DeleteTransacted(L"d.txt", Transaction), "transacted delete failed with %lu\n", GetLastError());

    /* Writing the commit log fails: the file system cannot prepare and the transaction rolls back */
    ok_eq_int(Arm(NTFSTXF_TRAP_WRITE, 1), ERROR_SUCCESS);
    SetLastError(0xdeadbeef);
    Result = pCommitTransaction(Transaction);
    ok(!Result, "CommitTransaction succeeded although the commit log could not be written\n");
    trace("CommitTransaction after a failed prepare: error %lu\n", GetLastError());
    ok_eq_ulong(Fired(), 1UL);
    ok_eq_int(KmtSendToDriver(IOCTL_NTFSTXF_DISARM), ERROR_SUCCESS);
    CloseHandle(Transaction);
    ok(!Exists(L"new2.txt"), "rolled back create is visible\n");
    ok(ReadsBack(L"d.txt", "d"), "rolled back delete removed the file\n");

    Transaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(Transaction != INVALID_HANDLE_VALUE, "CreateTransaction failed with %lu\n", GetLastError());
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(CreateTransacted(L"new2.txt", CREATE_NEW, "n2", Transaction), "transacted create failed with %lu\n", GetLastError());
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(L"new2.txt", "n2"), "commit after a rolled back transaction failed\n");

    Remove(L"new2.txt");
    Remove(L"d.txt");
}

START_TEST(NtfsTxfRecovery)
{
    WCHAR Temp[MAX_PATH], Root[4] = L"C:\\";
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Ktmw32 = LoadLibraryW(L"ktmw32.dll");
    DWORD Flags = 0;
    DWORD Error;

    pCreateFileTransactedW = (PFN_CREATE_FILE_TRANSACTED)GetProcAddress(Kernel32, "CreateFileTransactedW");
    pDeleteFileTransactedW = (PFN_DELETE_FILE_TRANSACTED)GetProcAddress(Kernel32, "DeleteFileTransactedW");
    pMoveFileTransactedW = (PFN_MOVE_FILE_TRANSACTED)GetProcAddress(Kernel32, "MoveFileTransactedW");
    if (Ktmw32)
    {
        pCreateTransaction = (PFN_CREATE_TRANSACTION)GetProcAddress(Ktmw32, "CreateTransaction");
        pCommitTransaction = (PFN_COMMIT_TRANSACTION)GetProcAddress(Ktmw32, "CommitTransaction");
    }
    if (!pCreateFileTransactedW || !pDeleteFileTransactedW || !pMoveFileTransactedW ||
        !pCreateTransaction || !pCommitTransaction)
    {
        skip(FALSE, "transacted file or transaction functions are missing\n");
        return;
    }

    GetTempPathW(RTL_NUMBER_OF(Temp), Temp);
    Root[0] = Temp[0];
    if (!GetVolumeInformationW(Root, NULL, 0, NULL, NULL, &Flags, NULL, 0) || !(Flags & FILE_SUPPORTS_TRANSACTIONS))
    {
        skip(FALSE, "the temporary directory's volume does not support transactions\n");
        return;
    }
    StringCchPrintfW(TestDir, RTL_NUMBER_OF(TestDir), L"%sTxfRecoveryTest", Temp);
    CreateDirectoryW(TestDir, NULL);

    Error = KmtLoadAndOpenDriver(L"NtfsTxfRecovery", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        RemoveDirectoryW(TestDir);
        return;
    }
    Error = KmtSendToDriver(IOCTL_NTFSTXF_REGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (!Error)
    {
        TestInterruptedCommit();
        TestFailedPrepare();
    }
    KmtSendToDriver(IOCTL_NTFSTXF_UNREGISTER);
    KmtCloseDriver();
    KmtUnloadDriver();
    RemoveDirectoryW(TestDir);
}
