/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the search of reg query /f and its scope, match and type switches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "reg_test.h"

#define FIND_KEY KEY_WINE "\\reg_find"
#define FIND_PATH "HKEY_CURRENT_USER\\" FIND_KEY

static BOOL
run_find(const char *switches, char *out, DWORD out_size, DWORD *exit_code)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE read_pipe, write_pipe;
    char cmd[512];
    DWORD total = 0, got;

    out[0] = 0;
    sprintf(cmd, "reg query HKCU\\" FIND_KEY " %s", switches);
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
        return FALSE;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return FALSE;
    }
    CloseHandle(write_pipe);
    while (total < out_size - 1 && ReadFile(read_pipe, out + total, out_size - 1 - total, &got, NULL) && got)
        total += got;
    out[total] = 0;
    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, exit_code);
    CloseHandle(read_pipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

#define check_find(s,e,c) check_find_(__FILE__,__LINE__,s,e,c)
static void
check_find_(const char *file, unsigned line, const char *switches, const char *expected, DWORD expected_code)
{
    char out[4096];
    DWORD code = 0xdeadbeef;

    lok(run_find(switches, out, sizeof(out), &code), "reg query %s did not run\n", switches);
    lok(code == expected_code, "reg query %s: exit code %lu, expected %lu\n", switches, code, expected_code);
    lok(!strcmp(out, expected), "reg query %s: output\n%s\nexpected\n%s\n", switches, out, expected);
}

START_TEST(queryfind)
{
    static const char multi[] = "one\0Needle two\0";
    static const BYTE binary[] = { 0x0A, 0x0B, 0x5C };
    DWORD dword;
    ULONGLONG qword = 300;
    HKEY hkey, subkey;

    delete_tree(HKEY_CURRENT_USER, FIND_KEY, 0);
    add_key(HKEY_CURRENT_USER, FIND_KEY, 0, &hkey);
    add_value(hkey, "Alpha", REG_SZ, "has needle data", 16);
    dword = 5;
    add_value(hkey, "NeedleName", REG_DWORD, &dword, sizeof(dword));
    add_value(hkey, "Beta", REG_MULTI_SZ, multi, sizeof(multi));
    add_value(hkey, "Bin", REG_BINARY, binary, sizeof(binary));
    dword = 26;
    add_value(hkey, "D", REG_DWORD, &dword, sizeof(dword));
    add_value(hkey, "Q", REG_QWORD, &qword, sizeof(qword));
    add_key(hkey, "NeedleKey", 0, &subkey);
    add_value(subkey, "Other", REG_SZ, "plain", 6);
    close_key(subkey);
    add_key(hkey, "Sub\\Deep", 0, &subkey);
    add_value(subkey, "X", REG_SZ, "Needle", 7);
    close_key(subkey);
    close_key(hkey);

    check_find("/f needle",
               "\r\n" FIND_PATH "\r\n"
               "    Alpha    REG_SZ    has needle data\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "    Beta    REG_MULTI_SZ    one\\0Needle two\r\n"
               "\r\n" FIND_PATH "\\NeedleKey\r\n"
               "End of search: 4 match(es) found.\r\n", 0);
    check_find("/f needle /s",
               "\r\n" FIND_PATH "\r\n"
               "    Alpha    REG_SZ    has needle data\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "    Beta    REG_MULTI_SZ    one\\0Needle two\r\n"
               "\r\n" FIND_PATH "\\NeedleKey\r\n"
               "\r\n" FIND_PATH "\\Sub\\Deep\r\n"
               "    X    REG_SZ    Needle\r\n"
               "\r\nEnd of search: 5 match(es) found.\r\n", 0);
    check_find("/f needle /k /s",
               "\r\n" FIND_PATH "\\NeedleKey\r\n"
               "End of search: 1 match(es) found.\r\n", 0);
    check_find("/f needle /d /s",
               "\r\n" FIND_PATH "\r\n"
               "    Alpha    REG_SZ    has needle data\r\n"
               "    Beta    REG_MULTI_SZ    one\\0Needle two\r\n"
               "\r\n" FIND_PATH "\\Sub\\Deep\r\n"
               "    X    REG_SZ    Needle\r\n"
               "\r\nEnd of search: 3 match(es) found.\r\n", 0);
    check_find("/f needle /c /s",
               "\r\n" FIND_PATH "\r\n"
               "    Alpha    REG_SZ    has needle data\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f Needle /e /s",
               "\r\n" FIND_PATH "\\Sub\\Deep\r\n"
               "    X    REG_SZ    Needle\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f needle /t REG_DWORD /s",
               "\r\n" FIND_PATH "\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f needle /v",
               "\r\n" FIND_PATH "\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f 2 /d",
               "\r\n" FIND_PATH "\r\n"
               "    D    REG_DWORD    0x1a\r\n"
               "    Q    REG_QWORD    0x12c\r\n"
               "\r\nEnd of search: 2 match(es) found.\r\n", 0);
    check_find("/f 0x1a /d /e",
               "\r\n" FIND_PATH "\r\n"
               "    D    REG_DWORD    0x1a\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f 0B5C /d",
               "\r\n" FIND_PATH "\r\n"
               "    Bin    REG_BINARY    0A0B5C\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);
    check_find("/f 1a /d", "\r\nEnd of search: 0 match(es) found.\r\n", 1);
    check_find("/f reg_find /k /s", "\r\nEnd of search: 0 match(es) found.\r\n", 1);
    check_find("/f needle /s /se #",
               "\r\n" FIND_PATH "\r\n"
               "    Alpha    REG_SZ    has needle data\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "    Beta    REG_MULTI_SZ    one#Needle two\r\n"
               "\r\n" FIND_PATH "\\NeedleKey\r\n"
               "\r\n" FIND_PATH "\\Sub\\Deep\r\n"
               "    X    REG_SZ    Needle\r\n"
               "\r\nEnd of search: 5 match(es) found.\r\n", 0);
    check_find("/v NeedleName /s",
               "\r\n" FIND_PATH "\r\n"
               "    NeedleName    REG_DWORD    0x5\r\n"
               "\r\nEnd of search: 1 match(es) found.\r\n", 0);

    delete_tree(HKEY_CURRENT_USER, FIND_KEY, 0);
}
