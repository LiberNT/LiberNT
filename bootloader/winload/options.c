/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel load options from a BCD OS loader entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

static const struct
{
    ULONG Element;
    PCWSTR Option;
} OslBooleanOptions[] =
{
    {BcdOSLoaderBoolean_DetectKernelAndHal,    L" DETECTHAL"},
    {BcdOSLoaderBoolean_WinPEMode,             L" MININT"},
    {BcdOSLoaderBoolean_UseLastGoodSettings,   L" LASTKNOWNGOOD"},
    {BcdOSLoaderBoolean_UseVgaDriver,          L" BASEVIDEO"},
    {BcdOSLoaderBoolean_DisableBootDisplay,    L" NOGUIBOOT"},
    {BcdOSLoaderBoolean_UseBootProcessorOnly,  L" ONECPU"},
    {BcdOSLoaderBoolean_BootLogInitialization, L" BOOTLOG"},
    {BcdOSLoaderBoolean_VerboseObjectLoadMode, L" SOS"},
    {BcdOSLoaderBoolean_EmsEnabled,            L" REDIRECT"},
};

static const PCWSTR OslSafeBootModes[] =
{
    L"MINIMAL",
    L"NETWORK",
    L"DSREPAIR",
};

static
VOID
OslAppend(
    _Inout_ POSL_PARAMETERS Parameters,
    _In_ PCWSTR Format,
    ...)
{
    SIZE_T Length = wcslen(Parameters->LoadOptions);
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Parameters->LoadOptions + Length,
                         RTL_NUMBER_OF(Parameters->LoadOptions) - Length,
                         Format,
                         Arguments);
    va_end(Arguments);
}

BOOLEAN
OslGetParameters(
    _In_ PBM_ENTRY Entry,
    _Out_ POSL_PARAMETERS Parameters)
{
    PBCD_OBJECT Object = &Entry->Object;
    WCHAR Text[BM_PATH_CHARS];
    ULONGLONG Integer;
    BOOLEAN HasLoadOptions;
    BOOLEAN Enabled;
    ULONG i;

    if (!BcdGetDevice(Object, BcdOSLoaderDevice_OSDevice, &Parameters->OsDevice) ||
        !BcdGetString(Object,
                      BcdOSLoaderString_SystemRoot,
                      Parameters->SystemRoot,
                      RTL_NUMBER_OF(Parameters->SystemRoot)))
    {
        return FALSE;
    }

    Parameters->LoadOptions[0] = UNICODE_NULL;

    if (!BcdGetString(Object,
                      BcdOSLoaderString_KernelPath,
                      Parameters->KernelFile,
                      RTL_NUMBER_OF(Parameters->KernelFile)))
    {
        RtlStringCchCopyW(Parameters->KernelFile, RTL_NUMBER_OF(Parameters->KernelFile), L"ntoskrnl.exe");
    }

    if (!BcdGetString(Object,
                      BcdOSLoaderString_HalPath,
                      Parameters->HalFile,
                      RTL_NUMBER_OF(Parameters->HalFile)))
    {
        RtlStringCchCopyW(Parameters->HalFile, RTL_NUMBER_OF(Parameters->HalFile), L"hal.dll");
    }

    for (i = 0; i < RTL_NUMBER_OF(OslBooleanOptions); i++)
    {
        if (BcdGetBoolean(Object, OslBooleanOptions[i].Element, &Enabled) && Enabled)
            OslAppend(Parameters, L"%s", OslBooleanOptions[i].Option);
    }

    if (BcdGetInteger(Object, BcdOSLoaderInteger_NumberOfProcessors, &Integer))
        OslAppend(Parameters, L" NUMPROC=%I64u", Integer);

    if (BcdGetInteger(Object, BcdOSLoaderInteger_SafeBoot, &Integer) && Integer < RTL_NUMBER_OF(OslSafeBootModes))
    {
        OslAppend(Parameters, L" SAFEBOOT:%s", OslSafeBootModes[Integer]);
        if (BcdGetBoolean(Object, BcdOSLoaderBoolean_SafeBootAlternateShell, &Enabled) && Enabled)
            OslAppend(Parameters, L"(ALTERNATESHELL)");
    }

    HasLoadOptions = BcdGetString(Object, BcdLibraryString_LoadOptionsString, Text, RTL_NUMBER_OF(Text));

    if (BcdGetBoolean(Object, BcdOSLoaderBoolean_KernelDebuggerEnabled, &Enabled) && Enabled)
    {
        OslAppend(Parameters, L" DEBUG");

        if (!(HasLoadOptions && wcsstr(Text, L"DEBUGPORT=") != NULL))
        {
            if (BcdGetInteger(Object, BcdLibraryInteger_SerialDebuggerPort, &Integer))
                OslAppend(Parameters, L" DEBUGPORT=COM%I64u", Integer);

            if (BcdGetInteger(Object, BcdLibraryInteger_SerialDebuggerBaudRate, &Integer))
                OslAppend(Parameters, L" BAUDRATE=%I64u", Integer);
        }
    }

    if (HasLoadOptions)
        OslAppend(Parameters, L" %s", Text);

    return TRUE;
}
