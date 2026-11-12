/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the transacted file functions of kernel32
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HANDLE (WINAPI *PFN_CREATE_TRANSACTION)(LPSECURITY_ATTRIBUTES, LPGUID, DWORD, DWORD, DWORD, DWORD, LPWSTR);
typedef BOOL (WINAPI *PFN_END_TRANSACTION)(HANDLE);
typedef HANDLE (NTAPI *PFN_RTL_GET_CURRENT_TRANSACTION)(VOID);
typedef LOGICAL (NTAPI *PFN_RTL_SET_CURRENT_TRANSACTION)(HANDLE);

typedef BOOL (WINAPI *PFN_COPY_W)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_COPY_A)(LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_MKDIR_W)(LPCWSTR, LPCWSTR, LPSECURITY_ATTRIBUTES, HANDLE);
typedef BOOL (WINAPI *PFN_MKDIR_A)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES, HANDLE);
typedef HANDLE (WINAPI *PFN_CREATE_W)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE, HANDLE, PUSHORT, PVOID);
typedef HANDLE (WINAPI *PFN_CREATE_A)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE, HANDLE, PUSHORT, PVOID);
typedef BOOL (WINAPI *PFN_HARDLINK_W)(LPCWSTR, LPCWSTR, LPSECURITY_ATTRIBUTES, HANDLE);
typedef BOOL (WINAPI *PFN_HARDLINK_A)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES, HANDLE);
typedef BOOLEAN (WINAPI *PFN_SYMLINK_W)(LPCWSTR, LPCWSTR, DWORD, HANDLE);
typedef BOOLEAN (WINAPI *PFN_SYMLINK_A)(LPCSTR, LPCSTR, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_PATH_W)(LPCWSTR, HANDLE);
typedef BOOL (WINAPI *PFN_PATH_A)(LPCSTR, HANDLE);
typedef HANDLE (WINAPI *PFN_FIND_NAME_W)(LPCWSTR, DWORD, LPDWORD, PWSTR, HANDLE);
typedef HANDLE (WINAPI *PFN_FIND_W)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFN_FIND_A)(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFN_FIND_STREAM_W)(LPCWSTR, STREAM_INFO_LEVELS, LPVOID, DWORD, HANDLE);
typedef DWORD (WINAPI *PFN_COMPRESSED_W)(LPCWSTR, LPDWORD, HANDLE);
typedef DWORD (WINAPI *PFN_COMPRESSED_A)(LPCSTR, LPDWORD, HANDLE);
typedef BOOL (WINAPI *PFN_GETATTR_W)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID, HANDLE);
typedef BOOL (WINAPI *PFN_GETATTR_A)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID, HANDLE);
typedef DWORD (WINAPI *PFN_FULLPATH_W)(LPCWSTR, DWORD, LPWSTR, LPWSTR *, HANDLE);
typedef DWORD (WINAPI *PFN_FULLPATH_A)(LPCSTR, DWORD, LPSTR, LPSTR *, HANDLE);
typedef DWORD (WINAPI *PFN_LONGPATH_W)(LPCWSTR, LPWSTR, DWORD, HANDLE);
typedef DWORD (WINAPI *PFN_LONGPATH_A)(LPCSTR, LPSTR, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_MOVE_W)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_MOVE_A)(LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_SETATTR_W)(LPCWSTR, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_SETATTR_A)(LPCSTR, DWORD, HANDLE);

static PFN_CREATE_TRANSACTION pCreateTransaction;
static PFN_END_TRANSACTION pCommitTransaction;
static PFN_END_TRANSACTION pRollbackTransaction;
static PFN_RTL_GET_CURRENT_TRANSACTION pRtlGetCurrentTransaction;
static PFN_RTL_SET_CURRENT_TRANSACTION pRtlSetCurrentTransaction;

static PFN_COPY_W pCopyFileTransactedW;
static PFN_COPY_A pCopyFileTransactedA;
static PFN_MKDIR_W pCreateDirectoryTransactedW;
static PFN_MKDIR_A pCreateDirectoryTransactedA;
static PFN_CREATE_W pCreateFileTransactedW;
static PFN_CREATE_A pCreateFileTransactedA;
static PFN_HARDLINK_W pCreateHardLinkTransactedW;
static PFN_HARDLINK_A pCreateHardLinkTransactedA;
static PFN_SYMLINK_W pCreateSymbolicLinkTransactedW;
static PFN_SYMLINK_A pCreateSymbolicLinkTransactedA;
static PFN_PATH_W pDeleteFileTransactedW;
static PFN_PATH_A pDeleteFileTransactedA;
static PFN_FIND_NAME_W pFindFirstFileNameTransactedW;
static PFN_FIND_W pFindFirstFileTransactedW;
static PFN_FIND_A pFindFirstFileTransactedA;
static PFN_FIND_STREAM_W pFindFirstStreamTransactedW;
static PFN_COMPRESSED_W pGetCompressedFileSizeTransactedW;
static PFN_COMPRESSED_A pGetCompressedFileSizeTransactedA;
static PFN_GETATTR_W pGetFileAttributesTransactedW;
static PFN_GETATTR_A pGetFileAttributesTransactedA;
static PFN_FULLPATH_W pGetFullPathNameTransactedW;
static PFN_FULLPATH_A pGetFullPathNameTransactedA;
static PFN_LONGPATH_W pGetLongPathNameTransactedW;
static PFN_LONGPATH_A pGetLongPathNameTransactedA;
static PFN_MOVE_W pMoveFileTransactedW;
static PFN_MOVE_A pMoveFileTransactedA;
static PFN_PATH_W pRemoveDirectoryTransactedW;
static PFN_PATH_A pRemoveDirectoryTransactedA;
static PFN_SETATTR_W pSetFileAttributesTransactedW;
static PFN_SETATTR_A pSetFileAttributesTransactedA;

typedef struct _TX_DIR
{
    WCHAR Dir[MAX_PATH];
    CHAR DirA[MAX_PATH];
} TX_DIR, *PTX_DIR;

static
BOOL
LoadFunctions(VOID)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE Ktm = LoadLibraryW(L"ktmw32.dll");

#define LOAD(Module, Name) (p##Name = (void *)GetProcAddress(Module, #Name))
    LOAD(Kernel32, CopyFileTransactedW);
    LOAD(Kernel32, CopyFileTransactedA);
    LOAD(Kernel32, CreateDirectoryTransactedW);
    LOAD(Kernel32, CreateDirectoryTransactedA);
    LOAD(Kernel32, CreateFileTransactedW);
    LOAD(Kernel32, CreateFileTransactedA);
    LOAD(Kernel32, CreateHardLinkTransactedW);
    LOAD(Kernel32, CreateHardLinkTransactedA);
    LOAD(Kernel32, CreateSymbolicLinkTransactedW);
    LOAD(Kernel32, CreateSymbolicLinkTransactedA);
    LOAD(Kernel32, DeleteFileTransactedW);
    LOAD(Kernel32, DeleteFileTransactedA);
    LOAD(Kernel32, FindFirstFileNameTransactedW);
    LOAD(Kernel32, FindFirstFileTransactedW);
    LOAD(Kernel32, FindFirstFileTransactedA);
    LOAD(Kernel32, FindFirstStreamTransactedW);
    LOAD(Kernel32, GetCompressedFileSizeTransactedW);
    LOAD(Kernel32, GetCompressedFileSizeTransactedA);
    LOAD(Kernel32, GetFileAttributesTransactedW);
    LOAD(Kernel32, GetFileAttributesTransactedA);
    LOAD(Kernel32, GetFullPathNameTransactedW);
    LOAD(Kernel32, GetFullPathNameTransactedA);
    LOAD(Kernel32, GetLongPathNameTransactedW);
    LOAD(Kernel32, GetLongPathNameTransactedA);
    LOAD(Kernel32, MoveFileTransactedW);
    LOAD(Kernel32, MoveFileTransactedA);
    LOAD(Kernel32, RemoveDirectoryTransactedW);
    LOAD(Kernel32, RemoveDirectoryTransactedA);
    LOAD(Kernel32, SetFileAttributesTransactedW);
    LOAD(Kernel32, SetFileAttributesTransactedA);
    LOAD(Ntdll, RtlGetCurrentTransaction);
    LOAD(Ntdll, RtlSetCurrentTransaction);
    if (Ktm != NULL)
    {
        LOAD(Ktm, CreateTransaction);
        LOAD(Ktm, CommitTransaction);
        LOAD(Ktm, RollbackTransaction);
    }
#undef LOAD

    return pCopyFileTransactedW && pCopyFileTransactedA && pCreateDirectoryTransactedW &&
           pCreateDirectoryTransactedA && pCreateFileTransactedW && pCreateFileTransactedA &&
           pCreateHardLinkTransactedW && pCreateHardLinkTransactedA && pCreateSymbolicLinkTransactedW &&
           pCreateSymbolicLinkTransactedA && pDeleteFileTransactedW && pDeleteFileTransactedA &&
           pFindFirstFileNameTransactedW && pFindFirstFileTransactedW && pFindFirstFileTransactedA &&
           pFindFirstStreamTransactedW && pGetCompressedFileSizeTransactedW &&
           pGetCompressedFileSizeTransactedA && pGetFileAttributesTransactedW &&
           pGetFileAttributesTransactedA && pGetFullPathNameTransactedW && pGetFullPathNameTransactedA &&
           pGetLongPathNameTransactedW && pGetLongPathNameTransactedA && pMoveFileTransactedW &&
           pMoveFileTransactedA && pRemoveDirectoryTransactedW && pRemoveDirectoryTransactedA &&
           pSetFileAttributesTransactedW && pSetFileAttributesTransactedA;
}

static
BOOL
EnableSymbolicLinkPrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Enabled;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return FALSE;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Enabled = LookupPrivilegeValue(NULL, SE_CREATE_SYMBOLIC_LINK_NAME, &Privileges.Privileges[0].Luid) &&
              AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Enabled;
}

static
HANDLE
NewTransaction(VOID)
{
    HANDLE Transaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);

    ok(Transaction != INVALID_HANDLE_VALUE, "CreateTransaction failed with %lu\n", GetLastError());
    return Transaction;
}

static
VOID
PathOf(
    _In_ PTX_DIR Dir,
    _In_ PCWSTR Name,
    _Out_writes_(MAX_PATH) PWSTR Path,
    _Out_writes_(MAX_PATH) PSTR PathA)
{
    StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", Dir->Dir, Name);
    StringCchPrintfA(PathA, MAX_PATH, "%s\\%S", Dir->DirA, Name);
}

static
BOOL
WriteTestFile(
    _In_ PCWSTR Path,
    _In_ PCSTR Data)
{
    HANDLE File;
    DWORD Written = 0;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(File, Data, (DWORD)strlen(Data), &Written, NULL);
    CloseHandle(File);
    return Written == strlen(Data);
}

static
BOOL
Exists(
    _In_ PCWSTR Path)
{
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static
VOID
RemoveTree(
    _In_ PTX_DIR Dir)
{
    static const PCWSTR Files[] = { L"exist.txt", L"exist2.txt", L"new.txt", L"newA.txt", L"new2.txt",
                                    L"copy.txt", L"copyA.txt", L"moved.txt", L"movedA.txt", L"link.txt",
                                    L"linkA.txt", L"sym.txt", L"symA.txt", L"attr.txt", L"data.txt",
                                    L"hard.txt", L"again.txt", L"target.txt", L"fresh.txt", L"tree\\inner.txt",
                                    L"full\\a.txt", L"full\\b.txt", L"full\\c.txt", L"outside.txt",
                                    L"Norm.txt", L"norm2.txt", L"Renamed.txt", L"norm3.txt", L"streams.txt" };
    static const PCWSTR Dirs[] = { L"sub", L"subA", L"newdir", L"newdirA", L"tree", L"full" };
    WCHAR Path[MAX_PATH];
    CHAR PathA[MAX_PATH];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Files); Index++)
    {
        PathOf(Dir, Files[Index], Path, PathA);
        SetFileAttributesW(Path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Path);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Dirs); Index++)
    {
        PathOf(Dir, Dirs[Index], Path, PathA);
        RemoveDirectoryW(Path);
    }
    RemoveDirectoryW(Dir->Dir);
}

static
BOOL
PrepareTree(
    _In_ PTX_DIR Dir)
{
    WCHAR Path[MAX_PATH];
    CHAR PathA[MAX_PATH];

    RemoveTree(Dir);
    if (!CreateDirectoryW(Dir->Dir, NULL))
    {
        ok(0, "CreateDirectory(%S) failed with %lu\n", Dir->Dir, GetLastError());
        return FALSE;
    }
    PathOf(Dir, L"exist.txt", Path, PathA);
    if (!WriteTestFile(Path, "txf"))
        return FALSE;
    PathOf(Dir, L"exist2.txt", Path, PathA);
    if (!WriteTestFile(Path, "move"))
        return FALSE;
    PathOf(Dir, L"attr.txt", Path, PathA);
    if (!WriteTestFile(Path, "attr"))
        return FALSE;
    PathOf(Dir, L"data.txt", Path, PathA);
    if (!WriteTestFile(Path, "old"))
        return FALSE;
    PathOf(Dir, L"again.txt", Path, PathA);
    if (!WriteTestFile(Path, "first"))
        return FALSE;
    PathOf(Dir, L"target.txt", Path, PathA);
    if (!WriteTestFile(Path, "target"))
        return FALSE;
    PathOf(Dir, L"sub", Path, PathA);
    ok(CreateDirectoryW(Path, NULL), "CreateDirectory(%S) failed with %lu\n", Path, GetLastError());
    PathOf(Dir, L"subA", Path, PathA);
    ok(CreateDirectoryW(Path, NULL), "CreateDirectory(%S) failed with %lu\n", Path, GetLastError());
    return TRUE;
}

static
BOOL
VolumeSupportsTransactions(
    _In_ PCWSTR Dir,
    _Out_writes_(32) PWSTR FileSystem)
{
    WCHAR Root[4] = { Dir[0], L':', L'\\', 0 };
    DWORD Flags = 0;

    FileSystem[0] = UNICODE_NULL;
    if (!GetVolumeInformationW(Root, NULL, 0, NULL, NULL, &Flags, FileSystem, 32))
    {
        ok(0, "GetVolumeInformation(%S) failed with %lu\n", Root, GetLastError());
        return FALSE;
    }
    return (Flags & FILE_SUPPORTS_TRANSACTIONS) != 0;
}

#define ok_refused(Result, Name) \
    do { \
        BOOL Refused_ = !(Result); \
        DWORD Error_ = GetLastError(); \
        ok(Refused_, "%s succeeded on a volume without transactions\n", Name); \
        ok(Error_ == ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED, "%s: error %lu\n", Name, Error_); \
    } while (0)

static
VOID
TestUnsupportedVolume(
    _In_ PTX_DIR Dir)
{
    WCHAR Exist[MAX_PATH], Exist2[MAX_PATH], New[MAX_PATH], Copy[MAX_PATH], Moved[MAX_PATH];
    WCHAR Link[MAX_PATH], Sym[MAX_PATH], Sub[MAX_PATH], NewDir[MAX_PATH], Pattern[MAX_PATH], Attr[MAX_PATH];
    CHAR ExistA[MAX_PATH], Exist2A[MAX_PATH], NewA[MAX_PATH], CopyA[MAX_PATH], MovedA[MAX_PATH];
    CHAR LinkA[MAX_PATH], SymA[MAX_PATH], SubA[MAX_PATH], NewDirA[MAX_PATH], PatternA[MAX_PATH], AttrA[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Data;
    WIN32_FIND_STREAM_DATA StreamData;
    WIN32_FIND_DATAW FindData;
    WIN32_FIND_DATAA FindDataA;
    WCHAR Buffer[MAX_PATH];
    CHAR BufferA[MAX_PATH];
    HANDLE Transaction;
    HANDLE Handle;
    DWORD Length;
    DWORD High;
    DWORD Result;

    PathOf(Dir, L"exist.txt", Exist, ExistA);
    PathOf(Dir, L"exist2.txt", Exist2, Exist2A);
    PathOf(Dir, L"new.txt", New, Scratch);
    PathOf(Dir, L"newA.txt", Buffer, NewA);
    PathOf(Dir, L"copy.txt", Copy, Scratch);
    PathOf(Dir, L"copyA.txt", Buffer, CopyA);
    PathOf(Dir, L"moved.txt", Moved, Scratch);
    PathOf(Dir, L"movedA.txt", Buffer, MovedA);
    PathOf(Dir, L"link.txt", Link, Scratch);
    PathOf(Dir, L"linkA.txt", Buffer, LinkA);
    PathOf(Dir, L"sym.txt", Sym, Scratch);
    PathOf(Dir, L"symA.txt", Buffer, SymA);
    PathOf(Dir, L"sub", Sub, Scratch);
    PathOf(Dir, L"subA", Buffer, SubA);
    PathOf(Dir, L"newdir", NewDir, Scratch);
    PathOf(Dir, L"newdirA", Buffer, NewDirA);
    PathOf(Dir, L"*", Pattern, PatternA);
    PathOf(Dir, L"attr.txt", Attr, AttrA);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(New, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "CreateFileTransactedW(CREATE_NEW)");
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(!Exists(New), "refused create left %S behind\n", New);

    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(Exist, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "CreateFileTransactedW(OPEN_EXISTING)");
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);

    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedA(NewA, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "CreateFileTransactedA(CREATE_NEW)");
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(GetFileAttributesA(NewA) == INVALID_FILE_ATTRIBUTES, "refused create left %s behind\n", NewA);

    SetLastError(0xdeadbeef);
    ok_refused(pCreateDirectoryTransactedW(NULL, NewDir, NULL, Transaction), "CreateDirectoryTransactedW");
    ok(!Exists(NewDir), "refused directory %S exists\n", NewDir);
    SetLastError(0xdeadbeef);
    ok_refused(pCreateDirectoryTransactedW(Sub, NewDir, NULL, Transaction), "CreateDirectoryTransactedW(template)");
    ok(!Exists(NewDir), "refused directory %S exists\n", NewDir);
    SetLastError(0xdeadbeef);
    ok_refused(pCreateDirectoryTransactedA(NULL, NewDirA, NULL, Transaction), "CreateDirectoryTransactedA");
    ok(GetFileAttributesA(NewDirA) == INVALID_FILE_ATTRIBUTES, "refused directory %s exists\n", NewDirA);

    SetLastError(0xdeadbeef);
    ok_refused(pDeleteFileTransactedW(Exist, Transaction), "DeleteFileTransactedW");
    ok(Exists(Exist), "refused delete removed %S\n", Exist);
    SetLastError(0xdeadbeef);
    ok_refused(pDeleteFileTransactedA(ExistA, Transaction), "DeleteFileTransactedA");
    ok(Exists(Exist), "refused delete removed %S\n", Exist);

    SetLastError(0xdeadbeef);
    ok_refused(pRemoveDirectoryTransactedW(Sub, Transaction), "RemoveDirectoryTransactedW");
    ok(Exists(Sub), "refused remove deleted %S\n", Sub);
    SetLastError(0xdeadbeef);
    ok_refused(pRemoveDirectoryTransactedA(SubA, Transaction), "RemoveDirectoryTransactedA");
    ok(GetFileAttributesA(SubA) != INVALID_FILE_ATTRIBUTES, "refused remove deleted %s\n", SubA);

    SetLastError(0xdeadbeef);
    ok_refused(pMoveFileTransactedW(Exist2, Moved, NULL, NULL, 0, Transaction), "MoveFileTransactedW");
    ok(Exists(Exist2) && !Exists(Moved), "refused move changed the namespace\n");
    SetLastError(0xdeadbeef);
    ok_refused(pMoveFileTransactedA(Exist2A, MovedA, NULL, NULL, 0, Transaction), "MoveFileTransactedA");
    ok(Exists(Exist2) && GetFileAttributesA(MovedA) == INVALID_FILE_ATTRIBUTES, "refused move changed the namespace\n");

    SetLastError(0xdeadbeef);
    ok_refused(pCopyFileTransactedW(Exist, Copy, NULL, NULL, NULL, 0, Transaction), "CopyFileTransactedW");
    ok(!Exists(Copy), "refused copy created %S\n", Copy);
    SetLastError(0xdeadbeef);
    ok_refused(pCopyFileTransactedA(ExistA, CopyA, NULL, NULL, NULL, 0, Transaction), "CopyFileTransactedA");
    ok(GetFileAttributesA(CopyA) == INVALID_FILE_ATTRIBUTES, "refused copy created %s\n", CopyA);

    SetLastError(0xdeadbeef);
    ok_refused(pCreateHardLinkTransactedW(Link, Exist, NULL, Transaction), "CreateHardLinkTransactedW");
    ok(!Exists(Link), "refused hard link %S exists\n", Link);
    SetLastError(0xdeadbeef);
    ok_refused(pCreateHardLinkTransactedA(LinkA, ExistA, NULL, Transaction), "CreateHardLinkTransactedA");
    ok(GetFileAttributesA(LinkA) == INVALID_FILE_ATTRIBUTES, "refused hard link %s exists\n", LinkA);

    if (EnableSymbolicLinkPrivilege())
    {
        SetLastError(0xdeadbeef);
        ok_refused(pCreateSymbolicLinkTransactedW(Sym, Exist, 0, Transaction), "CreateSymbolicLinkTransactedW");
        ok(!Exists(Sym), "refused symbolic link %S exists\n", Sym);
        SetLastError(0xdeadbeef);
        ok_refused(pCreateSymbolicLinkTransactedA(SymA, ExistA, 0, Transaction), "CreateSymbolicLinkTransactedA");
        ok(GetFileAttributesA(SymA) == INVALID_FILE_ATTRIBUTES, "refused symbolic link %s exists\n", SymA);
    }
    else
    {
        skip("no symbolic link privilege\n");
    }

    SetLastError(0xdeadbeef);
    ok_refused(pGetFileAttributesTransactedW(Exist, GetFileExInfoStandard, &Data, Transaction), "GetFileAttributesTransactedW");
    SetLastError(0xdeadbeef);
    ok_refused(pGetFileAttributesTransactedA(ExistA, GetFileExInfoStandard, &Data, Transaction), "GetFileAttributesTransactedA");

    SetLastError(0xdeadbeef);
    ok_refused(pSetFileAttributesTransactedW(Attr, FILE_ATTRIBUTE_HIDDEN, Transaction), "SetFileAttributesTransactedW");
    ok(!(GetFileAttributesW(Attr) & FILE_ATTRIBUTE_HIDDEN), "refused attribute change was applied\n");
    SetLastError(0xdeadbeef);
    ok_refused(pSetFileAttributesTransactedA(AttrA, FILE_ATTRIBUTE_HIDDEN, Transaction), "SetFileAttributesTransactedA");
    ok(!(GetFileAttributesW(Attr) & FILE_ATTRIBUTE_HIDDEN), "refused attribute change was applied\n");

    SetLastError(0xdeadbeef);
    High = 0x1234;
    Result = pGetCompressedFileSizeTransactedW(Exist, &High, Transaction);
    ok(Result == INVALID_FILE_SIZE, "GetCompressedFileSizeTransactedW returned %lu\n", Result);
    ok(GetLastError() == ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED, "GetCompressedFileSizeTransactedW: error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Result = pGetCompressedFileSizeTransactedA(ExistA, NULL, Transaction);
    ok(Result == INVALID_FILE_SIZE, "GetCompressedFileSizeTransactedA returned %lu\n", Result);
    ok(GetLastError() == ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED, "GetCompressedFileSizeTransactedA: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Handle = pFindFirstFileTransactedW(Pattern, FindExInfoStandard, &FindData, FindExSearchNameMatch, NULL, 0, Transaction);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "FindFirstFileTransactedW");
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);
    SetLastError(0xdeadbeef);
    Handle = pFindFirstFileTransactedA(PatternA, FindExInfoStandard, &FindDataA, FindExSearchNameMatch, NULL, 0, Transaction);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "FindFirstFileTransactedA");
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);

    SetLastError(0xdeadbeef);
    Length = RTL_NUMBER_OF(Buffer);
    Handle = pFindFirstFileNameTransactedW(Exist, 0, &Length, Buffer, Transaction);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "FindFirstFileNameTransactedW");
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);

    SetLastError(0xdeadbeef);
    Handle = pFindFirstStreamTransactedW(Exist, FindStreamInfoStandard, &StreamData, 0, Transaction);
    ok_refused(Handle != INVALID_HANDLE_VALUE, "FindFirstStreamTransactedW");
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);

    SetLastError(0xdeadbeef);
    Result = pGetLongPathNameTransactedW(Exist, Buffer, RTL_NUMBER_OF(Buffer), Transaction);
    ok(Result == 0, "GetLongPathNameTransactedW returned %lu\n", Result);
    ok(GetLastError() == ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED, "GetLongPathNameTransactedW: error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Result = pGetLongPathNameTransactedA(ExistA, BufferA, RTL_NUMBER_OF(BufferA), Transaction);
    ok(Result == 0, "GetLongPathNameTransactedA returned %lu\n", Result);
    ok(GetLastError() == ERROR_TRANSACTIONAL_OPEN_NOT_ALLOWED, "GetLongPathNameTransactedA: error %lu\n", GetLastError());

    ok(pRtlGetCurrentTransaction() == NULL, "thread transaction %p after the calls\n", pRtlGetCurrentTransaction());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
}

static
BOOL
ReadsBack(
    _In_ PCWSTR Path,
    _In_ PCSTR Expected)
{
    CHAR Buffer[16] = { 0 };
    HANDLE File;
    DWORD Read = 0;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL);
    CloseHandle(File);
    return Read == strlen(Expected) && !memcmp(Buffer, Expected, Read);
}

static
BOOL
ListedOutside(
    _In_ PCWSTR Pattern,
    _In_ PCWSTR Name)
{
    WIN32_FIND_DATAW Data;
    BOOL Found = FALSE;
    HANDLE Find;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (!_wcsicmp(Data.cFileName, Name))
            Found = TRUE;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Found;
}

static
VOID
TestSupportedVolume(
    _In_ PTX_DIR Dir)
{
    WCHAR Exist[MAX_PATH], Exist2[MAX_PATH], New[MAX_PATH], New2[MAX_PATH], Copy[MAX_PATH];
    WCHAR Moved[MAX_PATH], Sub[MAX_PATH], NewDir[MAX_PATH], Pattern[MAX_PATH], Attr[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Data;
    HANDLE Transaction;
    HANDLE Handle;
    DWORD Written;

    PathOf(Dir, L"exist.txt", Exist, Scratch);
    PathOf(Dir, L"exist2.txt", Exist2, Scratch);
    PathOf(Dir, L"new.txt", New, Scratch);
    PathOf(Dir, L"new2.txt", New2, Scratch);
    PathOf(Dir, L"copy.txt", Copy, Scratch);
    PathOf(Dir, L"moved.txt", Moved, Scratch);
    PathOf(Dir, L"sub", Sub, Scratch);
    PathOf(Dir, L"newdir", NewDir, Scratch);
    PathOf(Dir, L"*", Pattern, Scratch);
    PathOf(Dir, L"attr.txt", Attr, Scratch);

    /* A file created in a transaction stays invisible outside it and reserves its name until commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Handle = pCreateFileTransactedW(New, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "CreateFileTransactedW failed with %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        ok(WriteFile(Handle, "new", 3, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
        CloseHandle(Handle);
    }
    SetLastError(0xdeadbeef);
    ok(GetFileAttributesW(New) == INVALID_FILE_ATTRIBUTES, "uncommitted file visible outside\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "outside lookup error %lu\n", GetLastError());
    ok(!ListedOutside(Pattern, L"new.txt"), "uncommitted file listed outside\n");
    ok(pGetFileAttributesTransactedW(New, GetFileExInfoStandard, &Data, Transaction),
       "transacted lookup failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Handle = CreateFileW(New, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    ok(Handle == INVALID_HANDLE_VALUE, "outside create of a reserved name succeeded\n");
    ok(GetLastError() == ERROR_TRANSACTIONAL_CONFLICT, "outside create error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(New, "new"), "committed file missing or wrong\n");

    /* Rollback discards a creation */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Handle = pCreateFileTransactedW(New2, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "CreateFileTransactedW failed with %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(New2), "rolled back file exists\n");

    /* A deleted file stays visible outside the transaction until commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(Exist, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(Exists(Exist), "transacted delete visible outside before commit\n");
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Exist, "txf"), "rolled back delete lost the file\n");
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(Exist, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(Exist), "committed delete left the file\n");

    /* Directory creation and removal */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pCreateDirectoryTransactedW(NULL, NewDir, NULL, Transaction), "CreateDirectoryTransactedW failed with %lu\n", GetLastError());
    ok(!Exists(NewDir), "uncommitted directory visible outside\n");
    ok(pRemoveDirectoryTransactedW(Sub, Transaction), "RemoveDirectoryTransactedW failed with %lu\n", GetLastError());
    ok(Exists(Sub), "transacted directory removal visible outside before commit\n");
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(NewDir), "rolled back directory exists\n");
    ok(Exists(Sub), "rolled back removal lost the directory\n");

    /* Rename and copy become visible at commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pMoveFileTransactedW(Exist2, Moved, NULL, NULL, 0, Transaction), "MoveFileTransactedW failed with %lu\n", GetLastError());
    ok(pCopyFileTransactedW(New, Copy, NULL, NULL, NULL, 0, Transaction), "CopyFileTransactedW failed with %lu\n", GetLastError());
    ok(Exists(Exist2) && !Exists(Moved), "uncommitted rename visible outside\n");
    ok(!Exists(Copy), "uncommitted copy visible outside\n");
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(Exist2) && ReadsBack(Moved, "move"), "committed rename not applied\n");
    ok(ReadsBack(Copy, "new"), "committed copy missing\n");

    /* An attribute change is undone by rollback */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pSetFileAttributesTransactedW(Attr, FILE_ATTRIBUTE_HIDDEN, Transaction), "SetFileAttributesTransactedW failed with %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!(GetFileAttributesW(Attr) & FILE_ATTRIBUTE_HIDDEN), "rolled back attribute change kept\n");
}

static
BOOL
ListedInTransaction(
    _In_ PCWSTR Pattern,
    _In_ PCWSTR Name,
    _In_ HANDLE Transaction)
{
    WIN32_FIND_DATAW Data;
    BOOL Found = FALSE;
    HANDLE Find;

    Find = pFindFirstFileTransactedW(Pattern, FindExInfoStandard, &Data, FindExSearchNameMatch, NULL, 0, Transaction);
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (!_wcsicmp(Data.cFileName, Name))
            Found = TRUE;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Found;
}

static
BOOL
WriteThrough(
    _In_ HANDLE File,
    _In_ PCSTR Data)
{
    DWORD Written = 0;

    return SetFilePointer(File, 0, NULL, FILE_BEGIN) == 0 &&
           WriteFile(File, Data, (DWORD)strlen(Data), &Written, NULL) &&
           Written == strlen(Data) &&
           SetEndOfFile(File);
}

static
VOID
TestSupportedVolumeData(
    _In_ PTX_DIR Dir)
{
    WCHAR Data[MAX_PATH], Hard[MAX_PATH], Again[MAX_PATH], Target[MAX_PATH], Moved[MAX_PATH];
    WCHAR Tree[MAX_PATH], Inner[MAX_PATH], Pattern[MAX_PATH], New[MAX_PATH], Exist2[MAX_PATH];
    WCHAR Buffer[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Info;
    HANDLE Transaction;
    HANDLE Writer, Reader, Outsider;
    DWORD Length;

    PathOf(Dir, L"data.txt", Data, Scratch);
    PathOf(Dir, L"hard.txt", Hard, Scratch);
    PathOf(Dir, L"again.txt", Again, Scratch);
    PathOf(Dir, L"target.txt", Target, Scratch);
    PathOf(Dir, L"moved.txt", Moved, Scratch);
    PathOf(Dir, L"tree", Tree, Scratch);
    PathOf(Dir, L"tree\\inner.txt", Inner, Scratch);
    PathOf(Dir, L"*", Pattern, Scratch);
    PathOf(Dir, L"fresh.txt", New, Scratch);
    PathOf(Dir, L"attr.txt", Exist2, Scratch);

    /* A transacted writer's data stays private until commit, and a rollback drops it */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Writer = pCreateFileTransactedW(Data, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL,
                                    Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "transacted writer open failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        ok(WriteThrough(Writer, "new data"), "transacted write failed with %lu\n", GetLastError());
        ok(ReadsBack(Data, "old"), "outside reader sees uncommitted data\n");
        SetLastError(0xdeadbeef);
        Outsider = CreateFileW(Data, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        ok(Outsider == INVALID_HANDLE_VALUE, "outside writer opened a transactionally locked file\n");
        ok(GetLastError() == ERROR_SHARING_VIOLATION, "outside writer error %lu\n", GetLastError());
        if (Outsider != INVALID_HANDLE_VALUE) CloseHandle(Outsider);
        CloseHandle(Writer);
    }
    ok(pGetFileAttributesTransactedW(Data, GetFileExInfoStandard, &Info, Transaction),
       "transacted query failed with %lu\n", GetLastError());
    ok(Info.nFileSizeLow == 8, "transacted size %lu, expected 8\n", Info.nFileSizeLow);
    ok(GetFileAttributesExW(Data, GetFileExInfoStandard, &Info) && Info.nFileSizeLow == 3,
       "outside size %lu, expected 3\n", Info.nFileSizeLow);
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Data, "old"), "rolled back data visible\n");

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Writer = pCreateFileTransactedW(Data, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "transacted writer open failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        ok(WriteThrough(Writer, "committed"), "transacted write failed with %lu\n", GetLastError());
        CloseHandle(Writer);
    }
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Data, "committed"), "committed data missing\n");

    /* A non-transacted writer keeps a transaction from opening the file */
    Outsider = CreateFileW(Data, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Outsider != INVALID_HANDLE_VALUE, "outside writer open failed with %lu\n", GetLastError());
    Transaction = NewTransaction();
    if (Outsider != INVALID_HANDLE_VALUE && Transaction != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Writer = pCreateFileTransactedW(Data, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                                        NULL, Transaction, NULL, NULL);
        ok(Writer == INVALID_HANDLE_VALUE, "transacted writer opened past a non-transacted writer\n");
        ok(GetLastError() == ERROR_TRANSACTIONAL_CONFLICT, "transacted writer error %lu\n", GetLastError());
        if (Writer != INVALID_HANDLE_VALUE) CloseHandle(Writer);
        SetLastError(0xdeadbeef);
        Reader = pCreateFileTransactedW(Data, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                                        NULL, Transaction, NULL, NULL);
        ok(Reader == INVALID_HANDLE_VALUE, "transacted reader opened past a non-transacted writer\n");
        ok(GetLastError() == ERROR_TRANSACTIONAL_CONFLICT, "transacted reader error %lu\n", GetLastError());
        if (Reader != INVALID_HANDLE_VALUE) CloseHandle(Reader);
    }
    if (Outsider != INVALID_HANDLE_VALUE) CloseHandle(Outsider);

    /* A transacted reader blocks non-transacted writers only while its handle is open */
    if (Transaction != INVALID_HANDLE_VALUE)
    {
        Reader = pCreateFileTransactedW(Data, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                                        NULL, Transaction, NULL, NULL);
        ok(Reader != INVALID_HANDLE_VALUE, "transacted reader open failed with %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Outsider = CreateFileW(Data, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        ok(Outsider == INVALID_HANDLE_VALUE, "outside writer opened past a transacted reader\n");
        ok(GetLastError() == ERROR_SHARING_VIOLATION, "outside writer error %lu\n", GetLastError());
        if (Outsider != INVALID_HANDLE_VALUE) CloseHandle(Outsider);
        if (Reader != INVALID_HANDLE_VALUE) CloseHandle(Reader);
        Outsider = CreateFileW(Data, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        ok(Outsider != INVALID_HANDLE_VALUE, "outside writer still blocked after the reader closed: %lu\n", GetLastError());
        if (Outsider != INVALID_HANDLE_VALUE) CloseHandle(Outsider);
        ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
        CloseHandle(Transaction);
    }

    /* A hard link appears at commit and is discarded by rollback */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pCreateHardLinkTransactedW(Hard, Data, NULL, Transaction), "CreateHardLinkTransactedW failed with %lu\n", GetLastError());
    ok(!Exists(Hard), "uncommitted hard link visible outside\n");
    ok(pGetFileAttributesTransactedW(Hard, GetFileExInfoStandard, &Info, Transaction),
       "transacted query of the link failed with %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(Hard), "rolled back hard link exists\n");
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pCreateHardLinkTransactedW(Hard, Data, NULL, Transaction), "CreateHardLinkTransactedW failed with %lu\n", GetLastError());
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Hard, "committed"), "committed hard link missing\n");

    /* The transaction sees its own namespace in enumeration, names and long paths */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Writer = pCreateFileTransactedW(New, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "CreateFileTransactedW failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        Length = GetFinalPathNameByHandleW(Writer, Buffer, RTL_NUMBER_OF(Buffer), FILE_NAME_OPENED | VOLUME_NAME_NONE);
        ok(Length >= 21 && !_wcsicmp(Buffer + Length - 21, L"\\TxfApiTest\\fresh.txt"),
           "transacted handle name %S\n", Buffer);
        CloseHandle(Writer);
    }
    ok(pDeleteFileTransactedW(Exist2, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(ListedInTransaction(Pattern, L"fresh.txt", Transaction), "created file not listed in its transaction\n");
    ok(!ListedInTransaction(Pattern, L"attr.txt", Transaction), "deleted file listed in its transaction\n");
    ok(ListedOutside(Pattern, L"attr.txt"), "transacted delete hid the file outside\n");
    ok(!ListedOutside(Pattern, L"fresh.txt"), "uncommitted file listed outside\n");
    Length = pGetLongPathNameTransactedW(New, Buffer, RTL_NUMBER_OF(Buffer), Transaction);
    ok(Length > 9 && !_wcsicmp(Buffer + Length - 9, L"fresh.txt"), "GetLongPathNameTransactedW gave %lu %S\n", Length, Buffer);
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(Exists(Exist2) && !Exists(New), "rollback did not restore the namespace\n");

    /* A directory created with its content appears whole at commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pCreateDirectoryTransactedW(NULL, Tree, NULL, Transaction), "CreateDirectoryTransactedW failed with %lu\n", GetLastError());
    Writer = pCreateFileTransactedW(Inner, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "CreateFileTransactedW in a transacted directory failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        ok(WriteThrough(Writer, "inner"), "write failed with %lu\n", GetLastError());
        CloseHandle(Writer);
    }
    ok(!Exists(Tree), "uncommitted directory visible outside\n");
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Inner, "inner"), "committed directory content missing\n");

    /* Deleting then recreating a name, and replacing a file by a rename, apply at commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(Again, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    Writer = pCreateFileTransactedW(Again, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "recreate in the transaction failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        ok(WriteThrough(Writer, "second"), "write failed with %lu\n", GetLastError());
        CloseHandle(Writer);
    }
    ok(ReadsBack(Again, "first"), "outside reader sees the recreated file\n");
    ok(pMoveFileTransactedW(Data, Target, NULL, NULL, MOVEFILE_REPLACE_EXISTING, Transaction),
       "MoveFileTransactedW with replace failed with %lu\n", GetLastError());
    ok(ReadsBack(Target, "target") && ReadsBack(Data, "committed"), "uncommitted replace visible outside\n");
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(Again, "second"), "recreated file not committed\n");
    ok(!Exists(Data) && ReadsBack(Target, "committed"), "committed replace not applied\n");
}

static
ULONG
CountListedInTransaction(
    _In_ PCWSTR Pattern,
    _In_ HANDLE Transaction)
{
    WIN32_FIND_DATAW Data;
    ULONG Count = 0;
    HANDLE Find;

    Find = pFindFirstFileTransactedW(Pattern, FindExInfoStandard, &Data, FindExSearchNameMatch, NULL, 0, Transaction);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (wcscmp(Data.cFileName, L".") && wcscmp(Data.cFileName, L".."))
            Count++;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);
    return Count;
}

static
BOOL
PrepareFullDirectory(
    _In_ PCWSTR Full,
    _In_ PCWSTR ChildA,
    _In_ PCWSTR ChildB)
{
    if (!Exists(Full) && !CreateDirectoryW(Full, NULL))
    {
        ok(0, "CreateDirectory(%S) failed with %lu\n", Full, GetLastError());
        return FALSE;
    }
    return WriteTestFile(ChildA, "a") && WriteTestFile(ChildB, "b");
}

static
VOID
TestTransactedDirectoryRemoval(
    _In_ PTX_DIR Dir)
{
    WCHAR Full[MAX_PATH], ChildA[MAX_PATH], ChildB[MAX_PATH], ChildC[MAX_PATH], Pattern[MAX_PATH], Outside[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Info;
    HANDLE Transaction;
    HANDLE Writer;

    PathOf(Dir, L"full", Full, Scratch);
    PathOf(Dir, L"full\\a.txt", ChildA, Scratch);
    PathOf(Dir, L"full\\b.txt", ChildB, Scratch);
    PathOf(Dir, L"full\\c.txt", ChildC, Scratch);
    PathOf(Dir, L"full\\*", Pattern, Scratch);
    PathOf(Dir, L"outside.txt", Outside, Scratch);
    if (!PrepareFullDirectory(Full, ChildA, ChildB))
        return;

    /* A directory whose committed children were all deleted in the transaction is empty inside it */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(ChildA, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(pDeleteFileTransactedW(ChildB, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(CountListedInTransaction(Pattern, Transaction) == 0, "deleted children still listed in the transaction\n");
    ok(pRemoveDirectoryTransactedW(Full, Transaction),
       "RemoveDirectoryTransactedW of a transactionally empty directory failed with %lu\n", GetLastError());
    ok(Exists(Full) && ReadsBack(ChildA, "a") && ReadsBack(ChildB, "b"),
       "transacted directory removal visible outside before commit\n");
    ok(!pGetFileAttributesTransactedW(Full, GetFileExInfoStandard, &Info, Transaction),
       "removed directory still found in the transaction\n");
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(!Exists(ChildA) && !Exists(ChildB) && !Exists(Full), "committed directory removal not applied\n");

    /* One remaining committed child keeps the directory from being removed */
    if (!PrepareFullDirectory(Full, ChildA, ChildB))
        return;
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(ChildA, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!pRemoveDirectoryTransactedW(Full, Transaction), "RemoveDirectoryTransactedW removed a non-empty directory\n");
    ok(GetLastError() == ERROR_DIR_NOT_EMPTY, "non-empty directory removal error %lu\n", GetLastError());

    /* A child moved out of the directory no longer counts */
    ok(pMoveFileTransactedW(ChildB, Outside, NULL, NULL, 0, Transaction), "MoveFileTransactedW failed with %lu\n", GetLastError());
    ok(pRemoveDirectoryTransactedW(Full, Transaction),
       "RemoveDirectoryTransactedW after moving the last child out failed with %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(ChildA, "a") && ReadsBack(ChildB, "b") && !Exists(Outside), "rollback did not restore the directory\n");

    /* A child created in the transaction, or a committed child it writes to, keeps the directory */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pDeleteFileTransactedW(ChildA, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    ok(pDeleteFileTransactedW(ChildB, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    Writer = pCreateFileTransactedW(ChildC, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "CreateFileTransactedW failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE) CloseHandle(Writer);
    SetLastError(0xdeadbeef);
    ok(!pRemoveDirectoryTransactedW(Full, Transaction), "RemoveDirectoryTransactedW removed a directory with a new child\n");
    ok(GetLastError() == ERROR_DIR_NOT_EMPTY, "new child: error %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    Writer = pCreateFileTransactedW(ChildA, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(Writer != INVALID_HANDLE_VALUE, "transacted writer open failed with %lu\n", GetLastError());
    if (Writer != INVALID_HANDLE_VALUE)
    {
        ok(WriteThrough(Writer, "aa"), "transacted write failed with %lu\n", GetLastError());
        CloseHandle(Writer);
    }
    ok(pDeleteFileTransactedW(ChildB, Transaction), "DeleteFileTransactedW failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!pRemoveDirectoryTransactedW(Full, Transaction), "RemoveDirectoryTransactedW removed a directory with a written child\n");
    ok(GetLastError() == ERROR_DIR_NOT_EMPTY, "written child: error %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(ChildA, "a") && ReadsBack(ChildB, "b") && !Exists(ChildC), "rollback did not restore the children\n");
}

static
VOID
CheckNormalizedName(
    _In_ HANDLE File,
    _In_ PCWSTR Directory,
    _In_ PCWSTR Leaf,
    _In_ PCSTR What)
{
    WCHAR Expected[MAX_PATH], Buffer[MAX_PATH];
    UCHAR Info[sizeof(FILE_NAME_INFO) + MAX_PATH * sizeof(WCHAR)];
    PFILE_NAME_INFO NameInfo = (PFILE_NAME_INFO)Info;
    DWORD Length;

    StringCchPrintfW(Expected, RTL_NUMBER_OF(Expected), L"%s\\%s", Directory, Leaf);
    Length = GetFinalPathNameByHandleW(File, Buffer, RTL_NUMBER_OF(Buffer), FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
    ok(Length == wcslen(Expected) && !wcscmp(Buffer, Expected), "%s: normalized name %lu %S, expected %S\n",
       What, Length, Length < RTL_NUMBER_OF(Buffer) ? Buffer : L"", Expected);
    ZeroMemory(Info, sizeof(Info));
    ok(GetFileInformationByHandleEx(File, FileNormalizedNameInfo, Info, sizeof(Info)),
       "%s: FileNormalizedNameInfo failed with %lu\n", What, GetLastError());
    StringCchCopyNW(Buffer, RTL_NUMBER_OF(Buffer), NameInfo->FileName,
                    min(NameInfo->FileNameLength / sizeof(WCHAR), RTL_NUMBER_OF(Buffer) - 1));
    ok(NameInfo->FileNameLength == wcslen(Expected) * sizeof(WCHAR) && !wcscmp(Buffer, Expected),
       "%s: FileNormalizedNameInfo gave %S\n", What, Buffer);
}

static
VOID
TestTransactedNormalizedName(
    _In_ PTX_DIR Dir)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH], Moved[MAX_PATH], Buffer[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    HANDLE Transaction;
    HANDLE File;
    DWORD Length;

    File = CreateFileW(Dir->Dir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "directory open failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Length = GetFinalPathNameByHandleW(File, Directory, RTL_NUMBER_OF(Directory), FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
    CloseHandle(File);
    ok(Length != 0 && Length < RTL_NUMBER_OF(Directory), "directory normalized name failed with %lu\n", GetLastError());
    PathOf(Dir, L"norm2.txt", Path, Scratch);
    if (Length == 0 || Length >= RTL_NUMBER_OF(Directory) || !WriteTestFile(Path, "n2"))
        return;
    PathOf(Dir, L"norm3.txt", Path, Scratch);
    if (!WriteTestFile(Path, "n3"))
        return;

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;

    /* A file created in the transaction has its normalized name in the transaction's view */
    PathOf(Dir, L"Norm.txt", Path, Scratch);
    File = pCreateFileTransactedW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileTransactedW failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        CheckNormalizedName(File, Directory, L"Norm.txt", "created file");
        CloseHandle(File);
    }

    /* A committed file renamed in the transaction takes its new name */
    PathOf(Dir, L"norm2.txt", Path, Scratch);
    PathOf(Dir, L"Renamed.txt", Moved, Scratch);
    ok(pMoveFileTransactedW(Path, Moved, NULL, NULL, 0, Transaction), "MoveFileTransactedW failed with %lu\n", GetLastError());
    PathOf(Dir, L"RENAMED.TXT", Path, Scratch);
    File = pCreateFileTransactedW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "transacted open of the renamed file failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        CheckNormalizedName(File, Directory, L"Renamed.txt", "renamed file");
        Length = GetFinalPathNameByHandleW(File, Buffer, RTL_NUMBER_OF(Buffer), FILE_NAME_OPENED | VOLUME_NAME_NONE);
        ok(Length >= 12 && !wcscmp(Buffer + Length - 12, L"\\RENAMED.TXT"), "opened name %S\n", Buffer);
        CloseHandle(File);
    }

    /* A committed file opened by another case, by a reader and by a writer of the transaction */
    PathOf(Dir, L"NORM3.TXT", Path, Scratch);
    File = pCreateFileTransactedW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "transacted reader open failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        CheckNormalizedName(File, Directory, L"norm3.txt", "transacted reader");
        CloseHandle(File);
    }
    File = pCreateFileTransactedW(Path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "transacted writer open failed with %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        CheckNormalizedName(File, Directory, L"norm3.txt", "transacted writer");
        CloseHandle(File);
    }

    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
}

static
BOOL
ReadsBackTransacted(
    _In_ PCWSTR Path,
    _In_ PCSTR Expected,
    _In_ HANDLE Transaction)
{
    CHAR Buffer[16] = { 0 };
    HANDLE File;
    DWORD Read = 0;

    File = pCreateFileTransactedW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL);
    CloseHandle(File);
    return Read == strlen(Expected) && !memcmp(Buffer, Expected, Read);
}

static
BOOL
WriteTransacted(
    _In_ PCWSTR Path,
    _In_ DWORD Disposition,
    _In_ PCSTR Data,
    _In_ HANDLE Transaction)
{
    HANDLE File;
    BOOL Result;

    File = pCreateFileTransactedW(Path, GENERIC_WRITE, 0, NULL, Disposition, 0, NULL, Transaction, NULL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteThrough(File, Data);
    CloseHandle(File);
    return Result;
}

static
ULONG
ListedStreams(
    _In_ PCWSTR Path,
    _In_ HANDLE Transaction,
    _Out_writes_(Size) PWSTR Names,
    _In_ ULONG Size)
{
    WIN32_FIND_STREAM_DATA Data;
    ULONG Count = 0;
    HANDLE Find;

    Names[0] = UNICODE_NULL;
    Find = pFindFirstStreamTransactedW(Path, FindStreamInfoStandard, &Data, 0, Transaction);
    if (Find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        StringCchCatW(Names, Size, Data.cStreamName);
        Count++;
    } while (FindNextStreamW(Find, &Data));
    FindClose(Find);
    return Count;
}

static
VOID
TestTransactedNamedStreams(
    _In_ PTX_DIR Dir)
{
    WCHAR File[MAX_PATH], S1[MAX_PATH], S2[MAX_PATH], S3[MAX_PATH], Names[4 * MAX_PATH];
    UCHAR RenameBuffer[sizeof(FILE_RENAME_INFO) + 16 * sizeof(WCHAR)];
    PFILE_RENAME_INFO Rename = (PFILE_RENAME_INFO)RenameBuffer;
    CHAR Scratch[MAX_PATH];
    HANDLE Transaction;
    HANDLE Handle;

    PathOf(Dir, L"streams.txt", File, Scratch);
    PathOf(Dir, L"streams.txt:s1", S1, Scratch);
    PathOf(Dir, L"streams.txt:s2", S2, Scratch);
    PathOf(Dir, L"streams.txt:s3", S3, Scratch);
    if (!WriteTestFile(File, "base") || !WriteTestFile(S1, "one") || !WriteTestFile(S2, "two"))
        return;

    /* Named streams are transacted: a writer, a new stream and a stream delete stay private until commit */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(WriteTransacted(S1, OPEN_EXISTING, "uno", Transaction), "transacted stream writer failed with %lu\n", GetLastError());
    ok(ReadsBack(S1, "one"), "outside reader sees the uncommitted stream data\n");
    SetLastError(0xdeadbeef);
    Handle = CreateFileW(S2, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Handle == INVALID_HANDLE_VALUE, "outside writer opened another stream of a transactionally locked file\n");
    ok(GetLastError() == ERROR_SHARING_VIOLATION, "outside stream writer error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(ReadsBackTransacted(S1, "uno", Transaction), "transaction does not see its stream data\n");
    ok(ReadsBackTransacted(S2, "two", Transaction), "transaction lost an untouched stream\n");
    ok(ReadsBackTransacted(File, "base", Transaction), "transaction lost the unnamed stream\n");
    ok(WriteTransacted(S3, CREATE_NEW, "three", Transaction), "transacted stream create failed with %lu\n", GetLastError());
    ok(!ReadsBack(S3, "three"), "uncommitted stream visible outside\n");
    ok(pDeleteFileTransactedW(S2, Transaction), "transacted stream delete failed with %lu\n", GetLastError());
    ok(ReadsBack(S2, "two"), "transacted stream delete visible outside\n");
    ok(!ReadsBackTransacted(S2, "two", Transaction), "deleted stream still readable in the transaction\n");
    ok(ListedStreams(File, Transaction, Names, RTL_NUMBER_OF(Names)) == 3 && wcsstr(Names, L"::$DATA") &&
       wcsstr(Names, L":s1:$DATA") && wcsstr(Names, L":s3:$DATA") && !wcsstr(Names, L":s2:"),
       "transacted stream list %S\n", Names);

    /* A named stream cannot be renamed inside a transaction */
    Handle = pCreateFileTransactedW(S1, DELETE | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL, Transaction, NULL, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "transacted stream open for rename failed with %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(RenameBuffer, sizeof(RenameBuffer));
        Rename->FileNameLength = 3 * sizeof(WCHAR);
        CopyMemory(Rename->FileName, L":s4", 3 * sizeof(WCHAR));
        ok(!SetFileInformationByHandle(Handle, FileRenameInfo, Rename, sizeof(RenameBuffer)),
           "a named stream was renamed inside a transaction\n");
        CloseHandle(Handle);
    }

    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(S1, "uno"), "committed stream data missing\n");
    ok(ReadsBack(S3, "three"), "committed new stream missing\n");
    ok(!ReadsBack(S2, "two") && GetLastError() == ERROR_FILE_NOT_FOUND, "committed stream delete not applied (%lu)\n", GetLastError());
    ok(ReadsBack(File, "base"), "commit changed the unnamed stream\n");

    /* Rollback drops stream changes */
    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(WriteTransacted(S1, OPEN_EXISTING, "xx", Transaction), "transacted stream writer failed with %lu\n", GetLastError());
    ok(pDeleteFileTransactedW(S3, Transaction), "transacted stream delete failed with %lu\n", GetLastError());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
    ok(ReadsBack(S1, "uno") && ReadsBack(S3, "three"), "rollback did not restore the streams\n");
}

static
VOID
TestInactiveTransaction(
    _In_ PTX_DIR Dir)
{
    WCHAR Exist[MAX_PATH], Copy[MAX_PATH], New[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    HANDLE Transaction;
    HANDLE Handle;
    DWORD Error;
    BOOL Result;

    PathOf(Dir, L"exist.txt", Exist, Scratch);
    PathOf(Dir, L"copy.txt", Copy, Scratch);
    PathOf(Dir, L"new.txt", New, Scratch);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Result = pCopyFileTransactedW(Exist, Copy, NULL, NULL, NULL, 0, Transaction);
    Error = GetLastError();
    ok(!Result, "CopyFileTransactedW succeeded with a rolled back transaction\n");
    ok(Error == ERROR_TRANSACTION_NOT_ACTIVE || Error == ERROR_INVALID_TRANSACTION,
       "CopyFileTransactedW with a rolled back transaction: error %lu\n", Error);
    ok(!Exists(Copy), "copy with a rolled back transaction created %S\n", Copy);
    CloseHandle(Transaction);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    ok(pCommitTransaction(Transaction), "CommitTransaction failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(New, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL, Transaction, NULL, NULL);
    Error = GetLastError();
    ok(Handle == INVALID_HANDLE_VALUE, "CreateFileTransactedW succeeded with a committed transaction\n");
    ok(Error == ERROR_TRANSACTION_NOT_ACTIVE || Error == ERROR_INVALID_TRANSACTION,
       "CreateFileTransactedW with a committed transaction: error %lu\n", Error);
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    ok(!Exists(New), "create with a committed transaction created %S\n", New);
    CloseHandle(Transaction);
}

static
VOID
TestInvalidTransaction(
    _In_ PTX_DIR Dir)
{
    WCHAR Exist[MAX_PATH];
    CHAR Scratch[MAX_PATH];
    HANDLE Event;
    HANDLE Handle;

    PathOf(Dir, L"exist.txt", Exist, Scratch);

    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(Exist, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL,
                                    (HANDLE)(ULONG_PTR)0xDEAD0, NULL, NULL);
    ok(Handle == INVALID_HANDLE_VALUE, "CreateFileTransactedW succeeded with a bogus transaction\n");
    ok(GetLastError() == ERROR_CURRENT_TRANSACTION_NOT_VALID, "bogus transaction: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ok(!pDeleteFileTransactedW(Exist, Event), "DeleteFileTransactedW succeeded with an event handle\n");
    ok(GetLastError() == ERROR_CURRENT_TRANSACTION_NOT_VALID, "event as transaction: error %lu\n", GetLastError());
    ok(Exists(Exist), "delete with an event handle removed %S\n", Exist);
    CloseHandle(Event);
    ok(pRtlGetCurrentTransaction() == NULL, "thread transaction %p after the calls\n", pRtlGetCurrentTransaction());
}

static
VOID
TestParameters(
    _In_ PTX_DIR Dir)
{
    WCHAR Exist[MAX_PATH], Moved[MAX_PATH], Buffer[MAX_PATH];
    CHAR ExistA[MAX_PATH], MovedA[MAX_PATH], Scratch[MAX_PATH];
    WIN32_FIND_STREAM_DATA StreamData;
    HANDLE Transaction;
    HANDLE Ambient;
    HANDLE Handle;
    DWORD Length;

    PathOf(Dir, L"exist.txt", Exist, ExistA);
    PathOf(Dir, L"moved.txt", Moved, Scratch);
    PathOf(Dir, L"movedA.txt", Buffer, MovedA);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(Exist, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL,
                                    Transaction, NULL, (PVOID)(ULONG_PTR)1);
    ok(Handle == INVALID_HANDLE_VALUE, "CreateFileTransactedW accepted an extended parameter\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "extended parameter: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedA(ExistA, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL,
                                    Transaction, NULL, (PVOID)(ULONG_PTR)1);
    ok(Handle == INVALID_HANDLE_VALUE, "CreateFileTransactedA accepted an extended parameter\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "extended parameter: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);

    SetLastError(0xdeadbeef);
    ok(!pMoveFileTransactedW(Exist, Moved, NULL, NULL, MOVEFILE_FAIL_IF_NOT_TRACKABLE, Transaction),
       "MoveFileTransactedW accepted MOVEFILE_FAIL_IF_NOT_TRACKABLE\n");
    ok(GetLastError() == ERROR_NO_LINK_TRACKING_IN_TRANSACTION, "trackable move: error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!pMoveFileTransactedA(ExistA, MovedA, NULL, NULL, MOVEFILE_FAIL_IF_NOT_TRACKABLE, Transaction),
       "MoveFileTransactedA accepted MOVEFILE_FAIL_IF_NOT_TRACKABLE\n");
    ok(GetLastError() == ERROR_NO_LINK_TRACKING_IN_TRANSACTION, "trackable move: error %lu\n", GetLastError());
    ok(Exists(Exist) && !Exists(Moved), "refused move changed the namespace\n");

    SetLastError(0xdeadbeef);
    Handle = pFindFirstStreamTransactedW(Exist, FindStreamInfoStandard, &StreamData, 1, Transaction);
    ok(Handle == INVALID_HANDLE_VALUE, "FindFirstStreamTransactedW accepted nonzero flags\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "stream flags: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);

    SetLastError(0xdeadbeef);
    Length = RTL_NUMBER_OF(Buffer);
    Handle = pFindFirstFileNameTransactedW(Exist, 1, &Length, Buffer, Transaction);
    ok(Handle == INVALID_HANDLE_VALUE, "FindFirstFileNameTransactedW accepted nonzero flags\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "file name flags: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) FindClose(Handle);

    /* The thread's own current transaction comes back after a transacted call */
    Ambient = NewTransaction();
    if (Ambient != INVALID_HANDLE_VALUE)
    {
        pRtlSetCurrentTransaction(Ambient);
        Length = pGetFullPathNameTransactedW(Exist, RTL_NUMBER_OF(Buffer), Buffer, NULL, Transaction);
        ok(Length != 0, "GetFullPathNameTransactedW failed with %lu\n", GetLastError());
        ok(pRtlGetCurrentTransaction() == Ambient, "thread transaction %p, expected %p\n",
           pRtlGetCurrentTransaction(), Ambient);
        pRtlSetCurrentTransaction(NULL);
        pRollbackTransaction(Ambient);
        CloseHandle(Ambient);
    }

    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
}

static
VOID
TestFullPathName(
    _In_ PTX_DIR Dir)
{
    WCHAR Expected[MAX_PATH], Actual[MAX_PATH], Name[MAX_PATH];
    CHAR ExpectedA[MAX_PATH], ActualA[MAX_PATH], NameA[MAX_PATH];
    PWSTR ExpectedPart, ActualPart;
    PSTR ExpectedPartA, ActualPartA;
    HANDLE Transaction;
    DWORD Want, Got;

    PathOf(Dir, L"sub\\..\\exist.txt", Name, NameA);

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;

    Want = GetFullPathNameW(Name, RTL_NUMBER_OF(Expected), Expected, &ExpectedPart);
    Got = pGetFullPathNameTransactedW(Name, RTL_NUMBER_OF(Actual), Actual, &ActualPart, Transaction);
    ok(Got == Want && Got != 0, "GetFullPathNameTransactedW returned %lu, expected %lu\n", Got, Want);
    ok(!wcscmp(Actual, Expected), "GetFullPathNameTransactedW gave %S, expected %S\n", Actual, Expected);
    ok(ActualPart - Actual == ExpectedPart - Expected, "file part at %Id, expected %Id\n",
       ActualPart - Actual, ExpectedPart - Expected);
    Want = GetFullPathNameW(Name, 4, Expected, NULL);
    Got = pGetFullPathNameTransactedW(Name, 4, Actual, NULL, Transaction);
    ok(Got == Want && Got > 4, "short buffer returned %lu, expected %lu\n", Got, Want);

    Want = GetFullPathNameA(NameA, RTL_NUMBER_OF(ExpectedA), ExpectedA, &ExpectedPartA);
    Got = pGetFullPathNameTransactedA(NameA, RTL_NUMBER_OF(ActualA), ActualA, &ActualPartA, Transaction);
    ok(Got == Want && Got != 0, "GetFullPathNameTransactedA returned %lu, expected %lu\n", Got, Want);
    ok(!strcmp(ActualA, ExpectedA), "GetFullPathNameTransactedA gave %s, expected %s\n", ActualA, ExpectedA);
    ok(ActualPartA - ActualA == ExpectedPartA - ExpectedA, "file part at %Id, expected %Id\n",
       ActualPartA - ActualA, ExpectedPartA - ExpectedA);
    Want = GetFullPathNameA(NameA, 4, ExpectedA, NULL);
    Got = pGetFullPathNameTransactedA(NameA, 4, ActualA, NULL, Transaction);
    ok(Got == Want && Got > 4, "short buffer returned %lu, expected %lu\n", Got, Want);

    ok(pRtlGetCurrentTransaction() == NULL, "thread transaction %p after the calls\n", pRtlGetCurrentTransaction());
    ok(pRollbackTransaction(Transaction), "RollbackTransaction failed with %lu\n", GetLastError());
    CloseHandle(Transaction);
}

static
VOID
TestVolume(
    _In_ PTX_DIR Dir)
{
    WCHAR FileSystem[32];
    BOOL Supported;

    if (!PrepareTree(Dir))
    {
        RemoveTree(Dir);
        return;
    }

    Supported = VolumeSupportsTransactions(Dir->Dir, FileSystem);
    trace("%S on %S: transactions %s\n", Dir->Dir, FileSystem, Supported ? "supported" : "not supported");

    TestParameters(Dir);
    TestFullPathName(Dir);
    TestInvalidTransaction(Dir);
    TestInactiveTransaction(Dir);
    if (Supported)
    {
        TestSupportedVolume(Dir);
        TestSupportedVolumeData(Dir);
        TestTransactedDirectoryRemoval(Dir);
        TestTransactedNormalizedName(Dir);
        TestTransactedNamedStreams(Dir);
    }
    else
        TestUnsupportedVolume(Dir);

    RemoveTree(Dir);
}

static
VOID
TestRemote(VOID)
{
    static const WCHAR Remote[] = L"\\\\localhost\\C$\\Windows\\win.ini";
    HANDLE Transaction;
    HANDLE Handle;

    if (GetFileAttributesW(Remote) == INVALID_FILE_ATTRIBUTES)
    {
        skip("no reachable remote file (%lu)\n", GetLastError());
        return;
    }

    Transaction = NewTransaction();
    if (Transaction == INVALID_HANDLE_VALUE)
        return;
    SetLastError(0xdeadbeef);
    Handle = pCreateFileTransactedW(Remote, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL,
                                    Transaction, NULL, NULL);
    ok(Handle == INVALID_HANDLE_VALUE, "CreateFileTransactedW opened a remote file\n");
    ok(GetLastError() == ERROR_TRANSACTIONS_UNSUPPORTED_REMOTE, "remote file: error %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    pRollbackTransaction(Transaction);
    CloseHandle(Transaction);
}

static
BOOL
FindFatVolume(
    _Out_ PTX_DIR Dir)
{
    WCHAR Root[4] = L"C:\\";
    WCHAR FileSystem[32];
    DWORD Drives = GetLogicalDrives();
    WCHAR Letter;
    UINT Type;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        if (!(Drives & (1u << (Letter - L'A'))))
            continue;
        Root[0] = Letter;
        Type = GetDriveTypeW(Root);
        if (Type != DRIVE_FIXED && Type != DRIVE_REMOVABLE)
            continue;
        if (!GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, RTL_NUMBER_OF(FileSystem)))
            continue;
        if (_wcsnicmp(FileSystem, L"FAT", 3))
            continue;
        StringCchPrintfW(Dir->Dir, RTL_NUMBER_OF(Dir->Dir), L"%c:\\TxfApiTest", Letter);
        StringCchPrintfA(Dir->DirA, RTL_NUMBER_OF(Dir->DirA), "%c:\\TxfApiTest", (CHAR)Letter);
        return TRUE;
    }
    return FALSE;
}

START_TEST(TransactedFileApis)
{
    TX_DIR Dir;
    WCHAR Temp[MAX_PATH];
    CHAR TempA[MAX_PATH];

    if (!LoadFunctions())
    {
        skip("transacted file functions not exported\n");
        return;
    }
    if (!pCreateTransaction || !pCommitTransaction || !pRollbackTransaction ||
        !pRtlGetCurrentTransaction || !pRtlSetCurrentTransaction)
    {
        skip("ktmw32 or the thread transaction functions are missing\n");
        return;
    }

    GetTempPathW(RTL_NUMBER_OF(Temp), Temp);
    GetTempPathA(RTL_NUMBER_OF(TempA), TempA);
    StringCchPrintfW(Dir.Dir, RTL_NUMBER_OF(Dir.Dir), L"%sTxfApiTest", Temp);
    StringCchPrintfA(Dir.DirA, RTL_NUMBER_OF(Dir.DirA), "%sTxfApiTest", TempA);
    TestVolume(&Dir);

    if (FindFatVolume(&Dir))
        TestVolume(&Dir);
    else
        skip("no FAT volume to test\n");

    TestRemote();
}
