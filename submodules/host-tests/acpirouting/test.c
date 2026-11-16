/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A PCI-to-PCI bridge's _PRT routes the interrupts of its secondary bus
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <string.h>

int HostTestFailures;

typedef struct _FAKE_NODE
{
    const char *Name;
    int Parent;
    int HasAdr;
    UINT64 Adr;
    int HasBbn;
    UINT64 Bbn;
    UINT32 PrtAddress;
    UINT32 PrtGsi[4];
} FAKE_NODE;

typedef struct _FAKE_FUNCTION
{
    UINT16 Bus;
    UINT16 Device;
    UINT16 Function;
    UINT8 HeaderType;
    UINT8 SecondaryBus;
} FAKE_FUNCTION;

/* The AMD Picasso board: GP17 keeps the root order, GP18 rotates it, GP19 has
 * no _PRT and holds a bridge with its own, GP1A was left unnumbered. */
static const FAKE_NODE Nodes[] =
{
    { "PCI0", -1, 0, 0, 1, 0, 0x0008FFFF, { 0x34, 0x35, 0x36, 0x37 } },
    { "GP17", 0, 1, 0x00080001, 0, 0, 0xFFFF, { 0x34, 0x35, 0x36, 0x37 } },
    { "GP18", 0, 1, 0x00080002, 0, 0, 0xFFFF, { 0x36, 0x37, 0x34, 0x35 } },
    { "GP19", 0, 1, 0x00080003, 0, 0, 0, { 0 } },
    { "BR07", 3, 1, 0x00000000, 0, 0, 0x0003FFFF, { 0x30, 0x31, 0x32, 0x33 } },
    { "SATA", 2, 1, 0x00000000, 0, 0, 0, { 0 } },
    { "SMBS", 0, 1, 0x00140000, 0, 0, 0x0008FFFF, { 0x99, 0x99, 0x99, 0x99 } },
    { "GP1A", 0, 1, 0x00080004, 0, 0, 0x0008FFFF, { 0x98, 0x98, 0x98, 0x98 } },
};

static const FAKE_FUNCTION Functions[] =
{
    { 0, 8, 1, 0x81, 4 },
    { 0, 8, 2, 0x81, 5 },
    { 0, 8, 3, 0x81, 6 },
    { 0, 8, 4, 0x81, 0 },
    { 0, 20, 0, 0x80, 0 },
    { 5, 0, 0, 0x00, 0 },
    { 6, 0, 0, 0x01, 7 },
};

static PHAL_ACPI_PCI_ROUTE_QUERY Provider;
static int Allocations;

static int NodeIndex(ACPI_HANDLE Handle)
{
    return Handle ? (int)((const FAKE_NODE *)Handle - Nodes) : -1;
}

KIRQL KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

PVOID ExAllocatePoolWithTag(int PoolType, size_t Size, ULONG Tag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    Allocations++;
    return malloc(Size);
}

VOID ExFreePoolWithTag(PVOID Block, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    Allocations--;
    free(Block);
}

void AcpiOsFree(void *Memory)
{
    Allocations--;
    free(Memory);
}

VOID NTAPI HalpRegisterPciRouteQuery(PHAL_ACPI_PCI_ROUTE_QUERY NewProvider)
{
    Provider = NewProvider;
}

ACPI_STATUS BuspGetCurrentResources(ACPI_HANDLE Handle, ACPI_BUFFER *Buffer)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Buffer);
    return AE_NOT_FOUND;
}

ACPI_STATUS BuspGetIrqRoutingTable(ACPI_HANDLE Handle, ACPI_BUFFER *Buffer)
{
    const FAKE_NODE *Node = (const FAKE_NODE *)Handle;
    UINT32 Length = (UINT32)((offsetof(ACPI_PCI_ROUTING_TABLE, Source) + 1 + 7) & ~(size_t)7);
    ACPI_PCI_ROUTING_TABLE *Route;
    UINT8 *Table;
    UINT32 Pin;

    if (Node->PrtAddress == 0)
        return AE_NOT_FOUND;
    Table = calloc(5, Length);
    Allocations++;
    for (Pin = 0; Pin < 4; ++Pin)
    {
        Route = (ACPI_PCI_ROUTING_TABLE *)(Table + Pin * Length);
        Route->Length = Length;
        Route->Pin = Pin;
        Route->Address = Node->PrtAddress;
        Route->SourceIndex = Node->PrtGsi[Pin];
    }
    Buffer->Pointer = Table;
    Buffer->Length = 5 * Length;
    return AE_OK;
}

ACPI_STATUS AcpiGetHandle(ACPI_HANDLE Parent, const char *Pathname, ACPI_HANDLE *RetHandle)
{
    UNREFERENCED_PARAMETER(Parent);
    UNREFERENCED_PARAMETER(Pathname);
    UNREFERENCED_PARAMETER(RetHandle);
    return AE_NOT_FOUND;
}

ACPI_STATUS AcpiGetObjectInfo(ACPI_HANDLE Object, ACPI_DEVICE_INFO **ReturnBuffer)
{
    UNREFERENCED_PARAMETER(Object);
    Allocations++;
    *ReturnBuffer = calloc(1, sizeof(ACPI_DEVICE_INFO));
    return AE_OK;
}

ACPI_STATUS AcpiEvaluateObjectTyped(ACPI_HANDLE Object, ACPI_STRING Pathname, ACPI_OBJECT_LIST *ParameterObjects,
                                    ACPI_BUFFER *ReturnObjectBuffer, ACPI_OBJECT_TYPE ReturnType)
{
    const FAKE_NODE *Node = (const FAKE_NODE *)Object;
    ACPI_OBJECT *Result = ReturnObjectBuffer->Pointer;

    UNREFERENCED_PARAMETER(ParameterObjects);
    if (ReturnType != ACPI_TYPE_INTEGER || ReturnObjectBuffer->Length < sizeof(*Result))
        return AE_BAD_PARAMETER;
    Result->Type = ACPI_TYPE_INTEGER;
    if (!strcmp(Pathname, METHOD_NAME__ADR) && Node->HasAdr)
        Result->Integer.Value = Node->Adr;
    else if (!strcmp(Pathname, METHOD_NAME__BBN) && Node->HasBbn)
        Result->Integer.Value = Node->Bbn;
    else
        return AE_NOT_FOUND;
    return AE_OK;
}

ACPI_STATUS AcpiGetNextObject(ACPI_OBJECT_TYPE Type, ACPI_HANDLE Parent, ACPI_HANDLE Child, ACPI_HANDLE *OutHandle)
{
    int Index;

    if (Type != ACPI_TYPE_DEVICE)
        return AE_BAD_PARAMETER;
    for (Index = NodeIndex(Child) + 1; Index < (int)ACPI_ARRAY_LENGTH(Nodes); ++Index)
    {
        if (Nodes[Index].Parent == NodeIndex(Parent))
        {
            *OutHandle = (ACPI_HANDLE)&Nodes[Index];
            return AE_OK;
        }
    }
    return AE_NOT_FOUND;
}

ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Reg, UINT64 *Value, UINT32 Width)
{
    size_t Index;

    for (Index = 0; Index < ACPI_ARRAY_LENGTH(Functions); ++Index)
    {
        const FAKE_FUNCTION *Function = &Functions[Index];

        if (Function->Bus != PciId->Bus || Function->Device != PciId->Device || Function->Function != PciId->Function)
            continue;
        if (Reg == 0x00 && Width == 16)
            *Value = 0x1022;
        else if (Reg == 0x0E && Width == 8)
            *Value = Function->HeaderType;
        else if (Reg == 0x19 && Width == 8)
            *Value = Function->SecondaryBus;
        else
            return AE_BAD_PARAMETER;
        return AE_OK;
    }
    *Value = (Width >= 64) ? ~(UINT64)0 : (((UINT64)1 << Width) - 1);
    return AE_OK;
}

ACPI_STATUS AcpiGetDevices(char *Hid, ACPI_WALK_CALLBACK UserFunction, void *Context, void **ReturnValue)
{
    if (strcmp(Hid, "PNP0C0F") == 0)
        return AE_OK;
    return UserFunction((ACPI_HANDLE)&Nodes[0], 1, Context, ReturnValue);
}

ACPI_STATUS AcpiWalkNamespace(ACPI_OBJECT_TYPE Type, ACPI_HANDLE StartObject, UINT32 MaxDepth,
                              ACPI_WALK_CALLBACK DescendingCallback, ACPI_WALK_CALLBACK AscendingCallback,
                              void *Context, void **ReturnValue)
{
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(StartObject);
    UNREFERENCED_PARAMETER(MaxDepth);
    UNREFERENCED_PARAMETER(DescendingCallback);
    UNREFERENCED_PARAMETER(AscendingCallback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ReturnValue);
    return AE_OK;
}

ACPI_STATUS AcpiEvaluateObject(ACPI_HANDLE Object, ACPI_STRING Pathname, ACPI_OBJECT_LIST *ParameterObjects,
                               ACPI_BUFFER *ReturnObjectBuffer)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Pathname);
    UNREFERENCED_PARAMETER(ParameterObjects);
    UNREFERENCED_PARAMETER(ReturnObjectBuffer);
    return AE_NOT_FOUND;
}

static void CheckRoute(UCHAR Bus, UCHAR Device, UCHAR Function, UCHAR Pin, ULONG Expected)
{
    ULONG Gsi = 0;
    UCHAR Polarity = 0xFF;
    UCHAR Trigger = 0xFF;
    BOOLEAN Found = Provider(0, Bus, Device, Function, Pin, &Gsi, &Polarity, &Trigger);

    if (Expected == 0 ? Found : (!Found || Gsi != Expected ||
                                 Polarity != HAL_ACPI_POLARITY_LOW || Trigger != HAL_ACPI_TRIGGER_LEVEL))
    {
        printf("FAIL %02x:%02x.%u INT%c#: found %d GSI 0x%x, expected %s0x%lx\n", Bus, Device, Function,
               'A' + Pin - 1, Found, Gsi, Expected ? "" : "no entry ", (unsigned long)Expected);
        HostTestFailures++;
    }
}

int main(void)
{
    acpi_pci_link_init();
    if (Provider == NULL)
    {
        printf("FAIL no routing provider registered\n");
        return 1;
    }

    CheckRoute(0, 8, 0, 1, 0x34);
    CheckRoute(0, 8, 0, 3, 0x36);
    CheckRoute(4, 0, 1, 2, 0x35);
    CheckRoute(4, 0, 6, 3, 0x36);
    CheckRoute(5, 0, 0, 1, 0x36);
    CheckRoute(5, 0, 0, 2, 0x37);
    CheckRoute(5, 0, 0, 3, 0x34);
    CheckRoute(5, 0, 0, 4, 0x35);
    CheckRoute(6, 0, 0, 1, 0);
    CheckRoute(7, 3, 0, 1, 0x30);
    CheckRoute(7, 3, 0, 4, 0x33);
    CheckRoute(0, 20, 0, 1, 0);

    acpi_pci_link_exit();
    if (Provider != NULL || Allocations != 0)
    {
        printf("FAIL exit left provider %p, %d allocation(s)\n", (void *)Provider, Allocations);
        HostTestFailures++;
    }

    printf("%s: %d failure(s)\n", HostTestFailures ? "FAIL" : "PASS", HostTestFailures);
    return HostTestFailures != 0;
}
