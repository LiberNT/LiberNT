/*
 * PROJECT:     LiberNT Time Zone Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Time zone registry helpers shared by setup and the control panel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _REG_TZI_FORMAT
{
    LONG Bias;
    LONG StandardBias;
    LONG DaylightBias;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
} REG_TZI_FORMAT, *PREG_TZI_FORMAT;

typedef LONG
(*PENUM_TIMEZONE_CALLBACK)(
    _In_ HKEY hZoneKey,
    _In_opt_ PVOID Context);

BOOL
GetTimeZoneListIndex(
    _Inout_ PULONG pIndex);

LONG
QueryTimeZoneData(
    _In_ HKEY hZoneKey,
    _Out_opt_ PULONG Index,
    _Out_ PREG_TZI_FORMAT TimeZoneInfo,
    _Out_opt_ PWCHAR Description,
    _Inout_opt_ PULONG DescriptionSize,
    _Out_opt_ PWCHAR StandardName,
    _Inout_opt_ PULONG StandardNameSize,
    _Out_opt_ PWCHAR DaylightName,
    _Inout_opt_ PULONG DaylightNameSize);

VOID
EnumerateTimeZoneList(
    _In_ PENUM_TIMEZONE_CALLBACK Callback,
    _In_opt_ PVOID Context);

BOOL
GetAutoDaylight(VOID);

VOID
SetAutoDaylight(
    _In_ BOOL EnableAutoDaylightTime);
