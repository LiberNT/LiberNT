/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A shell process attached to a pseudo console
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "session.h"

#include <vector>

namespace
{

std::wstring Expand(const std::wstring &text)
{
    DWORD length;
    std::vector<wchar_t> buffer;

    if (text.empty())
        return text;
    length = ExpandEnvironmentStringsW(text.c_str(), NULL, 0);
    if (!length)
        return text;
    buffer.resize(length);
    if (!ExpandEnvironmentStringsW(text.c_str(), buffer.data(), length))
        return text;
    return buffer.data();
}

}

Session::Session(UINT id, const Profile &profile, int cols, int rows) :
    m_Id(id),
    m_Profile(profile),
    m_Screen(cols, rows, (size_t)profile.ScrollbackLines)
{
    InitializeCriticalSection(&m_Lock);
}

Session::~Session()
{
    if (m_Wait)
        UnregisterWaitEx(m_Wait, INVALID_HANDLE_VALUE);
    if (m_Console)
        ClosePseudoConsole(m_Console);
    if (m_Input)
        CloseHandle(m_Input);
    if (m_Reader)
    {
        if (WaitForSingleObject(m_Reader, 2000) == WAIT_TIMEOUT && m_Output)
            CancelSynchronousIo(m_Reader);
        WaitForSingleObject(m_Reader, 2000);
        CloseHandle(m_Reader);
    }
    if (m_Output)
        CloseHandle(m_Output);
    if (m_Process)
        CloseHandle(m_Process);
    DeleteCriticalSection(&m_Lock);
}

void
Session::SetProfile(const Profile &profile)
{
    m_Profile = profile;
    m_Screen.SetScrollbackLimit((size_t)profile.ScrollbackLines);
}

bool
Session::Start(HWND notify, const std::wstring &commandLine, const std::wstring &directory, std::wstring *error)
{
    HANDLE inputRead = NULL, outputWrite = NULL;
    STARTUPINFOEXW startup = {};
    PROCESS_INFORMATION process = {};
    SIZE_T attributeSize = 0;
    COORD size;
    HRESULT hr;
    std::vector<wchar_t> command;
    std::wstring expandedDirectory;
    BOOL created;

    m_Notify = notify;
    m_CommandLine = commandLine.empty() ? m_Profile.CommandLine : commandLine;
    m_Directory = directory.empty() ? m_Profile.StartingDirectory : directory;

    if (!CreatePipe(&inputRead, &m_Input, NULL, 0) || !CreatePipe(&m_Output, &outputWrite, NULL, 0))
    {
        if (error)
            *error = L"The pipes of the pseudo console cannot be created.";
        if (inputRead)
            CloseHandle(inputRead);
        return false;
    }

    size.X = (SHORT)m_Screen.Cols();
    size.Y = (SHORT)m_Screen.Rows();
    hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &m_Console);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr))
    {
        m_Console = NULL;
        if (error)
            *error = L"The pseudo console cannot be created.";
        return false;
    }

    InitializeProcThreadAttributeList(NULL, 1, 0, &attributeSize);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attributeSize);
    if (!startup.lpAttributeList ||
        !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_Console,
                                   sizeof(m_Console), NULL, NULL))
    {
        if (startup.lpAttributeList)
            HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
        if (error)
            *error = L"The process attributes cannot be prepared.";
        return false;
    }
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

    {
        std::wstring expanded = Expand(m_CommandLine);
        command.assign(expanded.begin(), expanded.end());
        command.push_back(L'\0');
    }
    expandedDirectory = Expand(m_Directory);
    if (!expandedDirectory.empty() && GetFileAttributesW(expandedDirectory.c_str()) == INVALID_FILE_ATTRIBUTES)
        expandedDirectory.clear();

    created = CreateProcessW(NULL, command.data(), NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, NULL,
                             expandedDirectory.empty() ? NULL : expandedDirectory.c_str(), &startup.StartupInfo,
                             &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    if (!created)
    {
        if (error)
        {
            wchar_t message[512];
            DWORD code = GetLastError();

            swprintf(message, 512, L"%s could not be started (error %lu).", command.data(), code);
            *error = message;
        }
        return false;
    }
    CloseHandle(process.hThread);
    m_Process = process.hProcess;

    m_Reader = CreateThread(NULL, 0, ReaderThread, this, 0, NULL);
    RegisterWaitForSingleObject(&m_Wait, m_Process, ExitCallback, this, INFINITE, WT_EXECUTEONLYONCE);
    return true;
}

DWORD WINAPI
Session::ReaderThread(void *parameter)
{
    Session *self = (Session *)parameter;
    char buffer[16384];
    DWORD read;

    while (ReadFile(self->m_Output, buffer, sizeof(buffer), &read, NULL) && read)
    {
        bool post;

        EnterCriticalSection(&self->m_Lock);
        self->m_Pending.append(buffer, read);
        post = !self->m_Posted;
        self->m_Posted = true;
        LeaveCriticalSection(&self->m_Lock);
        if (post)
            PostMessageW(self->m_Notify, WM_SESSION_OUTPUT, self->m_Id, 0);
    }
    return 0;
}

void CALLBACK
Session::ExitCallback(void *parameter, BOOLEAN timedOut)
{
    Session *self = (Session *)parameter;
    DWORD code = 0;

    UNREFERENCED_PARAMETER(timedOut);
    GetExitCodeProcess(self->m_Process, &code);
    PostMessageW(self->m_Notify, WM_SESSION_EXITED, self->m_Id, code);
}

void
Session::MarkExited(DWORD code)
{
    m_Exited = true;
    m_ExitCode = code;
}

bool
Session::Pump()
{
    std::string pending;

    EnterCriticalSection(&m_Lock);
    pending.swap(m_Pending);
    m_Posted = false;
    LeaveCriticalSection(&m_Lock);
    if (pending.empty())
        return false;
    m_Screen.Write(pending.data(), pending.size());
    {
        std::string responses = m_Screen.TakeResponses();
        if (!responses.empty())
            Write(responses);
    }
    return true;
}

void
Session::Write(const std::string &bytes)
{
    DWORD written;

    if (!m_Input || bytes.empty() || m_Exited)
        return;
    WriteFile(m_Input, bytes.data(), (DWORD)bytes.size(), &written, NULL);
}

void
Session::Resize(int cols, int rows)
{
    COORD size;

    if (cols == m_Screen.Cols() && rows == m_Screen.Rows())
        return;
    m_Screen.Resize(cols, rows);
    if (m_Console)
    {
        size.X = (SHORT)cols;
        size.Y = (SHORT)rows;
        ResizePseudoConsole(m_Console, size);
    }
}
