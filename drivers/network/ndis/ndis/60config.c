/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 configuration handles opened with NdisOpenConfigurationEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

static const WCHAR Ndis6ServicesKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
static const WCHAR Ndis6ParametersKey[] = L"\\Parameters";

/*
 * NdisOpenConfigurationEx
 *
 * NDIS 6 replacement for NdisOpenConfiguration. ConfigObject->NdisHandle is
 * the adapter handle from MiniportInitializeEx; the returned handle feeds the
 * existing NdisReadConfiguration/NdisCloseConfiguration implementation, so it
 * must be a MINIPORT_CONFIGURATION_CONTEXT holding the adapter's driver key.
 */
static NTSTATUS
Ndis6OpenParametersKey(
    _In_ PCWSTR Prefix,
    _In_ PCUNICODE_STRING Name,
    _Out_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    NTSTATUS Status;

    KeyName.Length = 0;
    KeyName.MaximumLength = (USHORT)(wcslen(Prefix) * sizeof(WCHAR) + Name->Length + sizeof(Ndis6ParametersKey));
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, KeyName.MaximumLength, NDIS6_TAG);
    if (KeyName.Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlAppendUnicodeToString(&KeyName, Prefix);
    RtlAppendUnicodeStringToString(&KeyName, Name);
    RtlAppendUnicodeToString(&KeyName, Ndis6ParametersKey);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
    ExFreePoolWithTag(KeyName.Buffer, NDIS6_TAG);
    return Status;
}

NDIS_STATUS
NTAPI
NdisOpenConfigurationEx(
    _In_  PNDIS_CONFIGURATION_OBJECT ConfigObject,
    _Out_ PNDIS_HANDLE  ConfigurationHandle)
{
    PNDIS_CONFIGURATION_OBJECT Obj = ConfigObject;
    PMINIPORT_CONFIGURATION_CONTEXT Ctx;
    PLOGICAL_ADAPTER Adapter;
    UNICODE_STRING Name;
    ULONG Signature;
    HANDLE KeyHandle;
    NTSTATUS Status;
    BOOLEAN AdapterKey = FALSE;

    if (Obj == NULL || ConfigurationHandle == NULL || Obj->NdisHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *ConfigurationHandle = NULL;
    Signature = *(PULONG)Obj->NdisHandle;

    if (Signature == NDIS6_PROTOCOL_BINDING_SIGNATURE)
    {
        PNDIS6_PROTOCOL_BINDING Binding = Obj->NdisHandle;
        OBJECT_ATTRIBUTES ObjectAttributes;

        if (Ndis6BuildProtocolSection(Binding->DriverBlock, Binding->Adapter, &Name) != NDIS_STATUS_SUCCESS)
            return NDIS_STATUS_RESOURCES;

        InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = ZwOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
        ExFreePoolWithTag(Name.Buffer, NDIS6_TAG);
    }
    else if (Signature == NDIS6_PROTOCOL_DRIVER_SIGNATURE)
    {
        PNDIS6_PROTOCOL_DRIVER_BLOCK Block = Obj->NdisHandle;

        Status = Ndis6OpenParametersKey(Ndis6ServicesKey, &Block->Characteristics.Name, &KeyHandle);
    }
    else if (Signature == NDIS6_DRIVER_BLOCK_SIGNATURE)
    {
        PNDIS6_DRIVER_BLOCK Block = Obj->NdisHandle;

        Status = Ndis6OpenParametersKey(L"", &Block->RegistryPath, &KeyHandle);
    }
    else if (Signature == NDIS6_FILTER_MODULE_SIGNATURE || Signature == NDIS6_FILTER_DRIVER_SIGNATURE)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block;

        if (Signature == NDIS6_FILTER_MODULE_SIGNATURE)
            Block = ((PNDIS6_FILTER_MODULE)Obj->NdisHandle)->DriverBlock;
        else
            Block = Obj->NdisHandle;

        RtlInitUnicodeString(&Name, Block->ServiceNameBuffer);
        Status = Ndis6OpenParametersKey(Ndis6ServicesKey, &Name, &KeyHandle);
    }
    else
    {
        Adapter = GET_LOGICAL_ADAPTER(Obj->NdisHandle);
        if (!Adapter->IsNdis6 ||
            Adapter->NdisMiniportBlock.DeviceObject == NULL ||
            Adapter->NdisMiniportBlock.PhysicalDeviceObject == NULL)
        {
            return NDIS_STATUS_INVALID_PARAMETER;
        }

        Status = IoOpenDeviceRegistryKey(
            Adapter->NdisMiniportBlock.PhysicalDeviceObject,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_ALL_ACCESS,
            &KeyHandle);
        AdapterKey = TRUE;
    }

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("NDIS6: failed to open the configuration key (0x%08X)\n", Status);
        return NDIS_STATUS_FAILURE;
    }

    Ctx = ExAllocatePool(NonPagedPool, sizeof(MINIPORT_CONFIGURATION_CONTEXT));
    if (Ctx == NULL)
    {
        ZwClose(KeyHandle);
        return NDIS_STATUS_RESOURCES;
    }

    KeInitializeSpinLock(&Ctx->ResourceLock);
    InitializeListHead(&Ctx->ResourceListHead);
    Ctx->Handle = KeyHandle;
    Ctx->AdapterDefaults = AdapterKey;

    *ConfigurationHandle = (NDIS_HANDLE)Ctx;
    return NDIS_STATUS_SUCCESS;
}

/* EOF */
