/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Utils source
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern volatile BOOLEAN BootServicesExitedFlag;

/* FUNCTIONS ******************************************************************/

TIMEINFO*
UefiGetTime(VOID)
{
    static TIMEINFO TimeInfo;
    static BOOLEAN ClockUnavailable;
    static EFI_EVENT SecondTimer;
    static ULONG Seconds;
    EFI_BOOT_SERVICES *Services = GlobalSystemTable->BootServices;
    EFI_STATUS Status;
    EFI_TIME time = {0};

    if (!ClockUnavailable)
    {
        Status = GlobalSystemTable->RuntimeServices->GetTime(&time, NULL);
        if (Status == EFI_SUCCESS)
        {
            TimeInfo.Year = time.Year;
            TimeInfo.Month = time.Month;
            TimeInfo.Day = time.Day;
            TimeInfo.Hour = time.Hour;
            TimeInfo.Minute = time.Minute;
            TimeInfo.Second = time.Second;
            return &TimeInfo;
        }

        ERR("UefiGetTime: cannot get time status %d, counting seconds with a timer\n", Status);
        ClockUnavailable = TRUE;
        if (BootServicesExitedFlag ||
            EFI_ERROR(Services->CreateEvent(EVT_TIMER, TPL_APPLICATION, NULL, NULL, &SecondTimer)))
        {
            SecondTimer = NULL;
        }
        else if (EFI_ERROR(Services->SetTimer(SecondTimer, TimerPeriodic, 10000000)))
        {
            Services->CloseEvent(SecondTimer);
            SecondTimer = NULL;
        }
    }

    if (SecondTimer && !BootServicesExitedFlag && Services->CheckEvent(SecondTimer) == EFI_SUCCESS)
        ++Seconds;

    TimeInfo.Year = 0;
    TimeInfo.Month = 0;
    TimeInfo.Day = 0;
    TimeInfo.Hour = (USHORT)((Seconds / 3600) % 24);
    TimeInfo.Minute = (USHORT)((Seconds / 60) % 60);
    TimeInfo.Second = (USHORT)(Seconds % 60);
    return &TimeInfo;
}


typedef struct _FREELDR_EFI_RNG_PROTOCOL FREELDR_EFI_RNG_PROTOCOL;

struct _FREELDR_EFI_RNG_PROTOCOL
{
    EFI_STATUS (EFIAPI *GetInfo)(FREELDR_EFI_RNG_PROTOCOL *This, UINTN *Size, EFI_GUID *List);
    EFI_STATUS (EFIAPI *GetRNG)(FREELDR_EFI_RNG_PROTOCOL *This, EFI_GUID *Algorithm, UINTN Size, UINT8 *Value);
};

BOOLEAN
UefiGetRandom(
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    static EFI_GUID RngGuid = {0x3152bca5, 0xeade, 0x433d, {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}};
    FREELDR_EFI_RNG_PROTOCOL *Rng;

    if (EFI_ERROR(GlobalSystemTable->BootServices->LocateProtocol(&RngGuid, NULL, (VOID**)&Rng)))
        return FALSE;

    return !EFI_ERROR(Rng->GetRNG(Rng, NULL, Size, Buffer));
}
