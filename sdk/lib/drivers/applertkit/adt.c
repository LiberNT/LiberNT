/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reader of the Apple device tree the boot loader exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleadt.h>

#define ADT_HEADER_SIZE             8
#define ADT_PROPERTY_HEADER_SIZE    (APPLE_ADT_NAME_LENGTH + sizeof(ULONG))
#define ADT_MAX_DEPTH               16

static
BOOLEAN
AdtRead32(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Offset,
    _Out_ PULONG Value)
{
    *Value = 0;
    if (Offset > Adt->Length - sizeof(ULONG) || Offset > Adt->Length)
        return FALSE;
    RtlCopyMemory(Value, Adt->Base + Offset, sizeof(ULONG));
    return TRUE;
}

static
BOOLEAN
AdtNodeCounts(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _Out_ PULONG Properties,
    _Out_ PULONG Children)
{
    *Properties = 0;
    *Children = 0;
    if (Node == APPLE_ADT_NO_NODE || Node > Adt->Length || Adt->Length - Node < ADT_HEADER_SIZE)
        return FALSE;
    return AdtRead32(Adt, Node, Properties) && AdtRead32(Adt, Node + sizeof(ULONG), Children);
}

/* A property is its fixed name, its length and its value, and the next one starts on a word. */
static
BOOLEAN
AdtProperty(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Offset,
    _Out_ PULONG Size,
    _Out_ PULONG Next)
{
    ULONG Raw;

    *Size = 0;
    *Next = 0;
    if (Offset > Adt->Length || Adt->Length - Offset < ADT_PROPERTY_HEADER_SIZE)
        return FALSE;
    if (!AdtRead32(Adt, Offset + APPLE_ADT_NAME_LENGTH, &Raw))
        return FALSE;
    *Size = Raw & ~APPLE_ADT_TEMPLATE;
    if (*Size > Adt->Length - Offset - ADT_PROPERTY_HEADER_SIZE)
        return FALSE;
    *Next = Offset + ADT_PROPERTY_HEADER_SIZE + ((*Size + 3) & ~3UL);
    return *Next > Offset;
}

static
BOOLEAN
AdtSkipNode(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_ ULONG Depth,
    _Out_ PULONG Next);

static
BOOLEAN
AdtBodyOffset(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _Out_ PULONG Body)
{
    ULONG Properties, Children, Offset, Index, Size;

    *Body = 0;
    if (!AdtNodeCounts(Adt, Node, &Properties, &Children))
        return FALSE;
    Offset = Node + ADT_HEADER_SIZE;
    for (Index = 0; Index < Properties; ++Index)
    {
        if (!AdtProperty(Adt, Offset, &Size, &Offset))
            return FALSE;
    }
    *Body = Offset;
    return TRUE;
}

static
BOOLEAN
AdtSkipNode(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_ ULONG Depth,
    _Out_ PULONG Next)
{
    ULONG Properties, Children, Offset, Index;

    *Next = 0;
    if (Depth >= ADT_MAX_DEPTH || !AdtNodeCounts(Adt, Node, &Properties, &Children) ||
        !AdtBodyOffset(Adt, Node, &Offset))
    {
        return FALSE;
    }
    for (Index = 0; Index < Children; ++Index)
    {
        if (!AdtSkipNode(Adt, Offset, Depth + 1, &Offset))
            return FALSE;
    }
    *Next = Offset;
    return TRUE;
}

BOOLEAN
NTAPI
AppleAdtOpen(
    _Out_ PAPPLE_ADT Adt,
    _In_reads_bytes_(Length) const VOID *Base,
    _In_ ULONG Length)
{
    ULONG Properties, Children, Next;

    Adt->Base = Base;
    Adt->Length = Length;
    if (!Base || Length < ADT_HEADER_SIZE || !AdtNodeCounts(Adt, 0, &Properties, &Children) || !Properties)
    {
        Adt->Base = NULL;
        Adt->Length = 0;
        return FALSE;
    }
    if (!AdtSkipNode(Adt, 0, 0, &Next))
    {
        Adt->Base = NULL;
        Adt->Length = 0;
        return FALSE;
    }
    return TRUE;
}

ULONG
NTAPI
AppleAdtRootNode(
    _In_ PAPPLE_ADT Adt)
{
    return Adt->Base ? 0 : APPLE_ADT_NO_NODE;
}

ULONG
NTAPI
AppleAdtFirstChild(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node)
{
    ULONG Properties, Children, Body;

    if (!AdtNodeCounts(Adt, Node, &Properties, &Children) || !Children || !AdtBodyOffset(Adt, Node, &Body))
        return APPLE_ADT_NO_NODE;
    return Body;
}

ULONG
NTAPI
AppleAdtNextSibling(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node)
{
    ULONG Next;

    if (!AdtSkipNode(Adt, Node, 0, &Next) || Next >= Adt->Length)
        return APPLE_ADT_NO_NODE;
    return Next;
}

const VOID *
NTAPI
AppleAdtGetProperty(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_opt_ PULONG Length)
{
    ULONG Properties, Children, Offset, Index, Size, Next, Position;

    if (Length)
        *Length = 0;
    if (!AdtNodeCounts(Adt, Node, &Properties, &Children))
        return NULL;
    Offset = Node + ADT_HEADER_SIZE;
    for (Index = 0; Index < Properties; ++Index)
    {
        if (!AdtProperty(Adt, Offset, &Size, &Next))
            return NULL;
        for (Position = 0; Position < APPLE_ADT_NAME_LENGTH; ++Position)
        {
            if (Adt->Base[Offset + Position] != (UCHAR)Name[Position])
                break;
            if (!Name[Position])
            {
                if (Length)
                    *Length = Size;
                return Adt->Base + Offset + ADT_PROPERTY_HEADER_SIZE;
            }
        }
        Offset = Next;
    }
    return NULL;
}

PCSTR
NTAPI
AppleAdtNodeName(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node)
{
    ULONG Size;
    const VOID *Value = AppleAdtGetProperty(Adt, Node, "name", &Size);

    return (Value && Size) ? (PCSTR)Value : NULL;
}

ULONG
NTAPI
AppleAdtChild(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name)
{
    ULONG Child, Index;
    PCSTR Found;

    for (Child = AppleAdtFirstChild(Adt, Node); Child != APPLE_ADT_NO_NODE;
         Child = AppleAdtNextSibling(Adt, Child))
    {
        Found = AppleAdtNodeName(Adt, Child);
        if (!Found)
            continue;
        for (Index = 0;; ++Index)
        {
            if (Found[Index] != Name[Index])
                break;
            if (!Name[Index])
                return Child;
        }
    }
    return APPLE_ADT_NO_NODE;
}

ULONG
NTAPI
AppleAdtPath(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Path)
{
    CHAR Name[APPLE_ADT_NAME_LENGTH];
    ULONG Node = AppleAdtRootNode(Adt);
    ULONG Length;

    if (!Path || Path[0] != '/')
        return APPLE_ADT_NO_NODE;
    ++Path;
    while (*Path && Node != APPLE_ADT_NO_NODE)
    {
        for (Length = 0; Path[Length] && Path[Length] != '/'; ++Length)
        {
            if (Length == APPLE_ADT_NAME_LENGTH - 1)
                return APPLE_ADT_NO_NODE;
            Name[Length] = Path[Length];
        }
        Name[Length] = '\0';
        Node = Length ? AppleAdtChild(Adt, Node, Name) : Node;
        Path += Length;
        if (*Path == '/')
            ++Path;
    }
    return Node;
}

BOOLEAN
NTAPI
AppleAdtReadU32(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_ PULONG Value)
{
    ULONG Length;
    const VOID *Found = AppleAdtGetProperty(Adt, Node, Name, &Length);

    *Value = 0;
    if (!Found || Length != sizeof(ULONG))
        return FALSE;
    RtlCopyMemory(Value, Found, sizeof(ULONG));
    return TRUE;
}

BOOLEAN
NTAPI
AppleAdtReadU64(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_ PULONG64 Value)
{
    ULONG Length;
    const VOID *Found = AppleAdtGetProperty(Adt, Node, Name, &Length);

    *Value = 0;
    if (!Found || Length != sizeof(ULONG64))
        return FALSE;
    RtlCopyMemory(Value, Found, sizeof(ULONG64));
    return TRUE;
}

BOOLEAN
NTAPI
AppleAdtIsCompatible(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Compatible)
{
    ULONG Length, Offset, Index;
    PCSTR List = AppleAdtGetProperty(Adt, Node, "compatible", &Length);

    if (!List)
        return FALSE;
    for (Offset = 0; Offset < Length; ++Offset)
    {
        for (Index = 0; Offset + Index < Length; ++Index)
        {
            if (List[Offset + Index] != Compatible[Index])
                break;
            if (!Compatible[Index])
                return TRUE;
        }
        while (Offset < Length && List[Offset])
            ++Offset;
    }
    return FALSE;
}
