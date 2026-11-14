/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Setup Library
 * FILE:            base/setup/lib/install.c
 * PURPOSE:         Installation functions
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

#define UPGRADE_INVENTORY_PATH  L"servicing\\LiberNT.files"

typedef enum _FILE_COPY_STATUS
{
    None = 0,
    // Success = 0,
} FILE_COPY_STATUS;

typedef VOID
(__cdecl *PFILE_COPY_STATUS_ROUTINE)(IN FILE_COPY_STATUS, ...);

BOOLEAN // ERROR_NUMBER
NTAPI
PrepareFileCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PFILE_COPY_STATUS_ROUTINE StatusRoutine OPTIONAL);

BOOLEAN
NTAPI
PrepareUpgradeFileCopy(
    _Inout_ PUSETUP_DATA pSetupData,
    _In_opt_ PFILE_COPY_STATUS_ROUTINE StatusRoutine);

typedef VOID
(NTAPI *PUPGRADE_TARGET_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ PCWSTR Target,
    _In_ BOOLEAN Directory);

NTSTATUS
NTAPI
EnumerateUpgradeTargets(
    _In_ PUSETUP_DATA pSetupData,
    _In_ PUPGRADE_TARGET_ROUTINE Routine,
    _In_opt_ PVOID Context);

BOOLEAN
NTAPI
DoFileCopy(
    IN OUT PUSETUP_DATA pSetupData,
    IN PSP_FILE_CALLBACK_W MsgHandler,
    IN PVOID Context OPTIONAL);

/* EOF */
