/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/syssetup/classinst.c
 * PURPOSE:     Class installers
 * PROGRAMMERS: Copyright 2006 Hervé Poussineau (hpoussin@reactos.org)
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/*
 * @unimplemented
 */
DWORD
WINAPI
ComputerClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT1("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @unimplemented
 */
DWORD
WINAPI
DeviceBayClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @unimplemented
 */
DWORD
WINAPI
EisaUpHalCoInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL,
    IN OUT PCOINSTALLER_CONTEXT_DATA Context)
{
    switch (InstallFunction)
    {
        default:
            DPRINT1("Install function %u ignored\n", InstallFunction);
            return ERROR_SUCCESS;
    }
}


/*
 * @implemented
 */
DWORD
WINAPI
HdcClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    DPRINT("HdcClassInstaller()\n");
    return ERROR_DI_DO_DEFAULT;
}


/*
 * @unimplemented
 */
DWORD
WINAPI
KeyboardClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @unimplemented
 */
DWORD
WINAPI
MouseClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @unimplemented
 */
DWORD
WINAPI
NtApmClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @unimplemented
 */
DWORD
WINAPI
ScsiClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    switch (InstallFunction)
    {
        default:
            DPRINT("Install function %u ignored\n", InstallFunction);
            return ERROR_DI_DO_DEFAULT;
    }
}


/*
 * @implemented
 */
DWORD
WINAPI
StorageCoInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL,
    IN OUT PCOINSTALLER_CONTEXT_DATA Context)
{
    ULONG ulStatus, ulProblem;
    DWORD dwBufferSize = 0;
    PWSTR pszDeviceDescription;
    CONFIGRET ret;

    DPRINT("StorageCoInstaller(%u %p %p %p)\n",
           InstallFunction, DeviceInfoSet, DeviceInfoData, Context);

    if (InstallFunction != DIF_INSTALLDEVICE)
        return ERROR_SUCCESS;

    if (Context->PostProcessing)
    {
        if (Context->PrivateData != NULL)
        {
            pszDeviceDescription = (PWSTR)Context->PrivateData;

            /* Store the device description as the friendly name */
            SetupDiSetDeviceRegistryPropertyW(DeviceInfoSet,
                                              DeviceInfoData,
                                              SPDRP_FRIENDLYNAME,
                                              (PBYTE)pszDeviceDescription,
                                              (wcslen(pszDeviceDescription) + 1) * sizeof(WCHAR));

            /* Free the device description */
            HeapFree(GetProcessHeap(), 0, Context->PrivateData);
            Context->PrivateData = NULL;
        }
    }
    else
    {
        if (DeviceInfoData == NULL)
            return ERROR_SUCCESS;

        ret = CM_Get_DevNode_Status(&ulStatus, &ulProblem, DeviceInfoData->DevInst, 0);
        if (ret != CR_SUCCESS)
            return ERROR_SUCCESS;

        if (ulStatus & DN_ROOT_ENUMERATED)
            return ERROR_SUCCESS;

        /* Get the device description size */
        SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet,
                                          DeviceInfoData,
                                          SPDRP_DEVICEDESC,
                                          NULL,
                                          NULL,
                                          0,
                                          &dwBufferSize);
        if (dwBufferSize == 0)
            return ERROR_SUCCESS;

        /* Allocate the device description buffer */
        pszDeviceDescription = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwBufferSize);
        if (pszDeviceDescription == NULL)
            return ERROR_SUCCESS;

        /* Get the device description */
        SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet,
                                          DeviceInfoData,
                                          SPDRP_DEVICEDESC,
                                          NULL,
                                          (PBYTE)pszDeviceDescription,
                                          dwBufferSize,
                                          &dwBufferSize);

        Context->PrivateData = (PVOID)pszDeviceDescription;
        return ERROR_DI_POSTPROCESSING_REQUIRED;
    }

    return ERROR_SUCCESS;
}


/*
 * @implemented
 */
DWORD
WINAPI
TapeClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    DPRINT("TapeClassInstaller()\n");
    return ERROR_DI_DO_DEFAULT;
}


/*
 * @implemented
 */
DWORD
WINAPI
VolumeClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    DPRINT("VolumeClassInstaller()\n");
    return ERROR_DI_DO_DEFAULT;
}

/* EOF */
