/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         Security Account Manager (SAM) Server
 * FILE:            reactos/dll/win32/samsrv/display.c
 * PURPOSE:         Display cache
 *
 * PROGRAMMERS:     Eric Kohl
 */

#include "samsrv.h"

#include <winuser.h>

/* GLOBALS *****************************************************************/

typedef struct _DISPLAY_ENTRY
{
    ULONG Rid;
    ULONG Control;
    RPC_UNICODE_STRING AccountName;
    RPC_UNICODE_STRING AdminComment;
    RPC_UNICODE_STRING FullName;
} DISPLAY_ENTRY, *PDISPLAY_ENTRY;


/* FUNCTIONS ***************************************************************/

static
VOID
SampFreeDisplayEntry(
    _In_ PDISPLAY_ENTRY Entry)
{
    if (Entry->AccountName.Buffer != NULL)
        midl_user_free(Entry->AccountName.Buffer);

    if (Entry->AdminComment.Buffer != NULL)
        midl_user_free(Entry->AdminComment.Buffer);

    if (Entry->FullName.Buffer != NULL)
        midl_user_free(Entry->FullName.Buffer);

    RtlZeroMemory(Entry, sizeof(*Entry));
}


static
int
__cdecl
SampCompareDisplayEntries(
    _In_ const void *First,
    _In_ const void *Second)
{
    return RtlCompareUnicodeString((PUNICODE_STRING)&((PDISPLAY_ENTRY)First)->AccountName,
                                   (PUNICODE_STRING)&((PDISPLAY_ENTRY)Second)->AccountName,
                                   TRUE);
}


static
NTSTATUS
SampReadDisplayEntry(
    _In_ PSAM_DB_OBJECT DomainObject,
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformationClass,
    _In_ LPWSTR KeyName,
    _In_ ULONG Rid,
    _Out_ PDISPLAY_ENTRY Entry,
    _Out_ PBOOLEAN Include)
{
    SAM_USER_FIXED_DATA FixedUserData;
    SAM_GROUP_FIXED_DATA FixedGroupData;
    PSAM_DB_OBJECT Object;
    ULONG Size;
    NTSTATUS Status;

    RtlZeroMemory(Entry, sizeof(*Entry));
    *Include = FALSE;

    if (DisplayInformationClass == DomainDisplayGroup)
    {
        Status = SampOpenDbObject(DomainObject,
                                  L"Groups",
                                  KeyName,
                                  Rid,
                                  SamDbGroupObject,
                                  0,
                                  &Object);
        if (!NT_SUCCESS(Status))
            return Status;

        Size = sizeof(SAM_GROUP_FIXED_DATA);
        Status = SampGetObjectAttribute(Object,
                                        L"F",
                                        NULL,
                                        (PVOID)&FixedGroupData,
                                        &Size);
        if (NT_SUCCESS(Status))
        {
            Entry->Control = FixedGroupData.Attributes;
            *Include = TRUE;
        }
    }
    else
    {
        Status = SampOpenDbObject(DomainObject,
                                  L"Users",
                                  KeyName,
                                  Rid,
                                  SamDbUserObject,
                                  0,
                                  &Object);
        if (!NT_SUCCESS(Status))
            return Status;

        Size = sizeof(SAM_USER_FIXED_DATA);
        Status = SampGetObjectAttribute(Object,
                                        L"F",
                                        NULL,
                                        (PVOID)&FixedUserData,
                                        &Size);
        if (NT_SUCCESS(Status))
        {
            Entry->Control = FixedUserData.UserAccountControl;

            if (DisplayInformationClass == DomainDisplayUser)
                *Include = (FixedUserData.UserAccountControl & USER_NORMAL_ACCOUNT) != 0;
            else
                *Include = (FixedUserData.UserAccountControl &
                            (USER_WORKSTATION_TRUST_ACCOUNT | USER_SERVER_TRUST_ACCOUNT)) != 0;
        }
    }

    if (NT_SUCCESS(Status) && *Include)
    {
        Entry->Rid = Rid;

        Status = SampGetObjectAttributeString(Object,
                                              L"Name",
                                              &Entry->AccountName);
        if (NT_SUCCESS(Status))
            Status = SampGetObjectAttributeString(Object,
                                                  L"AdminComment",
                                                  &Entry->AdminComment);

        if (NT_SUCCESS(Status) && DisplayInformationClass == DomainDisplayUser)
            Status = SampGetObjectAttributeString(Object,
                                                  L"FullName",
                                                  &Entry->FullName);

        if (!NT_SUCCESS(Status))
            SampFreeDisplayEntry(Entry);
    }

    SampCloseDbObject(Object);

    return Status;
}


static
NTSTATUS
SampReadDisplayEntries(
    _In_ PSAM_DB_OBJECT DomainObject,
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformationClass,
    _Out_ PDISPLAY_ENTRY *Entries,
    _Out_ PULONG EntryCount)
{
    HANDLE ContainerKeyHandle = NULL;
    WCHAR KeyName[64];
    PDISPLAY_ENTRY List = NULL;
    PDISPLAY_ENTRY NewList;
    ULONG Capacity = 0;
    ULONG Count = 0;
    ULONG EnumIndex;
    ULONG Rid;
    ULONG i;
    BOOLEAN Include;
    NTSTATUS Status;

    *Entries = NULL;
    *EntryCount = 0;

    Status = SampRegOpenKey(DomainObject->KeyHandle,
                            (DisplayInformationClass == DomainDisplayGroup) ? L"Groups" : L"Users",
                            KEY_READ,
                            &ContainerKeyHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    for (EnumIndex = 0; ; EnumIndex++)
    {
        Status = SampRegEnumerateSubKey(ContainerKeyHandle,
                                        EnumIndex,
                                        64 * sizeof(WCHAR),
                                        KeyName);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_NO_MORE_ENTRIES)
                Status = STATUS_SUCCESS;
            break;
        }

        Rid = wcstoul(KeyName, NULL, 16);
        if (Rid == 0 || Rid == ULONG_MAX)
            continue;

        if (Count == Capacity)
        {
            Capacity = (Capacity == 0) ? 16 : Capacity * 2;
            if (List == NULL)
                NewList = RtlAllocateHeap(RtlGetProcessHeap(),
                                          HEAP_ZERO_MEMORY,
                                          Capacity * sizeof(DISPLAY_ENTRY));
            else
                NewList = RtlReAllocateHeap(RtlGetProcessHeap(),
                                            HEAP_ZERO_MEMORY,
                                            List,
                                            Capacity * sizeof(DISPLAY_ENTRY));
            if (NewList == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            List = NewList;
        }

        Status = SampReadDisplayEntry(DomainObject,
                                      DisplayInformationClass,
                                      KeyName,
                                      Rid,
                                      &List[Count],
                                      &Include);
        if (!NT_SUCCESS(Status))
            break;

        if (Include)
            Count++;
    }

    SampRegCloseKey(&ContainerKeyHandle);

    if (!NT_SUCCESS(Status))
    {
        for (i = 0; i < Count; i++)
            SampFreeDisplayEntry(&List[i]);

        if (List != NULL)
            RtlFreeHeap(RtlGetProcessHeap(), 0, List);

        return Status;
    }

    if (Count != 0)
        qsort(List, Count, sizeof(DISPLAY_ENTRY), SampCompareDisplayEntries);

    *Entries = List;
    *EntryCount = Count;

    return STATUS_SUCCESS;
}


static
ULONG
SampDisplayEntrySize(
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformationClass,
    _In_ PDISPLAY_ENTRY Entry)
{
    switch (DisplayInformationClass)
    {
        case DomainDisplayUser:
            return sizeof(SAMPR_DOMAIN_DISPLAY_USER) +
                   Entry->AccountName.MaximumLength +
                   Entry->AdminComment.MaximumLength +
                   Entry->FullName.MaximumLength;

        case DomainDisplayMachine:
            return sizeof(SAMPR_DOMAIN_DISPLAY_MACHINE) +
                   Entry->AccountName.MaximumLength +
                   Entry->AdminComment.MaximumLength;

        default:
            return sizeof(SAMPR_DOMAIN_DISPLAY_GROUP) +
                   Entry->AccountName.MaximumLength +
                   Entry->AdminComment.MaximumLength;
    }
}


NTSTATUS
SampInitializeDisplayCache(VOID)
{
    TRACE("SampInitializeDisplayCache()\n");
    return STATUS_SUCCESS;
}


NTSTATUS
SampShutdownDisplayCache(VOID)
{
    TRACE("SampShutdownDisplayCache()\n");
    return STATUS_SUCCESS;
}


NTSTATUS
SampQueryDisplayInformation(
    _In_ PSAM_DB_OBJECT DomainObject,
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformationClass,
    _In_ ULONG Index,
    _In_ ULONG EntryCount,
    _In_ ULONG PreferredMaximumLength,
    _Out_ PULONG TotalAvailable,
    _Out_ PULONG TotalReturned,
    _Out_ PSAMPR_DISPLAY_INFO_BUFFER Buffer)
{
    PDISPLAY_ENTRY Entries = NULL;
    PDISPLAY_ENTRY Entry;
    PSAMPR_DOMAIN_DISPLAY_USER Users = NULL;
    PSAMPR_DOMAIN_DISPLAY_MACHINE Machines = NULL;
    PSAMPR_DOMAIN_DISPLAY_GROUP Groups = NULL;
    ULONG Count = 0;
    ULONG Returned = 0;
    ULONG Available = 0;
    ULONG Bytes = 0;
    ULONG Size;
    ULONG i;
    NTSTATUS Status;

    TRACE("SampQueryDisplayInformation(%p %lu %lu %lu %lu)\n",
          DomainObject, DisplayInformationClass, Index, EntryCount, PreferredMaximumLength);

    *TotalAvailable = 0;
    *TotalReturned = 0;
    RtlZeroMemory(Buffer, sizeof(*Buffer));

    switch (DisplayInformationClass)
    {
        case DomainDisplayUser:
        case DomainDisplayMachine:
        case DomainDisplayGroup:
            break;

        case DomainDisplayOemUser:
        case DomainDisplayOemGroup:
            return STATUS_NOT_IMPLEMENTED;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    Status = SampReadDisplayEntries(DomainObject,
                                    DisplayInformationClass,
                                    &Entries,
                                    &Count);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < Count; i++)
        Available += SampDisplayEntrySize(DisplayInformationClass, &Entries[i]);

    for (i = Index; i < Count && Returned < EntryCount; i++)
    {
        Size = SampDisplayEntrySize(DisplayInformationClass, &Entries[i]);
        if (Returned != 0 && Bytes + Size > PreferredMaximumLength)
            break;

        Bytes += Size;
        Returned++;
    }

    if (Returned != 0)
    {
        switch (DisplayInformationClass)
        {
            case DomainDisplayUser:
                Users = midl_user_allocate(Returned * sizeof(SAMPR_DOMAIN_DISPLAY_USER));
                if (Users == NULL)
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                break;

            case DomainDisplayMachine:
                Machines = midl_user_allocate(Returned * sizeof(SAMPR_DOMAIN_DISPLAY_MACHINE));
                if (Machines == NULL)
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                break;

            default:
                Groups = midl_user_allocate(Returned * sizeof(SAMPR_DOMAIN_DISPLAY_GROUP));
                if (Groups == NULL)
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < Returned; i++)
        {
            Entry = &Entries[Index + i];

            switch (DisplayInformationClass)
            {
                case DomainDisplayUser:
                    Users[i].Index = Index + i + 1;
                    Users[i].Rid = Entry->Rid;
                    Users[i].AccountControl = Entry->Control;
                    Users[i].AccountName = Entry->AccountName;
                    Users[i].AdminComment = Entry->AdminComment;
                    Users[i].FullName = Entry->FullName;
                    break;

                case DomainDisplayMachine:
                    Machines[i].Index = Index + i + 1;
                    Machines[i].Rid = Entry->Rid;
                    Machines[i].AccountControl = Entry->Control;
                    Machines[i].AccountName = Entry->AccountName;
                    Machines[i].AdminComment = Entry->AdminComment;
                    break;

                default:
                    Groups[i].Index = Index + i + 1;
                    Groups[i].Rid = Entry->Rid;
                    Groups[i].Attributes = Entry->Control;
                    Groups[i].AccountName = Entry->AccountName;
                    Groups[i].AdminComment = Entry->AdminComment;
                    break;
            }

            RtlZeroMemory(Entry, sizeof(*Entry));
        }

        switch (DisplayInformationClass)
        {
            case DomainDisplayUser:
                Buffer->UserInformation.EntriesRead = Returned;
                Buffer->UserInformation.Buffer = Users;
                break;

            case DomainDisplayMachine:
                Buffer->MachineInformation.EntriesRead = Returned;
                Buffer->MachineInformation.Buffer = Machines;
                break;

            default:
                Buffer->GroupInformation.EntriesRead = Returned;
                Buffer->GroupInformation.Buffer = Groups;
                break;
        }

        *TotalAvailable = Available;
        *TotalReturned = Bytes;

        if (Returned != 0 && Index + Returned < Count)
            Status = STATUS_MORE_ENTRIES;
    }

    for (i = 0; i < Count; i++)
        SampFreeDisplayEntry(&Entries[i]);

    if (Entries != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entries);

    return Status;
}

/* EOF */
