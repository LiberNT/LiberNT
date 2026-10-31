/*
 * PROJECT:     LiberNT Terminal
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A shell process attached to a pseudo console
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>
#include <string>

#include "settings.h"
#include "vt.h"

#define WM_SESSION_OUTPUT (WM_APP + 1)
#define WM_SESSION_EXITED (WM_APP + 2)

class Session
{
public:
    Session(UINT id, const Profile &profile, int cols, int rows);
    ~Session();

    /* Starts the profile's command line; both messages carry the session id in wParam. */
    bool Start(HWND notify, const std::wstring &commandLine, const std::wstring &directory, std::wstring *error);
    void Write(const std::string &bytes);
    void Resize(int cols, int rows);
    /* Feeds the queued output into the screen; returns false when nothing was queued. */
    bool Pump();

    UINT Id() const { return m_Id; }
    Vt::Screen &Screen() { return m_Screen; }
    const Profile &Settings() const { return m_Profile; }
    void SetProfile(const Profile &profile);
    bool Exited() const { return m_Exited; }
    DWORD ExitCode() const { return m_ExitCode; }
    void MarkExited(DWORD code);
    const std::wstring &CommandLine() const { return m_CommandLine; }
    const std::wstring &Directory() const { return m_Directory; }

private:
    static DWORD WINAPI ReaderThread(void *parameter);
    static void CALLBACK ExitCallback(void *parameter, BOOLEAN timedOut);

    UINT m_Id;
    Profile m_Profile;
    Vt::Screen m_Screen;
    HWND m_Notify = NULL;
    HPCON m_Console = NULL;
    HANDLE m_Input = NULL;
    HANDLE m_Output = NULL;
    HANDLE m_Process = NULL;
    HANDLE m_Reader = NULL;
    HANDLE m_Wait = NULL;
    CRITICAL_SECTION m_Lock;
    std::string m_Pending;
    bool m_Posted = false;
    bool m_Exited = false;
    DWORD m_ExitCode = 0;
    std::wstring m_CommandLine;
    std::wstring m_Directory;
};
