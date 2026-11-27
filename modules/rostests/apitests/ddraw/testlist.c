#define STANDALONE
#include <wine/test.h>

extern void func_AvailableVidMem(void);
extern void func_DirectDrawCreate(void);
extern void func_DllGetClassObject(void);
extern void func_EnumDisplayModes(void);
extern void func_GetDeviceIdentifier(void);
extern void func_GetGDISurface(void);
extern void func_PaletteEntries(void);
extern void func_PaletteFlags(void);
extern void func_PrimaryBltLatency(void);
extern void func_QueryInterface(void);
extern void func_VerticalBlank(void);

const struct test winetest_testlist[] =
{
    { "AvailableVidMem", func_AvailableVidMem },
    { "DirectDrawCreate", func_DirectDrawCreate },
    { "DllGetClassObject", func_DllGetClassObject },
    { "EnumDisplayModes", func_EnumDisplayModes },
    { "GetDeviceIdentifier", func_GetDeviceIdentifier },
    { "GetGDISurface", func_GetGDISurface },
    { "PaletteEntries", func_PaletteEntries },
    { "PaletteFlags", func_PaletteFlags },
    { "PrimaryBltLatency", func_PrimaryBltLatency },
    { "QueryInterface", func_QueryInterface },
    { "VerticalBlank", func_VerticalBlank },

    { 0, 0 }
};
