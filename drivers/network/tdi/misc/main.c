/*
 * DESCRIPTION: Entry point for TDI.SYS
 * (c) Captain Obvious
 */

#include "precomp.h"

extern LONG CteTimeIncrement;

LIST_ENTRY TdiPnpClientList;
LIST_ENTRY TdiNetAddressList;
KMUTEX TdiPnpMutex;

NTSTATUS
NTAPI
DllInitialize(IN PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    CteTimeIncrement = KeQueryTimeIncrement();
    InitializeListHead(&TdiPnpClientList);
    InitializeListHead(&TdiNetAddressList);
    KeInitializeMutex(&TdiPnpMutex, 0);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    /* Initialize the time increment for CTE timers */
    CteTimeIncrement = KeQueryTimeIncrement();

    return STATUS_SUCCESS;
}

/* EOF */
