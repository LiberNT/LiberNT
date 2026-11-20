/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests parsing and drawing of Direct2D SVG documents
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1_3.h>
#include <wincodec.h>

#define SIZE 64

static const char TestSvg[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 32 32\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"grad\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">\n"
    "      <stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/>\n"
    "    </linearGradient>\n"
    "    <rect id=\"cell\" width=\"4\" height=\"4\" fill=\"lime\"/>\n"
    "    <clipPath id=\"clip\"><rect x=\"16\" y=\"16\" width=\"4\" height=\"4\"/></clipPath>\n"
    "  </defs>\n"
    "  <rect id=\"red\" x=\"2\" y=\"2\" width=\"4\" height=\"4\" fill=\"red\"/>\n"
    "  <circle cx=\"12\" cy=\"4\" r=\"2\" style=\"fill:#0000ff\"/>\n"
    "  <g transform=\"translate(20,0)\"><rect x=\"0\" y=\"2\" width=\"4\" height=\"4\" fill=\"rgb(0,128,0)\"/></g>\n"
    "  <use xlink:href=\"#cell\" x=\"26\" y=\"2\"/>\n"
    "  <rect x=\"2\" y=\"10\" width=\"20\" height=\"4\" fill=\"url(#grad)\"/>\n"
    "  <rect x=\"0\" y=\"16\" width=\"32\" height=\"16\" fill=\"yellow\" clip-path=\"url(#clip)\"/>\n"
    "  <text x=\"2\" y=\"28\">x</text>\n"
    "  <filter id=\"f\"><rect x=\"2\" y=\"26\" width=\"4\" height=\"4\" fill=\"black\"/></filter>\n"
    "  <foo><rect x=\"8\" y=\"26\" width=\"4\" height=\"4\" fill=\"black\"/></foo>\n"
    "  <g opacity=\"0.5\"><rect x=\"24\" y=\"24\" width=\"4\" height=\"4\" fill=\"#0000ff\"/></g>\n"
    "  <path d=\"M 26 10 h 4 v 4 h -4 z\" fill=\"none\" stroke=\"#000000\" stroke-width=\"1\"/>\n"
    "  <rect x=\"1e1\" y=\"2.0E+1\" width=\"4\" height=\"0.4e1\" fill=\"#00ff00\"/>\n"
    "</svg>\n";

typedef struct _SVG_PIXEL
{
    UINT x;
    UINT y;
    BYTE b, g, r;
    const char *What;
} SVG_PIXEL;

static IStream *
CreateStream(const char *Text)
{
    HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, strlen(Text));
    IStream *Stream = NULL;

    if (!Memory)
        return NULL;
    memcpy(GlobalLock(Memory), Text, strlen(Text));
    GlobalUnlock(Memory);
    if (FAILED(CreateStreamOnHGlobal(Memory, TRUE, &Stream)))
        GlobalFree(Memory);
    return Stream;
}

static void
CheckPixels(IWICBitmap *Bitmap, const SVG_PIXEL *Pixels, UINT Count)
{
    WICRect Rect = { 0, 0, SIZE, SIZE };
    IWICBitmapLock *Lock = NULL;
    UINT Stride = 0, BufferSize = 0, i;
    BYTE *Bits = NULL;
    const BYTE *Pixel;
    HRESULT hr;

    hr = IWICBitmap_Lock(Bitmap, &Rect, WICBitmapLockRead, &Lock);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    IWICBitmapLock_GetStride(Lock, &Stride);
    hr = IWICBitmapLock_GetDataPointer(Lock, &BufferSize, &Bits);
    ok_hr(hr, S_OK);
    for (i = 0; SUCCEEDED(hr) && i < Count; i++)
    {
        Pixel = Bits + (SIZE_T)Pixels[i].y * Stride + (SIZE_T)Pixels[i].x * 4;
        ok(abs(Pixel[0] - Pixels[i].b) <= 8 && abs(Pixel[1] - Pixels[i].g) <= 8 && abs(Pixel[2] - Pixels[i].r) <= 8,
           "%s: pixel (%u,%u) is %u,%u,%u, expected %u,%u,%u\n", Pixels[i].What, Pixels[i].x, Pixels[i].y, Pixel[0],
           Pixel[1], Pixel[2], Pixels[i].b, Pixels[i].g, Pixels[i].r);
    }
    IWICBitmapLock_Release(Lock);
}

static void
CheckGradient(IWICBitmap *Bitmap)
{
    WICRect Rect = { 0, 0, SIZE, SIZE };
    IWICBitmapLock *Lock = NULL;
    UINT Stride = 0, BufferSize = 0;
    const BYTE *Left, *Right;
    BYTE *Bits = NULL;

    if (FAILED(IWICBitmap_Lock(Bitmap, &Rect, WICBitmapLockRead, &Lock)))
        return;
    IWICBitmapLock_GetStride(Lock, &Stride);
    if (SUCCEEDED(IWICBitmapLock_GetDataPointer(Lock, &BufferSize, &Bits)))
    {
        Left = Bits + 24 * (SIZE_T)Stride + (SIZE_T)5 * 4;
        Right = Bits + 24 * (SIZE_T)Stride + (SIZE_T)42 * 4;
        ok(Left[2] > 200 && Left[0] < 60, "gradient start is %u,%u,%u\n", Left[0], Left[1], Left[2]);
        ok(Right[0] > 200 && Right[2] < 60, "gradient end is %u,%u,%u\n", Right[0], Right[1], Right[2]);
    }
    IWICBitmapLock_Release(Lock);
}

static void
TestDom(ID2D1SvgDocument *Document)
{
    ID2D1SvgElement *Root = NULL, *Element = NULL;
    D2D1_SIZE_F Size;
    WCHAR Name[32];
    HRESULT hr;

    Document->lpVtbl->GetViewportSize(Document, &Size);
    ok(Size.width == SIZE && Size.height == SIZE, "viewport %.1f x %.1f\n", Size.width, Size.height);
    ID2D1SvgDocument_GetRoot(Document, &Root);
    ok(Root != NULL, "no root\n");
    if (Root)
    {
        hr = ID2D1SvgElement_GetTagName(Root, Name, ARRAYSIZE(Name));
        ok_hr(hr, S_OK);
        ok(!wcscmp(Name, L"svg"), "root is %ls\n", Name);
        ok(ID2D1SvgElement_HasChildren(Root), "root has no children\n");
        ID2D1SvgElement_Release(Root);
    }
    hr = ID2D1SvgDocument_FindElementById(Document, L"red", &Element);
    ok_hr(hr, S_OK);
    ok(Element != NULL, "red not found\n");
    if (Element)
    {
        hr = ID2D1SvgElement_GetTagName(Element, Name, ARRAYSIZE(Name));
        ok(hr == S_OK && !wcscmp(Name, L"rect"), "red is %ls\n", Name);
        ID2D1SvgElement_Release(Element);
    }
    Element = (ID2D1SvgElement *)0xdeadbeef;
    hr = ID2D1SvgDocument_FindElementById(Document, L"missing", &Element);
    ok_hr(hr, S_OK);
    ok(Element == NULL, "found a missing element\n");
}

START_TEST(SvgDocument)
{
    static const SVG_PIXEL Pixels[] =
    {
        { 8, 8, 0, 0, 255, "rect" },
        { 24, 8, 255, 0, 0, "circle with a style fill" },
        { 44, 8, 0, 128, 0, "transformed group" },
        { 56, 8, 0, 255, 0, "use" },
        { 36, 36, 0, 255, 255, "clip path inside" },
        { 8, 40, 255, 255, 255, "clip path outside" },
        { 6, 56, 255, 255, 255, "filter content" },
        { 20, 56, 255, 255, 255, "unknown element content" },
        { 52, 52, 255, 128, 128, "group opacity" },
        { 52, 24, 0, 0, 0, "stroke" },
        { 56, 24, 255, 255, 255, "no fill" },
        { 24, 44, 0, 255, 0, "exponent notation" },
        { 18, 44, 255, 255, 255, "left of the exponent rect" },
    };
    D2D1_RENDER_TARGET_PROPERTIES Props = { D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                            { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96.0f, 96.0f,
                                            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT };
    D2D1_COLOR_F White = { 1.0f, 1.0f, 1.0f, 1.0f };
    D2D1_SIZE_F Viewport = { SIZE, SIZE };
    ID2D1SvgDocument *Document = NULL;
    ID2D1DeviceContext5 *Context = NULL;
    ID2D1RenderTarget *Target = NULL;
    IWICImagingFactory *Wic = NULL;
    ID2D1Factory *Factory = NULL;
    IWICBitmap *Bitmap = NULL;
    IStream *Stream;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&Factory);
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                              (void **)&Wic);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateBitmap(Wic, SIZE, SIZE, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                             &Bitmap);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(Factory, Bitmap, &Props, &Target);
    if (SUCCEEDED(hr))
        hr = ID2D1RenderTarget_QueryInterface(Target, &IID_ID2D1DeviceContext5, (void **)&Context);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        Stream = CreateStream("<svg");
        Document = (ID2D1SvgDocument *)0xdeadbeef;
        hr = ID2D1DeviceContext5_CreateSvgDocument(Context, Stream, Viewport, &Document);
        ok(FAILED(hr), "an incomplete document parsed, hr 0x%lx\n", hr);
        if (Stream)
            IStream_Release(Stream);

        hr = ID2D1DeviceContext5_CreateSvgDocument(Context, NULL, Viewport, &Document);
        ok_hr(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            ID2D1SvgElement *Root = NULL;
            WCHAR Name[8] = { 0 };

            ID2D1SvgDocument_GetRoot(Document, &Root);
            ok(Root != NULL, "an empty document has no root\n");
            if (Root)
            {
                ID2D1SvgElement_GetTagName(Root, Name, ARRAYSIZE(Name));
                ok(!wcscmp(Name, L"svg"), "the root of an empty document is %ls\n", Name);
                ok(!ID2D1SvgElement_HasChildren(Root), "the root of an empty document has children\n");
                ID2D1SvgElement_Release(Root);
            }
            ID2D1SvgDocument_Release(Document);
        }

        Stream = CreateStream(TestSvg);
        hr = ID2D1DeviceContext5_CreateSvgDocument(Context, Stream, Viewport, &Document);
        ok_hr(hr, S_OK);
        if (Stream)
            IStream_Release(Stream);
        if (SUCCEEDED(hr))
        {
            TestDom(Document);
            ID2D1DeviceContext5_BeginDraw(Context);
            ID2D1DeviceContext5_Clear(Context, &White);
            ID2D1DeviceContext5_DrawSvgDocument(Context, Document);
            hr = ID2D1DeviceContext5_EndDraw(Context, NULL, NULL);
            ok_hr(hr, S_OK);
            CheckPixels(Bitmap, Pixels, ARRAYSIZE(Pixels));
            CheckGradient(Bitmap);
            ID2D1SvgDocument_Release(Document);
        }
    }
    if (Context)
        ID2D1DeviceContext5_Release(Context);
    if (Target)
        ID2D1RenderTarget_Release(Target);
    if (Bitmap)
        IWICBitmap_Release(Bitmap);
    if (Wic)
        IWICImagingFactory_Release(Wic);
    if (Factory)
        ID2D1Factory_Release(Factory);
    CoUninitialize();
}
