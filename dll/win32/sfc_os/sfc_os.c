/*
 * System File Checker (Windows File Protection)
 *
 * Copyright 2008 Pierre Schweitzer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define WIN32_NO_STATUS

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <winwlx.h>
#define NTOS_MODE_USER
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>

HINSTANCE hLibModule;

typedef struct _PROTECTED_FILE_DATA
{
    WCHAR FileName[MAX_PATH];
    DWORD FileNumber;
} PROTECTED_FILE_DATA, *PPROTECTED_FILE_DATA;


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinstDLL);
            hLibModule = hinstDLL;
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            break;
        }
    }

    return TRUE;
}


/******************************************************************
 *              SfcIsFileProtected     [sfc_os.@]
 *
 * Check, if the given File is protected by the System
 *
 * PARAMS
 *  RpcHandle    [I] This must be NULL
 *  ProtFileName [I] Filename with Path to check
 *
 * RETURNS
 *  Failure: FALSE with GetLastError() != ERROR_FILE_NOT_FOUND
 *  Success: TRUE, when the File is Protected
 *           FALSE with GetLastError() == ERROR_FILE_NOT_FOUND,
 *           when the File is not Protected
 *
 *
 * BUGS
 *  We return always the Result for: "File is not Protected"
 *
 */
BOOL WINAPI SfcIsFileProtected(HANDLE RpcHandle, LPCWSTR ProtFileName)
{
    static const SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    static const ULONG TrustedInstallerRids[] =
    {
        SECURITY_SERVICE_ID_BASE_RID, 956008885, 3418522649U, 1831038044, 1853292631, 2271478464U
    };
    UCHAR SidBuffer[SECURITY_MAX_SID_SIZE];
    PSID TrustedInstaller = (PSID)SidBuffer, Owner = NULL;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACCESS_ALLOWED_ACE Ace;
    PACL Dacl = NULL;
    BOOLEAN Defaulted, Present = FALSE, Protected = FALSE;
    ULONG Index, Length = 0;
    NTSTATUS Status;
    HANDLE File = INVALID_HANDLE_VALUE;

    DPRINT("(%p, %S)\n", RpcHandle, ProtFileName);

    if (ProtFileName)
    {
        File = CreateFileW(ProtFileName, READ_CONTROL,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    }
    if (File != INVALID_HANDLE_VALUE)
    {
        Status = NtQuerySecurityObject(File, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                       NULL, 0, &Length);
        if (Status == STATUS_BUFFER_TOO_SMALL)
            Descriptor = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
        if (Descriptor)
        {
            Status = NtQuerySecurityObject(File, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                           Descriptor, Length, &Length);
        }
        CloseHandle(File);

        RtlInitializeSid(TrustedInstaller, (PSID_IDENTIFIER_AUTHORITY)&NtAuthority,
                         RTL_NUMBER_OF(TrustedInstallerRids));
        for (Index = 0; Index < RTL_NUMBER_OF(TrustedInstallerRids); Index++)
            *RtlSubAuthoritySid(TrustedInstaller, Index) = TrustedInstallerRids[Index];

        if (Descriptor && NT_SUCCESS(Status) &&
            NT_SUCCESS(RtlGetOwnerSecurityDescriptor(Descriptor, &Owner, &Defaulted)) &&
            Owner != NULL && RtlEqualSid(Owner, TrustedInstaller) &&
            NT_SUCCESS(RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted)) &&
            Present && Dacl != NULL)
        {
            Protected = TRUE;
            for (Index = 0; Index < Dacl->AceCount; Index++)
            {
                if (!NT_SUCCESS(RtlGetAce(Dacl, Index, (PVOID*)&Ace)))
                {
                    Protected = FALSE;
                    break;
                }
                if (Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    !(Ace->Header.AceFlags & INHERIT_ONLY_ACE) &&
                    (Ace->Mask & ~(FILE_GENERIC_READ | FILE_GENERIC_EXECUTE)) &&
                    !RtlEqualSid((PSID)&Ace->SidStart, TrustedInstaller))
                {
                    Protected = FALSE;
                    break;
                }
            }
        }
        if (Descriptor)
            RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor);
    }

    SetLastError(Protected ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND);
    return Protected;
}

/******************************************************************
 *              SfcIsKeyProtected     [sfc_os.@]
 *
 * Check, if the given Registry Key is protected by the System
 *
 * PARAMS
 *  hKey          [I] Handle to the root registry key
 *  lpSubKey      [I] Name of the subkey to check
 *  samDesired    [I] The Registry View to Examine (32 or 64 bit)
 *
 * RETURNS
 *  Failure: FALSE with GetLastError() != ERROR_FILE_NOT_FOUND
 *  Success: TRUE, when the Key is Protected
 *           FALSE with GetLastError() == ERROR_FILE_NOT_FOUND,
 *           when the Key is not Protected
 *
 */
BOOL WINAPI SfcIsKeyProtected(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired)
{
    static BOOL reported = FALSE;

    if (reported) {
        DPRINT("(%p, %S) stub\n", hKey, lpSubKey);
    }
    else
    {
        DPRINT1("(%p, %S) stub\n", hKey, lpSubKey);
        reported = TRUE;
    }

    if( !hKey ) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}

/******************************************************************
 *              SfcGetNextProtectedFile     [sfc_os.@]
 */
BOOL WINAPI SfcGetNextProtectedFile(HANDLE RpcHandle, PPROTECTED_FILE_DATA ProtFileData)
{
    if (!ProtFileData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    UNIMPLEMENTED;
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

/******************************************************************
 *              SfcFileException     [sfc_os.@]
 *
 * Disable the protection for the given file during one minute
 *
 * PARAMS
 *  dwUnknown0    [I] Set to 0
 *  pwszFile      [I] Name of the file to unprotect
 *  dwUnknown1    [I] Set to -1
 *
 * RETURNS
 *  Failure: 1;
 *  Success: 0;
 *
 */
DWORD WINAPI SfcFileException(DWORD dwUnknown0, PWCHAR pwszFile, DWORD dwUnknown1)
{
    UNIMPLEMENTED;
    /* Always return success */
    return 0;
}

/******************************************************************
 *              SfcWLEventLogoff     [sfc_os.@]
 *
 * Logoff notification function
 *
 * PARAMS
 *  pInfo         [I] Pointer to logoff notification information
 *
 * RETURNS
 *  nothing
 *
 */
VOID
WINAPI
SfcWLEventLogoff(
    PWLX_NOTIFICATION_INFO pInfo)
{
    UNIMPLEMENTED;
}

/******************************************************************
 *              SfcWLEventLogon     [sfc_os.@]
 *
 * Logon notification function
 *
 * PARAMS
 *  pInfo         [I] Pointer to logon notification information
 *
 * RETURNS
 *  nothing
 *
 */
VOID
WINAPI
SfcWLEventLogon(
    PWLX_NOTIFICATION_INFO pInfo)
{
    UNIMPLEMENTED;
}
