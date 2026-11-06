/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test cmd.exe batch scripts: echo-on display, %* and syntax errors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>

static char s_dir[MAX_PATH];

static void write_file(const char *name, const char *content)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD written;

    StringCbPrintfA(path, sizeof(path), "%s\\%s", s_dir, name);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFileA(%s) failed with %lu\n", path, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, content, lstrlenA(content), &written, NULL);
    CloseHandle(file);
}

static void delete_file(const char *name)
{
    char path[MAX_PATH];

    StringCbPrintfA(path, sizeof(path), "%s\\%s", s_dir, name);
    DeleteFileA(path);
}

static void check_file(int line, const char *name, const char *expected)
{
    char path[MAX_PATH], buffer[256];
    HANDLE file;
    DWORD got = 0;

    StringCbPrintfA(path, sizeof(path), "%s\\%s", s_dir, name);
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok_(__FILE__, line)(file != INVALID_HANDLE_VALUE, "%s not created, error %lu\n", name, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    ReadFile(file, buffer, sizeof(buffer) - 1, &got, NULL);
    buffer[got] = 0;
    CloseHandle(file);
    DeleteFileA(path);
    ok_(__FILE__, line)(!strcmp(buffer, expected), "%s holds '%s', expected '%s'\n", name, buffer, expected);
}

static BOOL run_cmd(const char *cmdline, const char *input, DWORD *exit_code, char *output, DWORD size)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE read_pipe, write_pipe, in, err;
    char buffer[512], path[MAX_PATH];
    DWORD got, total = 0;
    BOOL ret;

    if (input)
    {
        write_file("input.txt", input);
        StringCbPrintfA(path, sizeof(path), "%s\\input.txt", s_dir);
        in = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
    }
    else
        in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    err = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
        return FALSE;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = write_pipe;
    si.hStdError = err;
    lstrcpynA(buffer, cmdline, sizeof(buffer));
    ret = CreateProcessA(NULL, buffer, NULL, NULL, TRUE, 0, NULL, s_dir, &si, &pi);
    CloseHandle(write_pipe);
    CloseHandle(in);
    CloseHandle(err);
    if (!ret)
    {
        CloseHandle(read_pipe);
        return FALSE;
    }

    while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, NULL) && got)
    {
        if (total + got >= size)
            got = size - 1 - total;
        memcpy(output + total, buffer, got);
        total += got;
    }
    output[total] = 0;
    CloseHandle(read_pipe);

    ok(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0, "%s did not exit\n", cmdline);
    GetExitCodeProcess(pi.hProcess, exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (input)
        delete_file("input.txt");
    return TRUE;
}

static void check_cmd(int line, const char *cmdline, const char *input, DWORD expected_code,
                      const char *expected_output, BOOL exact)
{
    static char output[4096];
    DWORD exit_code = 0xdeadbeef;

    if (!run_cmd(cmdline, input, &exit_code, output, sizeof(output)))
    {
        ok_(__FILE__, line)(FALSE, "Running %s failed with %lu\n", cmdline, GetLastError());
        return;
    }
    ok_(__FILE__, line)(exit_code == expected_code, "%s: exit code %lu, expected %lu\n",
                        cmdline, exit_code, expected_code);
    if (exact)
        ok_(__FILE__, line)(!strcmp(output, expected_output), "%s: output '%s', expected '%s'\n",
                            cmdline, output, expected_output);
    else
        ok_(__FILE__, line)(strstr(output, expected_output) != NULL, "%s: output '%s' lacks '%s'\n",
                            cmdline, output, expected_output);
}

static void test_echo_on_display(void)
{
    write_file("echoon.cmd",
        "@prompt X$G\r\n"
        "@echo on\r\n"
        "echo a & echo b\r\n"
        "echo c | sort\r\n"
        "dir nonexist >nul 2>&1\r\n"
        "if 1==1 (echo d) else (echo e)\r\n"
        "if 1==1 (echo f)\r\n"
        "(echo g) > nul\r\n"
        "iF /I nOt 1==2 echo h\r\n"
        "echo i 2>nul >nul\r\n"
        "echo j || echo k\r\n"
        "for %%a in (1) do (echo %%a)\r\n"
        "@echo off\r\n");
    check_cmd(__LINE__, "cmd /c echoon.cmd", NULL, 0,
        "\r\n"
        "X>echo a   & echo b \r\n"
        "a \r\n"
        "b\r\n"
        "\r\n"
        "X>echo c   | sort\r\n"
        "c \r\n"
        "\r\n"
        "X>dir nonexist  1>nul 2>&1 \r\n"
        "\r\n"
        "X>if 1 == 1 (echo d )  else (echo e ) \r\n"
        "d\r\n"
        "\r\n"
        "X>if 1 == 1 (echo f ) \r\n"
        "f\r\n"
        "\r\n"
        "X>(echo g ) 1>nul \r\n"
        "\r\n"
        "X>iF /I nOt 1 == 2 echo h \r\n"
        "h\r\n"
        "\r\n"
        "X>echo i  2>nul 1>nul \r\n"
        "\r\n"
        "X>echo j   || echo k \r\n"
        "j \r\n"
        "\r\n"
        "X>for %a in (1) do (echo %a ) \r\n"
        "\r\n"
        "X>(echo 1 ) \r\n"
        "1\r\n", TRUE);
    delete_file("echoon.cmd");
}

static void test_echo_on_redirections_and_keywords(void)
{
    write_file("echoon2.cmd",
        "@prompt X$G\r\n"
        "@echo on\r\n"
        "echo a >x1.txt b\r\n"
        "echo a >x2.txt   b\r\n"
        "echo a 2>nul b\r\n"
        "echo a>x3.txt& rem\r\n"
        "echo a >x4.txt \r\n"
        "If Exist echoon2.cmd echo e1\r\n"
        "iF DeFiNeD PROMPT echo e2\r\n"
        "IF ErrorLevel 0 echo e3\r\n"
        "if a equ a echo e4\r\n"
        "if 1 Lss 2 echo e5\r\n"
        "if a==a (echo e6) ELSE (echo e7)\r\n"
        "@setlocal enabledelayedexpansion\r\n"
        "@set x=1\r\n"
        "echo !x!\r\n"
        "(echo !x!)\r\n"
        "for %%a in (1) do @echo %%a\r\n"
        "for %%a in (1) do (echo %%a & echo b)\r\n"
        "sort < nul\r\n"
        "sort <nul >nul\r\n"
        "@echo off\r\n");
    check_cmd(__LINE__, "cmd /c echoon2.cmd", NULL, 0,
        "\r\n"
        "X>echo a  b 1>x1.txt \r\n"
        "\r\n"
        "X>echo a    b 1>x2.txt \r\n"
        "\r\n"
        "X>echo a  b 2>nul \r\n"
        "a  b\r\n"
        "\r\n"
        "X>echo a 1>x3.txt  & rem\r\n"
        "\r\n"
        "X>echo a   1>x4.txt \r\n"
        "\r\n"
        "X>If Exist echoon2.cmd echo e1 \r\n"
        "e1\r\n"
        "\r\n"
        "X>iF DeFiNeD PROMPT echo e2 \r\n"
        "e2\r\n"
        "\r\n"
        "X>IF ErrorLevel 0 echo e3 \r\n"
        "e3\r\n"
        "\r\n"
        "X>if a EQU a echo e4 \r\n"
        "e4\r\n"
        "\r\n"
        "X>if 1 LSS 2 echo e5 \r\n"
        "e5\r\n"
        "\r\n"
        "X>if a == a (echo e6 )  ELSE (echo e7 ) \r\n"
        "e6\r\n"
        "\r\n"
        "X>echo !x! \r\n"
        "1\r\n"
        "\r\n"
        "X>(echo !x! ) \r\n"
        "1\r\n"
        "\r\n"
        "X>for %a in (1) do @echo %a \r\n"
        "1\r\n"
        "\r\n"
        "X>for %a in (1) do (echo %a   & echo b ) \r\n"
        "\r\n"
        "X>(echo 1   & echo b ) \r\n"
        "1 \r\n"
        "b\r\n"
        "\r\n"
        "X>sort  0<nul \r\n"
        "\r\n"
        "X>sort  0<nul 1>nul \r\n", TRUE);
    check_file(__LINE__, "x1.txt", "a  b\r\n");
    check_file(__LINE__, "x2.txt", "a    b\r\n");
    check_file(__LINE__, "x3.txt", "a\r\n");
    check_file(__LINE__, "x4.txt", "a  \r\n");
    delete_file("echoon2.cmd");
}

static void test_all_arguments(void)
{
    write_file("bar.bat", "@echo [%*]\r\n");
    write_file("args.cmd",
        "@echo off\r\n"
        "call bar.bat cp1 & rem\r\n"
        "call bar.bat cp2\r\n"
        "call %%undefvar%% bar.bat cp3 %%undefvar%%\r\n"
        "call bar.bat  cp4  x  & rem\r\n"
        "call bar.bat cp5\t& rem\r\n"
        "set foobar=echo\r\n"
        "call %%%foobar%%% bar.bat cp6 %%%foobar%%%\r\n");
    check_cmd(__LINE__, "cmd /c args.cmd", NULL, 0,
        "[cp1]\r\n"
        "[cp2]\r\n"
        "[cp3]\r\n"
        "[cp4  x]\r\n"
        "[cp5]\r\n"
        "[cp6]\r\n", TRUE);
    delete_file("args.cmd");
    delete_file("bar.bat");
}

static void test_syntax_errors(void)
{
    write_file("sub.cmd",
        "@echo off\r\n"
        "echo before\r\n"
        "echo>\r\n"
        "echo after %errorlevel%\r\n");
    write_file("main2.cmd",
        "@echo off\r\n"
        "call sub.cmd\r\n"
        "echo back %errorlevel%\r\n");
    write_file("andand.cmd",
        "@echo off\r\n"
        "echo before\r\n"
        "echo a &&\r\n"
        "echo after\r\n");
    check_cmd(__LINE__, "cmd /c sub.cmd", NULL, 255, "before\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c main2.cmd", NULL, 255, "before\r\n", TRUE);
    check_cmd(__LINE__, "cmd /q /c \"echo>\"", NULL, 1, "", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"echo>\"", "echo alive %errorlevel%\r\n", 1, "", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"echo a|\"", "echo alive %errorlevel%\r\n", 1, "", TRUE);
    check_cmd(__LINE__, "cmd /c andand.cmd", NULL, 255, "before\r\n", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"echo a &&\"", "echo alive %errorlevel%\r\n", 1, "", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"echo a ||\"", "echo alive %errorlevel%\r\n", 1, "", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"(echo a\"", "echo alive %errorlevel%\r\n", 1, "", TRUE);
    check_cmd(__LINE__, "cmd /q /k \"echo a &\"", "echo alive %errorlevel%\r\n", 0, "alive 0\r\n", FALSE);
    check_cmd(__LINE__, "cmd /q /k \"echo a & (dir nonexist >nul)\"", "echo alive %errorlevel%\r\n", 0,
              "alive 1\r\n", FALSE);
    check_cmd(__LINE__, "cmd /q", "echo>\r\necho still alive %errorlevel%\r\n", 0, "still alive 0\r\n", FALSE);
    delete_file("andand.cmd");
    delete_file("main2.cmd");
    delete_file("sub.cmd");
}

static void test_exit_codes(void)
{
    write_file("bat1.cmd", "@echo off\r\ndir nonexist >nul 2>&1\r\necho x\r\n");
    write_file("bat2.cmd", "@echo off\r\ndir nonexist >nul 2>&1\r\nrem x\r\n");
    write_file("bat4.cmd", "@echo off\r\ndir nonexist >nul 2>&1\r\n");
    write_file("bat5.cmd", "@echo off\r\ndir nonexist >nul 2>&1\r\necho x\r\necho eof %errorlevel%\r\n");
    write_file("bat6.cmd", "@echo off\r\ncall bat1.cmd\r\necho back %errorlevel%\r\n");
    write_file("bat7.cmd", "@echo off\r\ndir nonexist >nul 2>&1\r\nset y=1\r\n");
    check_cmd(__LINE__, "cmd /c \"dir nonexist >nul 2>&1 & echo x\"", NULL, 0, "x\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c \"dir nonexist >nul 2>&1 & rem\"", NULL, 0, "", TRUE);
    check_cmd(__LINE__, "cmd /c \"dir nonexist >nul 2>&1 & set x=1\"", NULL, 0, "", TRUE);
    check_cmd(__LINE__, "cmd /c bat1.cmd", NULL, 0, "x\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c bat2.cmd", NULL, 0, "", TRUE);
    check_cmd(__LINE__, "cmd /c bat4.cmd", NULL, 1, "", TRUE);
    check_cmd(__LINE__, "cmd /c bat5.cmd", NULL, 0, "x\r\neof 1\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c bat7.cmd", NULL, 0, "", TRUE);
    check_cmd(__LINE__, "cmd /c \"call bat1.cmd\"", NULL, 1, "x\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c \"bat1.cmd & echo y\"", NULL, 0, "x\r\ny\r\n", TRUE);
    check_cmd(__LINE__, "cmd /c bat6.cmd", NULL, 0, "x\r\nback 1\r\n", TRUE);
    delete_file("bat7.cmd");
    delete_file("bat6.cmd");
    delete_file("bat5.cmd");
    delete_file("bat4.cmd");
    delete_file("bat2.cmd");
    delete_file("bat1.cmd");
}

START_TEST(script)
{
    char temp[MAX_PATH];

    GetTempPathA(sizeof(temp), temp);
    StringCbPrintfA(s_dir, sizeof(s_dir), "%scmd_script_test", temp);
    CreateDirectoryA(s_dir, NULL);

    test_echo_on_display();
    test_echo_on_redirections_and_keywords();
    test_all_arguments();
    test_syntax_errors();
    test_exit_codes();

    RemoveDirectoryA(s_dir);
}
