/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test SetupDiGetDeviceInterfaceAlias
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdlib.h>

static const GUID RenderCategory = { STATIC_KSCATEGORY_RENDER };
static const GUID AudioCategory = { STATIC_KSCATEGORY_AUDIO };
static const GUID DiskInterface = { 0x53f56307L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };

static PSP_DEVICE_INTERFACE_DETAIL_DATA_W
GetInterfacePath(HDEVINFO Set, PSP_DEVICE_INTERFACE_DATA Interface)
{
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    DWORD Size = 0;

    SetupDiGetDeviceInterfaceDetailW(Set, Interface, NULL, 0, &Size, NULL);
    if (!Size)
        return NULL;
    Detail = malloc(Size);
    if (!Detail)
        return NULL;
    Detail->cbSize = sizeof(*Detail);
    if (!SetupDiGetDeviceInterfaceDetailW(Set, Interface, Detail, Size, NULL, NULL))
    {
        free(Detail);
        return NULL;
    }
    return Detail;
}

static VOID
CheckParameters(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface, Alias;
    HDEVINFO Set;
    BOOL Ret;

    Interface.cbSize = sizeof(Interface);
    Alias.cbSize = sizeof(Alias);
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(INVALID_HANDLE_VALUE, &Interface, &AudioCategory, &Alias);
    ok(!Ret, "Alias of an invalid set succeeded\n");
    ok_err(ERROR_INVALID_HANDLE);

    Set = SetupDiGetClassDevsW(&DiskInterface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    if (!SetupDiEnumDeviceInterfaces(Set, NULL, &DiskInterface, 0, &Interface))
    {
        skip("No disk interface is present\n");
        SetupDiDestroyDeviceInfoList(Set);
        return;
    }

    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, NULL, &AudioCategory, &Alias);
    ok(!Ret, "Alias of no interface succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);

    Interface.cbSize = 0;
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Alias);
    ok(!Ret, "Alias of an interface with a bad size succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Interface.cbSize = sizeof(Interface);

    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, NULL, &Alias);
    ok(!Ret, "Alias without a class succeeded\n");
    ok_err(ERROR_INVALID_USER_BUFFER);

    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, NULL);
    ok(!Ret, "Alias without output succeeded\n");
    ok_err(ERROR_NO_SUCH_DEVICE_INTERFACE);

    Alias.cbSize = 0;
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Alias);
    ok(!Ret, "Alias with a bad output size succeeded\n");
    ok_err(ERROR_NO_SUCH_DEVICE_INTERFACE);

    Alias.cbSize = sizeof(Alias);
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Alias);
    ok(!Ret, "A disk interface has an audio alias\n");
    ok_err(ERROR_NO_SUCH_DEVICE_INTERFACE);

    ZeroMemory(&Alias, sizeof(Alias));
    Alias.cbSize = sizeof(Alias);
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetDeviceInterfaceAlias(Set, &Interface, &DiskInterface, &Alias);
    ok(Ret, "Alias in the interface's own class failed with 0x%lx\n", GetLastError());
    ok(IsEqualGUID(&Alias.InterfaceClassGuid, &DiskInterface), "Alias in the own class has another class\n");
    ok(Alias.Reserved == Interface.Reserved, "Alias in the own class is another interface\n");
    ok(Alias.Flags & SPINT_ACTIVE, "Alias in the own class has flags 0x%lx\n", Alias.Flags);

    SetupDiDestroyDeviceInfoList(Set);
}

static VOID
CheckConfigManagerAlias(PCWSTR Path, PCWSTR AliasPath)
{
    WCHAR Buffer[1024];
    ULONG Length, Needed = (ULONG)wcslen(AliasPath) + 1;
    CONFIGRET Cr;

    Length = ARRAYSIZE(Buffer);
    Cr = CM_Get_Device_Interface_AliasW(Path, (LPGUID)&AudioCategory, Buffer, &Length, 0);
    ok(Cr == CR_SUCCESS, "CM_Get_Device_Interface_AliasW returned 0x%lx\n", Cr);
    ok(Length == Needed, "Length %lu, expected %lu\n", Length, Needed);
    ok(Cr != CR_SUCCESS || _wcsicmp(Buffer, AliasPath) == 0, "Alias %ls, expected %ls\n", Buffer, AliasPath);

    Length = 5;
    Cr = CM_Get_Device_Interface_AliasW(Path, (LPGUID)&AudioCategory, Buffer, &Length, 0);
    ok(Cr == CR_BUFFER_SMALL, "CM_Get_Device_Interface_AliasW with a small buffer returned 0x%lx\n", Cr);
    ok(Length == Needed, "Length %lu after a small buffer, expected %lu\n", Length, Needed);

    Length = ARRAYSIZE(Buffer);
    Cr = CM_Get_Device_Interface_AliasW(Path, (LPGUID)&DiskInterface, Buffer, &Length, 0);
    ok(Cr == CR_NO_SUCH_DEVICE_INTERFACE, "CM_Get_Device_Interface_AliasW without an alias returned 0x%lx\n", Cr);
    ok(Length == 0, "Length %lu without an alias\n", Length);
}

static VOID
CheckRenderAliases(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface, Alias, Back;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Path, AliasPath, BackPath;
    HDEVINFO Set;
    DWORD Index;
    UINT Checked = 0;

    Set = SetupDiGetClassDevsW(&RenderCategory, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    for (Index = 0; ; Index++)
    {
        Interface.cbSize = sizeof(Interface);
        if (!SetupDiEnumDeviceInterfaces(Set, NULL, &RenderCategory, Index, &Interface))
            break;
        Alias.cbSize = sizeof(Alias);
        if (!SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Alias))
            continue;
        Checked++;

        SetLastError(0xdeadbeef);
        ok(!SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, NULL), "Alias %lu without output succeeded\n", Index);
        ok_err(ERROR_INVALID_USER_BUFFER);
        Back.cbSize = 0;
        SetLastError(0xdeadbeef);
        ok(!SetupDiGetDeviceInterfaceAlias(Set, &Interface, &AudioCategory, &Back), "Alias %lu with a bad output size succeeded\n", Index);
        ok_err(ERROR_INVALID_USER_BUFFER);
        ok(IsEqualGUID(&Alias.InterfaceClassGuid, &AudioCategory), "Alias %lu has another class\n", Index);
        ok(Alias.Flags & SPINT_ACTIVE, "Alias %lu has flags 0x%lx\n", Index, Alias.Flags);

        Back.cbSize = sizeof(Back);
        ok(SetupDiGetDeviceInterfaceAlias(Set, &Alias, &RenderCategory, &Back),
           "Alias %lu has no render alias, error %lu\n", Index, GetLastError());
        ok(Back.Reserved == Interface.Reserved, "Alias %lu leads back to another interface\n", Index);
        Path = GetInterfacePath(Set, &Interface);
        AliasPath = GetInterfacePath(Set, &Alias);
        BackPath = GetInterfacePath(Set, &Back);
        ok(Path && AliasPath && BackPath, "Interface %lu has no path\n", Index);
        if (Path && AliasPath && BackPath)
        {
            CheckConfigManagerAlias(Path->DevicePath, AliasPath->DevicePath);
            ok(_wcsicmp(Path->DevicePath, AliasPath->DevicePath) != 0, "Alias %lu is the interface itself\n", Index);
            ok(_wcsicmp(Path->DevicePath, BackPath->DevicePath) == 0, "Alias %lu leads back to %ls, not %ls\n",
               Index, BackPath->DevicePath, Path->DevicePath);
        }
        free(Path);
        free(AliasPath);
        free(BackPath);
    }

    SetupDiDestroyDeviceInfoList(Set);
    if (!Checked)
        skip("No audio render filter is present\n");
}

START_TEST(DeviceInterfaceAlias)
{
    CheckParameters();
    CheckRenderAliases();
}
