/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for imagelist
 * COPYRIGHT:   Copyright 2024 Whindmar Saksit <whindsaks@proton.me>
 */

#include "wine/test.h"
#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include <initguid.h>
#include <commoncontrols.h>
#include <comctl32_undoc.h>

#define WinVerMajor() LOBYTE(GetVersion())

#define ILC_COLORMASK 0xfe
#define IL_IMGSIZE 16

static BOOL IL_IsValid(HIMAGELIST himl)
{
    int w = -42, h;
    if (!himl || IsBadReadPtr(himl, sizeof(void*)))
        return FALSE;
    return ImageList_GetIconSize(himl, &w, &h) && w != -42;
}

static HRESULT IL_Destroy(HIMAGELIST himl)
{
    if (himl && !IL_IsValid(himl))
        return E_INVALIDARG;
    return ImageList_Destroy(himl) ? S_OK : S_FALSE;
}

static inline HIMAGELIST IL_Create(UINT flags)
{
    return ImageList_Create(IL_IMGSIZE, IL_IMGSIZE, flags, 1, 0);
}

static UINT IL_CalculateOtherBpp(UINT ilc)
{
    UINT bpp = (ilc & ILC_COLORMASK) == ILC_COLOR32 ? ILC_COLOR16 : ILC_COLOR32;
    return (ilc & ~ILC_COLORMASK) | bpp;
}

static BOOL IL_AddImagesForTest(HIMAGELIST himl)
{
    int idx = -1;
    HINSTANCE hInst = LoadLibraryW(L"USER32");
    if (!hInst)
        return FALSE;
    HICON hIco = (HICON)LoadImage(hInst, MAKEINTRESOURCE(100),
                                  IMAGE_ICON, IL_IMGSIZE, IL_IMGSIZE, 0);
    if (hIco)
    {
        idx = ImageList_AddIcon(himl, hIco);
        DestroyIcon(hIco);
    }
    FreeLibrary(hInst);
    return idx != -1;
}

static void Test_SystemIL(void)
{
    const UINT flags = ILC_COLOR16 | ILC_MASK;
    HIMAGELIST himl;

    himl = IL_Create(flags);
    ok(IL_Destroy(himl) == S_OK && !IL_IsValid(himl), "Can destroy normal\n");

    /* You can (sometimes) destroy a system imagelist!
     * On Win9x it destroys it for all processes according to
     * https://sporks.space/2021/09/18/notes-on-the-system-image-list/ and
     * https://www.catch22.net/tuts/win32/system-image-list/
     */
    himl = IL_Create(flags | ILC_SYSTEM);
    if (WinVerMajor() >= 6)
        ok(IL_Destroy(himl) == S_FALSE && IL_IsValid(himl), "Can't destroy system\n");
    else
        ok(IL_Destroy(himl) == S_OK && !IL_IsValid(himl), "Can destroy system\n");
}

static void Test_Flags(void)
{
    const UINT flags = ILC_COLOR16 | ILC_MASK;
    UINT flagsIn, flagsOut;
    HIMAGELIST himl;

    himl = IL_Create(flagsIn = flags);
    flagsOut = ImageList_GetFlags(himl);
    if (himl ? TRUE : (skip("Could not initialize\n"), FALSE))
    {
        ok((flagsOut & ILC_COLORMASK) == (flagsIn & ILC_COLORMASK), "ILC_COLOR\n");
        ok(!(flagsOut & ILC_SYSTEM), "!ILC_SYSTEM\n");

        ok(IL_AddImagesForTest(himl), "Initialize\n");
        flagsIn = IL_CalculateOtherBpp(flagsIn);
        ok(ImageList_SetFlags(himl, flagsIn), "Can change BPP\n");
        ok(ImageList_GetImageCount(himl) == 0, "SetFlags deletes all images\n");

        ok(IL_AddImagesForTest(himl), "Initialize\n");
        ok(ImageList_SetFlags(himl, ImageList_GetFlags(himl)), "Can set same flags\n");
        if (WinVerMajor() >= 6)
        {
            ok(ImageList_GetImageCount(himl) != 0, "SetFlags does not delete with same flags\n");
            ok(ImageList_SetFlags(himl, flagsIn ^ ILC_SYSTEM), "Can change ILC_SYSTEM\n");
        }
        else
        {
            ok(ImageList_GetImageCount(himl) == 0, "SetFlags deletes all images even with same flags\n");
            ok(!ImageList_SetFlags(himl, flagsIn ^ ILC_SYSTEM), "Can't change ILC_SYSTEM\n");
        }

        IL_Destroy(himl);
    }

    himl = IL_Create(flagsIn = flags | ILC_SYSTEM);
    flagsOut = ImageList_GetFlags(himl);
    if (himl ? TRUE : (skip("Could not initialize\n"), FALSE))
    {
        ok((flagsOut & ILC_SYSTEM), "ILC_SYSTEM\n"); /* Flag is not hidden */

        ok(IL_AddImagesForTest(himl), "Initialize\n");

        flagsIn = IL_CalculateOtherBpp(flagsIn);
        ok(ImageList_SetFlags(himl, flagsIn), "Can change BPP\n");
        ok(ImageList_GetImageCount(himl) == 0, "SetFlags deletes all images\n");

        ok(IL_AddImagesForTest(himl), "Initialize\n");
        ok(ImageList_SetFlags(himl, ImageList_GetFlags(himl)), "Can set same flags\n");
        if (WinVerMajor() >= 6)
        {
            ok(ImageList_SetFlags(himl, flagsIn ^ ILC_SYSTEM), "Can change ILC_SYSTEM\n");
        }
        else
        {
            ok(!ImageList_SetFlags(himl, flagsIn ^ ILC_SYSTEM), "Can't change ILC_SYSTEM\n");
        }

        IL_Destroy(himl);
    }
}

static void IL_GetBits(HBITMAP hbm, int index, DWORD *bits)
{
    BITMAPINFO bmi = {{ sizeof(BITMAPINFOHEADER) }};
    DWORD buffer[16] = { 0 };
    HDC hdc = CreateCompatibleDC(NULL);
    BITMAP bm;

    GetObjectW(hbm, sizeof(bm), &bm);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    GetDIBits(hdc, hbm, 0, bm.bmHeight, buffer, &bmi, DIB_RGB_COLORS);
    bits[0] = buffer[index * 2];
    bits[1] = buffer[index * 2 + 1];
    DeleteDC(hdc);
}

static void IL_AddBits(HIMAGELIST himl, DWORD bits0, DWORD bits1, HBITMAP hbmMask)
{
    BITMAPINFO bmi = {{ sizeof(BITMAPINFOHEADER), 2, -1, 1, 32, BI_RGB }};
    DWORD *bits;
    HBITMAP hbm = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);

    bits[0] = bits0;
    bits[1] = bits1;
    ok(ImageList_Add(himl, hbm, hbmMask) != -1, "ImageList_Add failed\n");
    DeleteObject(hbm);
}

static void IL_CheckMerge(HIMAGELIST himl, int i1, int i2, DWORD expected0, DWORD expected1)
{
    HIMAGELIST merged = ImageList_Merge(himl, i1, himl, i2, 0, 0);
    IImageList2 *list;
    IMAGEINFO info;
    DWORD bits[2], flags = 0;

    ok(merged != NULL, "ImageList_Merge(%d, %d) failed\n", i1, i2);
    if (!merged)
        return;
    ImageList_GetImageInfo(merged, 0, &info);
    IL_GetBits(info.hbmImage, 0, bits);
    ok(bits[0] == expected0 && bits[1] == expected1, "Merge(%d, %d): got %08lX %08lX, expected %08lX %08lX\n",
       i1, i2, bits[0], bits[1], expected0, expected1);
    if (HIMAGELIST_QueryInterface(merged, &IID_IImageList2, (void **)&list) == S_OK)
    {
        list->lpVtbl->GetItemFlags(list, 0, &flags);
        list->lpVtbl->Release(list);
    }
    ok(flags == ILIF_ALPHA, "Merge(%d, %d): got flags %#lx\n", i1, i2, flags);
    ImageList_Destroy(merged);
}

static void Test_Alpha(void)
{
    static const BYTE mask_bits[2] = { 0x40, 0 };
    HIMAGELIST himl = ImageList_Create(2, 1, ILC_COLOR32 | ILC_MASK, 0, 4);
    HBITMAP hbmMask = CreateBitmap(2, 1, 1, 1, mask_bits);
    ICONINFO icon_info;
    IMAGEINFO info;
    DWORD bits[2];
    HICON icon;

    IL_AddBits(himl, 0x89ABCDEF, 0xFF123456, NULL);
    IL_AddBits(himl, 0x00112233, 0x00445566, hbmMask);

    ImageList_GetImageInfo(himl, 0, &info);
    IL_GetBits(info.hbmImage, 0, bits);
    ok(bits[0] == 0x895C6E80 && bits[1] == 0xFF123456, "Stored %08lX %08lX\n", bits[0], bits[1]);

    icon = ImageList_GetIcon(himl, 0, ILD_NORMAL);
    ok(icon != NULL, "ImageList_GetIcon failed\n");
    if (icon && GetIconInfo(icon, &icon_info))
    {
        IL_GetBits(icon_info.hbmColor, 0, bits);
        ok(bits[0] == 0x89ABCCEE && bits[1] == 0xFF123456, "Icon %08lX %08lX\n", bits[0], bits[1]);
        DeleteObject(icon_info.hbmColor);
        DeleteObject(icon_info.hbmMask);
    }
    DestroyIcon(icon);

    IL_CheckMerge(himl, 0, 0, 0xC887A1BB, 0xFF123456);
    IL_CheckMerge(himl, 1, 0, 0x89647E98, 0xFF123456);

    DeleteObject(hbmMask);
    ImageList_Destroy(himl);
}

START_TEST(ImageListApi)
{
    InitCommonControls();
    Test_SystemIL();
    Test_Flags();
    Test_Alpha();
}
