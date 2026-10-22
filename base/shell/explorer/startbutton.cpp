/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Create the Start button's state images at the taskbar's actual DPI
 */

#include "precomp.h"
#include <wincodec.h>

HBITMAP CreateStartOrbBitmap(INT Size, UINT ResourceId)
{
    /* Bound the destination DIB allocation. */
    if (Size <= 0 || Size > 4096)
        return NULL;

    HRSRC hResource = FindResourceW(hExplorerInstance, MAKEINTRESOURCEW(ResourceId), RT_RCDATA);
    if (!hResource)
        return NULL;
    HGLOBAL hData = LoadResource(hExplorerInstance, hResource);
    const BYTE *Data = (const BYTE *)LockResource(hData);
    DWORD DataSize = SizeofResource(hExplorerInstance, hResource);
    if (!Data || !DataSize)
        return NULL;

    CComPtr<IStream> Stream;
    Stream.Attach(SHCreateMemStream(Data, DataSize));
    if (!Stream)
        return NULL;

    CComPtr<IWICImagingFactory> Factory;
    CComPtr<IWICBitmapDecoder> Decoder;
    CComPtr<IWICBitmapFrameDecode> Frame;
    CComPtr<IWICFormatConverter> Converter;
    CComPtr<IWICBitmapScaler> Scaler;
    HRESULT hr = Factory.CoCreateInstance(CLSID_WICImagingFactory, IID_IWICImagingFactory, NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
        return NULL;
    hr = Factory->CreateDecoderFromStream(Stream, NULL, WICDecodeMetadataCacheOnLoad, &Decoder);
    if (FAILED(hr) || FAILED(Decoder->GetFrame(0, &Frame)))
        return NULL;
    hr = Factory->CreateFormatConverter(&Converter);
    if (FAILED(hr))
        return NULL;
    hr = Converter->Initialize(Frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return NULL;

    UINT Width, Height;
    hr = Converter->GetSize(&Width, &Height);
    if (FAILED(hr) || !Width || !Height || Width > 4096 || Height > 4096)
        return NULL;

    hr = Factory->CreateBitmapScaler(&Scaler);
    if (FAILED(hr))
        return NULL;
    hr = Scaler->Initialize(Converter, Size, Size, WICBitmapInterpolationModeHighQualityCubic);
    if (FAILED(hr))
        return NULL;

    BITMAPINFO Info = {};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Size;
    Info.bmiHeader.biHeight = -Size;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    BYTE *Bits;
    HBITMAP Bitmap = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    if (Bitmap)
    {
        hr = Scaler->CopyPixels(NULL, Size * 4, Size * Size * 4, Bits);
        if (FAILED(hr))
        {
            DeleteObject(Bitmap);
            Bitmap = NULL;
        }
    }
    return Bitmap;
}

static BOOL GetStartOrbDib(HBITMAP Bitmap, DIBSECTION *Dib)
{
    return Bitmap &&
           GetObjectW(Bitmap, sizeof(*Dib), Dib) == sizeof(*Dib) &&
           Dib->dsBm.bmBits &&
           Dib->dsBm.bmBitsPixel == 32 &&
           Dib->dsBmih.biHeight < 0 &&
           Dib->dsBm.bmWidth == Dib->dsBm.bmHeight;
}

INT GetStartOrbExtent(HBITMAP Bitmap)
{
    DIBSECTION Dib;
    if (!GetStartOrbDib(Bitmap, &Dib))
        return 0;

    const BYTE *Bits = (const BYTE *)Dib.dsBm.bmBits;
    INT Left = Dib.dsBm.bmWidth, Right = -1;
    for (INT y = 0; y < Dib.dsBm.bmHeight; ++y)
    {
        const BYTE *Row = Bits + y * Dib.dsBm.bmWidthBytes;
        for (INT x = 0; x < Dib.dsBm.bmWidth; ++x)
        {
            if (Row[x * 4 + 3])
            {
                Left = min(Left, x);
                Right = max(Right, x);
            }
        }
    }
    return (Right >= Left) ? Right - Left + 1 : 0;
}

BOOL DrawStartOrbBitmap(HDC hdc, HBITMAP Source, INT x, INT y, INT Size, double Scale)
{
    DIBSECTION Dib;
    if (Size <= 0 || Scale <= 0.0 || !GetStartOrbDib(Source, &Dib) ||
        Dib.dsBm.bmWidth < Size || Dib.dsBm.bmWidth % Size)
    {
        return FALSE;
    }

    const INT SourceSize = Dib.dsBm.bmWidth;
    const INT Sub = SourceSize / Size;
    const UINT Samples = Sub * Sub;
    const BYTE *Bits = (const BYTE *)Dib.dsBm.bmBits;
    const double Center = SourceSize / 2.0;

    BITMAPINFO Info = {};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Size;
    Info.bmiHeader.biHeight = -Size;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    DWORD *Frame;
    HBITMAP Bitmap = CreateDIBSection(hdc, &Info, DIB_RGB_COLORS, (void **)&Frame, NULL, 0);
    if (!Bitmap)
        return FALSE;
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem)
    {
        DeleteObject(Bitmap);
        return FALSE;
    }

    for (INT dy = 0; dy < Size; ++dy)
    {
        for (INT dx = 0; dx < Size; ++dx)
        {
            UINT b = 0, g = 0, r = 0, a = 0;
            for (INT j = 0; j < Sub; ++j)
            {
                double v = Center + (dy * Sub + j + 0.5 - Center) / Scale;
                if (v < 0.0 || v >= SourceSize)
                    continue;
                const BYTE *Row = Bits + (INT)v * Dib.dsBm.bmWidthBytes;
                for (INT i = 0; i < Sub; ++i)
                {
                    double u = Center + (dx * Sub + i + 0.5 - Center) / Scale;
                    if (u < 0.0 || u >= SourceSize)
                        continue;
                    const BYTE *Pixel = Row + (INT)u * 4;
                    b += Pixel[0] * Pixel[3];
                    g += Pixel[1] * Pixel[3];
                    r += Pixel[2] * Pixel[3];
                    a += Pixel[3];
                }
            }
            Frame[dy * Size + dx] = ((a / Samples) << 24) |
                                    ((r / (255 * Samples)) << 16) |
                                    ((g / (255 * Samples)) << 8) |
                                    (b / (255 * Samples));
        }
    }

    HGDIOBJ OldBitmap = SelectObject(hdcMem, Bitmap);
    BLENDFUNCTION Blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    BOOL Ret = GdiAlphaBlend(hdc, x, y, Size, Size, hdcMem, 0, 0, Size, Size, Blend);
    SelectObject(hdcMem, OldBitmap);
    DeleteDC(hdcMem);
    DeleteObject(Bitmap);
    return Ret;
}

HIMAGELIST CreateStartOrbImageList(INT Size)
{
    if (Size <= 0 || Size > 4096)
        return NULL;

    /* BUTTON_IMAGELIST uses the PBS_* state order, including disabled/focused. */
    static const UINT Resources[] =
    {
        IDI_STARTORB,
        IDI_STARTORB_HOVER,
        IDI_STARTORB_PRESSED,
        IDI_STARTORB,
        IDI_STARTORB
    };
    HIMAGELIST ImageList = ImageList_Create(Size, Size, ILC_COLOR32 | ILC_MASK,
                                          _countof(Resources), 1);
    if (!ImageList)
        return NULL;

    UINT State;
    for (State = 0; State < _countof(Resources); ++State)
    {
        HBITMAP Bitmap = CreateStartOrbBitmap(Size, Resources[State]);
        if (!Bitmap)
            break;
        INT Index = ImageList_Add(ImageList, Bitmap, NULL);
        DeleteObject(Bitmap);
        if (Index != (INT)State)
            break;
    }

    if (State == _countof(Resources))
        return ImageList;

    /* A single image is valid for every button state; a partial list is not. */
    if (State > 0 && ImageList_SetImageCount(ImageList, 1))
        return ImageList;

    /* Keep the resource icon as a fallback if the PNG decoder is unavailable. */
    if (State == 0)
    {
        HICON Icon = (HICON)LoadImageW(hExplorerInstance, MAKEINTRESOURCEW(IDI_STARTORB),
                                     IMAGE_ICON, Size, Size, 0);
        if (Icon)
        {
            INT Index = ImageList_AddIcon(ImageList, Icon);
            DestroyIcon(Icon);
            if (Index >= 0)
                return ImageList;
        }
    }

    ImageList_Destroy(ImageList);
    return NULL;
}
