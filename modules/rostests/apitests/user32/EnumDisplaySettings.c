
#include "precomp.h"

#define SIZEOF_DEVMODEW_300_W 188
#define SIZEOF_DEVMODEW_400_W 212
#define SIZEOF_DEVMODEW_500_W 220

#define SIZEOF_DEVMODEW_300_A 124
#define SIZEOF_DEVMODEW_400_A 148
#define SIZEOF_DEVMODEW_500_A 156

static void
TestDisplayDevices(void)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Mode;
    DWORD Index;
    BOOL Current;

    for (Index = 0; ; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        Current = EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Mode);
        if (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
        {
            ok(Current, "%ls: no current settings (%lu)\n", Device.DeviceName, GetLastError());
            ok(Mode.dmPelsWidth != 0, "%ls: attached with width 0\n", Device.DeviceName);
            ok(Mode.dmPelsHeight != 0, "%ls: attached with height 0\n", Device.DeviceName);
        }
        else
        {
            ok(!Current, "%ls: detached with current settings %lux%lu\n", Device.DeviceName, Mode.dmPelsWidth,
               Mode.dmPelsHeight);
        }
    }
    ok(Index != 0, "no display device\n");
}

START_TEST(EnumDisplaySettings)
{
    DEVMODEW dmW;
    DEVMODEA dmA;
    HDC icDisplay;

    /* TODO: test with a printer driver */

    icDisplay = CreateICW(L"DISPLAY", NULL, NULL, NULL);
    ok(icDisplay != NULL, "\n");

    dmW.dmDriverExtra = 0x7777;

    /* Try ridiculous size */
    dmW.dmSize = 0x8888;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);
    ok(dmW.dmDriverExtra == 0, "%d\n", dmW.dmDriverExtra);

    /* Try a tiny size */
    dmW.dmSize = 4;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);

    /* Something in between */
    dmW.dmSize = (SIZEOF_DEVMODEW_300_W + SIZEOF_DEVMODEW_400_W) / 2 ;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);

    /* WINVER < 0x0400 */
    dmW.dmSize = SIZEOF_DEVMODEW_300_W;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);

    /* WINVER < 0x0500 */
    dmW.dmSize = SIZEOF_DEVMODEW_400_W;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);

    /* "Modern" windows */
    dmW.dmSize = SIZEOF_DEVMODEW_500_W;
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dmW), "\n");
    ok(dmW.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmW.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmW.dmSize == SIZEOF_DEVMODEW_300_W, "%d\n", dmW.dmSize);

    DeleteDC(icDisplay);

    icDisplay = CreateICA("DISPLAY", NULL, NULL, NULL);
    ok(icDisplay != NULL, "\n");

    dmA.dmDriverExtra = 0x7777;

    /* Try ridiculous size */
    dmA.dmSize = 0x8888;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);
    ok(dmA.dmDriverExtra == 0, "%d\n", dmA.dmDriverExtra);

    /* Try a tiny size */
    dmA.dmSize = 4;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);

    /* Something in between */
    dmA.dmSize = (SIZEOF_DEVMODEW_300_A + SIZEOF_DEVMODEW_400_A) / 2 ;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);

    /* WINVER < 0x0400 */
    dmA.dmSize = SIZEOF_DEVMODEW_300_A;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);

    /* WINVER < 0x0500 */
    dmA.dmSize = SIZEOF_DEVMODEW_400_A;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);

    /* "Modern" windows */
    dmA.dmSize = SIZEOF_DEVMODEW_500_A;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dmA), "\n");
    ok(dmA.dmBitsPerPel == GetDeviceCaps(icDisplay, BITSPIXEL), "%lu, should be %d.\n", dmA.dmBitsPerPel, GetDeviceCaps(icDisplay, BITSPIXEL));
    ok(dmA.dmSize == SIZEOF_DEVMODEW_300_A, "%d\n", dmA.dmSize);

    DeleteDC(icDisplay);

    TestDisplayDevices();
}
