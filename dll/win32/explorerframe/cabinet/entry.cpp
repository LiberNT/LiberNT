/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     File Explorer window threads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cabinet.h"
#include <browseui_undoc.h>

extern "C" HRESULT WINAPI SHOpenFolderWindow(PIE_THREAD_PARAM_BLOCK parameters);
extern "C" IEThreadParamBlock *WINAPI SHCreateIETHREADPARAM(long param8, long paramC, IUnknown *param10,
                                                            IUnknown *param14);

HRESULT
Cabinet_RunWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd)
{
    CCabinetWindow *window;
    DPI_AWARENESS_CONTEXT previous;
    HRESULT hr;
    MSG msg;

    if (!ShellFrame::Startup())
        return E_FAIL;
    PIDLIST_ABSOLUTE fallback = NULL;

    previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!pidl && SUCCEEDED(Cabinet_GetDefaultFolder(&fallback)))
        pidl = fallback;
    window = new CCabinetWindow();
    hr = window ? window->Open(pidl, select, showCmd ? showCmd : SW_SHOWNORMAL) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        while (GetMessageW(&msg, NULL, 0, 0) > 0)
        {
            if (!window->TranslateAcceleratorMessage(&msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    delete window;
    if (fallback)
        ILFree(fallback);
    if (previous)
        SetThreadDpiAwarenessContext(previous);
    ShellFrame::Shutdown();
    return hr;
}

HRESULT
Cabinet_OpenWindow(PCIDLIST_ABSOLUTE pidl, PCUITEMID_CHILD select, int showCmd)
{
    PIE_THREAD_PARAM_BLOCK parameters;
    HRESULT hr;

    UNREFERENCED_PARAMETER(select);
    parameters = SHCreateIETHREADPARAM(0, showCmd, NULL, NULL);
    if (!parameters)
        return E_OUTOFMEMORY;
    parameters->directoryPIDL = ILClone(pidl);
    hr = SHOpenFolderWindow(parameters);
    return hr;
}
