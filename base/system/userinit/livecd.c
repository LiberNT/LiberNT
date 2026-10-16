/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Userinit Logon Application
 * FILE:        base/system/userinit/livecd.c
 * PROGRAMMERS: Eric Kohl
 */

#include "userinit.h"


BOOL
IsLiveCD(VOID)
{
    HKEY ControlKey = NULL;
    LPWSTR SystemStartOptions = NULL;
    LPWSTR CurrentOption, NextOption; /* Pointers into SystemStartOptions */
    LONG rc;
    BOOL ret = FALSE;

    rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                       REGSTR_PATH_CURRENT_CONTROL_SET,
                       0,
                       KEY_QUERY_VALUE,
                       &ControlKey);
    if (rc != ERROR_SUCCESS)
    {
        WARN("RegOpenKeyEx() failed with error %lu\n", rc);
        goto cleanup;
    }

    rc = ReadRegSzKey(ControlKey, L"SystemStartOptions", &SystemStartOptions);
    if (rc != ERROR_SUCCESS)
    {
        WARN("ReadRegSzKey() failed with error %lu\n", rc);
        goto cleanup;
    }

    /* Check for CONSOLE switch in SystemStartOptions */
    CurrentOption = SystemStartOptions;
    while (CurrentOption)
    {
        NextOption = wcschr(CurrentOption, L' ');
        if (NextOption)
            *NextOption = L'\0';
        if (_wcsicmp(CurrentOption, L"MININT") == 0)
        {
            TRACE("Found 'MININT' boot option\n");
            ret = TRUE;
            goto cleanup;
        }
        CurrentOption = NextOption ? NextOption + 1 : NULL;
    }

cleanup:
    if (ControlKey != NULL)
        RegCloseKey(ControlKey);
    HeapFree(GetProcessHeap(), 0, SystemStartOptions);

    TRACE("IsLiveCD() returning %u\n", ret);
    return ret;
}

/* EOF */
