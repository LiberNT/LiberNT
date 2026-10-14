/*
 * PROJECT:     LiberNT Boot Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Startup settings and diagnostic boot options
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

static const struct
{
    PCSTR Options;
    PCSTR Remove;
} StartupProfiles[] =
{
    {NULL, NULL},
    {"DEBUG DEBUGPORT=FILE DEBUGPORT=SCREEN", "/NODEBUG/DEBUGPORT="},
    {"DEBUG DEBUGPORT=NET HOST_NAME=libernt.com HOST_PORT=50000", "/NODEBUG/DEBUGPORT=NET/HOST_IP=/HOST_NAME=/HOST_PORT="},
    {"DEBUG DEBUGPORT=FILE DEBUGPORT=SCREEN", "/NODEBUG/DEBUGPORT=/RDPATH=/RDRAMSIZE=/RDIMAGEOFFSET=/RDIMAGELENGTH="}
};

static
BOOLEAN
StartupOptionMatches(
    _In_ PCSTR Option,
    _In_ SIZE_T OptionLength,
    _In_ PCSTR Remove)
{
    PCSTR Name, End;
    SIZE_T Length;

    for (Name = Remove; *Name == '/'; Name = End)
    {
        Name++;
        End = strchr(Name, '/');
        if (!End)
            End = Name + strlen(Name);

        Length = End - Name;
        if (Length == 0 || Length > OptionLength)
            continue;

        if (_strnicmp(Option, Name, Length) == 0 &&
            (Length == OptionLength || Name[Length - 1] == '=' || Name[Length - 1] == ':'))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
StartupUpdateOptions(
    _Inout_updates_z_(Size) PSTR Options,
    _In_ SIZE_T Size,
    _In_opt_ PCSTR Add,
    _In_opt_ PCSTR Remove)
{
    PSTR Read = Options;
    PSTR Write = Options;
    PSTR End;

    while (Remove && *Read)
    {
        while (*Read == ' ')
            Read++;

        End = Read;
        while (*End && *End != ' ')
            End++;

        if (End != Read && !StartupOptionMatches(Read, End - Read, Remove))
        {
            if (Write != Options)
                *Write++ = ' ';

            RtlMoveMemory(Write, Read, End - Read);
            Write += End - Read;
        }

        Read = End;
    }

    if (Remove)
        *Write = ANSI_NULL;

    if (Add)
    {
        if (Options[0] != ANSI_NULL)
            RtlStringCbCatA(Options, Size, " ");

        RtlStringCbCatA(Options, Size, Add);
    }
}

VOID
AppendBootTimeOptions(
    _Inout_updates_z_(BootOptionsSize) PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize,
    _In_ PBM_STARTUP Startup)
{
    static const PCSTR Modes[] =
    {
        NULL,
        "SAFEBOOT:MINIMAL NOGUIBOOT",
        "SAFEBOOT:NETWORK NOGUIBOOT",
        "SAFEBOOT:MINIMAL(ALTERNATESHELL) NOGUIBOOT"
    };

    if (BootOptionsSize < sizeof(CHAR))
        return;

    if (Startup->Profile != 0 && Startup->Profile < RTL_NUMBER_OF(StartupProfiles))
    {
        StartupUpdateOptions(BootOptions,
                             BootOptionsSize,
                             StartupProfiles[Startup->Profile].Options,
                             StartupProfiles[Startup->Profile].Remove);
    }

    if (Startup->Mode > NO_OPTION && Startup->Mode < RTL_NUMBER_OF(Modes))
        StartupUpdateOptions(BootOptions, BootOptionsSize, Modes[Startup->Mode], "/SAFEBOOT/SAFEBOOT:");

    if ((Startup->Flags & BOOT_LOGGING) || Startup->Mode != NO_OPTION)
        StartupUpdateOptions(BootOptions, BootOptionsSize, "BOOTLOG", NULL);

    if (Startup->Flags & BOOT_VGA_MODE)
        StartupUpdateOptions(BootOptions, BootOptionsSize, "BASEVIDEO", NULL);

    if (Startup->Flags & BOOT_DEBUGGING)
        StartupUpdateOptions(BootOptions, BootOptionsSize, "DEBUG", "/NODEBUG");

    if (Startup->Flags & BOOT_NO_INTEGRITY_CHECKS)
        StartupUpdateOptions(BootOptions, BootOptionsSize, "DISABLE_INTEGRITY_CHECKS", "/DISABLE_INTEGRITY_CHECKS");
}
