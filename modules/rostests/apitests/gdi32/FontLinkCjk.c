/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that CJK font linking and fallback follow the system language
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#include <apitest.h>
#include <strsafe.h>
#include <usp10.h>

#define WIDTH 600
#define HEIGHT 200

/* Shared Han characters covered by CP932/936/949/950 whose glyph forms differ across CJK locales */
#define TEST_HAN_STRING    L"\x9aa8\x76f4\x8ff0\x907f"
#define TEST_HANGUL_STRING L"\xac00\xd55c\xae00"

typedef enum _CJK_GROUP
{
    GRP_JP,
    GRP_TC,
    GRP_SC,
    GRP_KO
} CJK_GROUP;

typedef enum _RENDER_MODE
{
    RENDER_GDI,
    RENDER_UNISCRIBE,
    RENDER_GDIPLUS
} RENDER_MODE;

static const struct
{
    PCWSTR Face;
    CJK_GROUP Group;
} s_Candidates[] =
{
    { L"MS UI Gothic",         GRP_JP },
    { L"MS Gothic",            GRP_JP },
    { L"MS PGothic",           GRP_JP },
    { L"Meiryo",               GRP_JP },
    { L"Meiryo UI",            GRP_JP },
    { L"Yu Gothic",            GRP_JP },
    { L"Yu Gothic UI",         GRP_JP },
    { L"Noto Sans JP",         GRP_JP },
    { L"MingLiU",              GRP_TC },
    { L"PMingLiU",             GRP_TC },
    { L"Microsoft JhengHei",   GRP_TC },
    { L"Microsoft JhengHei UI",GRP_TC },
    { L"Noto Sans TC",         GRP_TC },
    { L"SimSun",               GRP_SC },
    { L"NSimSun",              GRP_SC },
    { L"Microsoft YaHei",      GRP_SC },
    { L"Microsoft YaHei UI",   GRP_SC },
    { L"DengXian",             GRP_SC },
    { L"Noto Sans SC",         GRP_SC },
    { L"Gulim",                GRP_KO },
    { L"Malgun Gothic",        GRP_KO },
    { L"Batang",               GRP_KO },
    { L"Noto Sans KR",         GRP_KO },
};

static const PCWSTR s_GroupNames[] = { L"Japanese", L"Chinese Traditional", L"Chinese Simplified", L"Korean" };

static BOOL s_Exists[_countof(s_Candidates)];

typedef HRESULT (WINAPI *PSSA)(HDC, const void *, int, int, int, DWORD, int, SCRIPT_CONTROL *, SCRIPT_STATE *,
                               const int *, SCRIPT_TABDEF *, const int *, SCRIPT_STRING_ANALYSIS *);
typedef HRESULT (WINAPI *PSSO)(SCRIPT_STRING_ANALYSIS, int, int, UINT, const RECT *, int, int, BOOL);
typedef HRESULT (WINAPI *PSSF)(SCRIPT_STRING_ANALYSIS *);

static PSSA s_pScriptStringAnalyse;
static PSSO s_pScriptStringOut;
static PSSF s_pScriptStringFree;

typedef void GpGraphics;
typedef void GpFont;
typedef void GpBrush;
typedef void GpStringFormat;
typedef struct { float X, Y, Width, Height; } GpRectF;
typedef struct { UINT32 Version; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;

typedef int (WINAPI *PGDIPSTARTUP)(ULONG_PTR *, const GpStartupInput *, void *);
typedef int (WINAPI *PGDIPCFFHDC)(HDC, GpGraphics **);
typedef int (WINAPI *PGDIPCFLF)(HDC, const LOGFONTW *, GpFont **);
typedef int (WINAPI *PGDIPDS)(GpGraphics *, const WCHAR *, INT, const GpFont *, const GpRectF *,
                              const GpStringFormat *, const GpBrush *);
typedef int (WINAPI *PGDIPCSF)(DWORD, GpBrush **);
typedef int (WINAPI *PGDIPCSFMT)(INT, LANGID, GpStringFormat **);
typedef int (WINAPI *PGDIPDEL)(void *);
typedef void (WINAPI *PGDIPSHUTDOWN)(ULONG_PTR);

static PGDIPSTARTUP s_pGdiplusStartup;
static PGDIPCFFHDC s_pGdipCreateFromHDC;
static PGDIPCFLF s_pGdipCreateFontFromLogfontW;
static PGDIPDS s_pGdipDrawString;
static PGDIPCSF s_pGdipCreateSolidFill;
static PGDIPCSFMT s_pGdipCreateStringFormat;
static PGDIPDEL s_pGdipDeleteFont, s_pGdipDeleteBrush, s_pGdipDeleteGraphics, s_pGdipDeleteStringFormat;
static PGDIPSHUTDOWN s_pGdiplusShutdown;

static int CALLBACK
EnumFoundProc(const LOGFONTW *plf, const TEXTMETRICW *ptm, DWORD dwType, LPARAM lParam)
{
    *(BOOL *)lParam = TRUE;
    return 0;
}

static BOOL
FaceExists(HDC hdc, PCWSTR pszFace)
{
    LOGFONTW lf;
    BOOL bFound = FALSE;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(lf.lfFaceName, _countof(lf.lfFaceName), pszFace);
    EnumFontFamiliesExW(hdc, &lf, EnumFoundProc, (LPARAM)&bFound, 0);
    return bFound;
}

static BOOL
FaceCoversText(PCWSTR pszFace, PCWSTR pszText)
{
    HDC hdc;
    HFONT hFont, hFontOld;
    WORD Indices[16];
    int i, cch = (int)wcslen(pszText);
    BOOL bCovers = TRUE;

    if (cch <= 0 || cch > (int)_countof(Indices))
        return FALSE;

    hdc = CreateCompatibleDC(NULL);
    hFont = CreateFontW(-48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, pszFace);
    hFontOld = SelectObject(hdc, hFont);
    if (GetGlyphIndicesW(hdc, pszText, cch, Indices, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
    {
        bCovers = FALSE;
    }
    else
    {
        for (i = 0; i < cch; i++)
        {
            if (Indices[i] == 0xffff || Indices[i] == 0)
            {
                bCovers = FALSE;
                break;
            }
        }
    }
    SelectObject(hdc, hFontOld);
    DeleteObject(hFont);
    DeleteDC(hdc);
    return bCovers;
}

static BOOL
GroupExists(CJK_GROUP Group)
{
    UINT i;

    for (i = 0; i < _countof(s_Candidates); i++)
    {
        if (s_Exists[i] && s_Candidates[i].Group == Group)
            return TRUE;
    }
    return FALSE;
}

static DWORD *
RenderString(PCWSTR pszFace, PCWSTR pszText, RENDER_MODE Mode, HBITMAP *phbm)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), WIDTH, -HEIGHT, 1, 32, BI_RGB } };
    HDC hdc;
    HBITMAP hbm, hbmOld;
    HFONT hFont, hFontOld;
    DWORD *pBits = NULL;
    RECT rc = { 0, 0, WIDTH, HEIGHT };
    int cch = (int)wcslen(pszText);

    *phbm = NULL;
    hdc = CreateCompatibleDC(NULL);
    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    if (!pBits)
    {
        DeleteDC(hdc);
        return NULL;
    }
    hbmOld = SelectObject(hdc, hbm);
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
    hFont = CreateFontW(-48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, pszFace);
    hFontOld = SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    if (Mode == RENDER_UNISCRIBE)
    {
        SCRIPT_STRING_ANALYSIS ssa = NULL;

        if (SUCCEEDED(s_pScriptStringAnalyse(hdc, pszText, cch, cch * 2 + 16, -1,
                                             SSA_FALLBACK | SSA_GLYPHS, 0, NULL, NULL, NULL, NULL, NULL, &ssa)))
        {
            s_pScriptStringOut(ssa, 10, 20, 0, NULL, 0, 0, FALSE);
            s_pScriptStringFree(&ssa);
        }
    }
    else if (Mode == RENDER_GDIPLUS)
    {
        GpGraphics *pGraphics = NULL;
        GpFont *pFont = NULL;
        GpBrush *pBrush = NULL;
        GpStringFormat *pFormat = NULL;
        GpRectF Rect = { 10.0f, 20.0f, (float)WIDTH - 20.0f, (float)HEIGHT - 40.0f };
        LOGFONTW lf;

        if (s_pGdipCreateFromHDC(hdc, &pGraphics) == 0 &&
            GetObjectW(hFont, sizeof(lf), &lf) &&
            s_pGdipCreateFontFromLogfontW(hdc, &lf, &pFont) == 0 &&
            s_pGdipCreateStringFormat(0, 0, &pFormat) == 0 &&
            s_pGdipCreateSolidFill(0xff000000, &pBrush) == 0)
        {
            s_pGdipDrawString(pGraphics, pszText, cch, pFont, &Rect, pFormat, pBrush);
        }
        if (pBrush) s_pGdipDeleteBrush(pBrush);
        if (pFormat) s_pGdipDeleteStringFormat(pFormat);
        if (pFont) s_pGdipDeleteFont(pFont);
        if (pGraphics) s_pGdipDeleteGraphics(pGraphics);
    }
    else
    {
        TextOutW(hdc, 10, 20, pszText, cch);
    }
    GdiFlush();

    SelectObject(hdc, hFontOld);
    DeleteObject(hFont);
    SelectObject(hdc, hbmOld);
    DeleteDC(hdc);
    *phbm = hbm;

    return pBits;
}

static UINT
InkCount(const DWORD *pBits)
{
    UINT Count = 0, i;

    for (i = 0; i < WIDTH * HEIGHT; i++)
    {
        if ((pBits[i] & 0xffffff) != 0xffffff)
            Count++;
    }
    return Count;
}

static void
InkOrigin(const DWORD *pBits, int *pMinX, int *pMinY)
{
    int x, y, minX = WIDTH, minY = HEIGHT;

    for (y = 0; y < HEIGHT; y++)
    {
        for (x = 0; x < WIDTH; x++)
        {
            if ((pBits[y * WIDTH + x] & 0xffffff) != 0xffffff)
            {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
            }
        }
    }
    *pMinX = (minX < WIDTH) ? minX : 0;
    *pMinY = (minY < HEIGHT) ? minY : 0;
}

static UINT
FarPixels(const DWORD *pA, const DWORD *pB)
{
    int ax, ay, bx, by, x, y;
    UINT Far = 0;

    InkOrigin(pA, &ax, &ay);
    InkOrigin(pB, &bx, &by);

    for (y = 0; y < HEIGHT; y++)
    {
        for (x = 0; x < WIDTH; x++)
        {
            DWORD a = (x + ax < WIDTH && y + ay < HEIGHT) ? pA[(y + ay) * WIDTH + (x + ax)] : 0xffffff;
            DWORD b = (x + bx < WIDTH && y + by < HEIGHT) ? pB[(y + by) * WIDTH + (x + bx)] : 0xffffff;
            int dR = abs((int)(a & 0xff) - (int)(b & 0xff));
            int dG = abs((int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff));
            int dB = abs((int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff));

            if (dR + dG + dB >= 96)
                Far++;
        }
    }
    return Far;
}

static int
ActualGroup(PCWSTR pszBase, PCWSTR pszText, RENDER_MODE Mode)
{
    HBITMAP hbmBase = NULL;
    DWORD *pBase;
    UINT BestFar = ~0u;
    int Group = -1;
    UINT i;

    pBase = RenderString(pszBase, pszText, Mode, &hbmBase);
    if (!pBase)
    {
        skip("could not render %ls\n", pszBase);
        return -1;
    }
    if (InkCount(pBase) < 200)
    {
        trace("%ls drew almost nothing (%u ink)\n", pszBase, InkCount(pBase));
        DeleteObject(hbmBase);
        return -1;
    }

    for (i = 0; i < _countof(s_Candidates); i++)
    {
        HBITMAP hbmCand = NULL;
        DWORD *pCand;
        UINT Far;

        if (!s_Exists[i] || !FaceCoversText(s_Candidates[i].Face, pszText))
            continue;
        pCand = RenderString(s_Candidates[i].Face, pszText, Mode, &hbmCand);
        if (!pCand)
            continue;
        if (InkCount(pCand) >= 200)
        {
            Far = FarPixels(pBase, pCand);
            trace("%ls vs %ls: %u far\n", pszBase, s_Candidates[i].Face, Far);
            if (Far < BestFar)
            {
                BestFar = Far;
                Group = s_Candidates[i].Group;
            }
        }
        DeleteObject(hbmCand);
    }

    DeleteObject(hbmBase);
    if (Group >= 0)
        trace("%ls (mode %d) closest to %ls (%u far)\n", pszBase, Mode, s_GroupNames[Group], BestFar);
    return Group;
}

static int
ExpectedHanGroup(void)
{
    CJK_GROUP Order[4];
    UINT Rank;

    switch (PRIMARYLANGID(GetSystemDefaultLangID()))
    {
        case LANG_JAPANESE:
            Order[0] = GRP_JP; Order[1] = GRP_TC; Order[2] = GRP_SC; Order[3] = GRP_KO;
            break;
        case LANG_KOREAN:
            Order[0] = GRP_KO; Order[1] = GRP_JP; Order[2] = GRP_TC; Order[3] = GRP_SC;
            break;
        case LANG_CHINESE:
            switch (SUBLANGID(GetSystemDefaultLangID()))
            {
                case SUBLANG_CHINESE_TRADITIONAL:
                case SUBLANG_CHINESE_HONGKONG:
                case SUBLANG_CHINESE_MACAU:
                    Order[0] = GRP_TC; Order[1] = GRP_SC; Order[2] = GRP_JP; Order[3] = GRP_KO;
                    break;
                default:
                    Order[0] = GRP_SC; Order[1] = GRP_TC; Order[2] = GRP_JP; Order[3] = GRP_KO;
                    break;
            }
            break;
        default:
            Order[0] = GRP_JP; Order[1] = GRP_TC; Order[2] = GRP_SC; Order[3] = GRP_KO;
            break;
    }

    for (Rank = 0; Rank < _countof(Order); Rank++)
    {
        if (GroupExists(Order[Rank]))
            return Order[Rank];
    }

    return -1;
}

static int
ExpectedGdiplusHanGroup(void)
{
    switch (PRIMARYLANGID(GetSystemDefaultLangID()))
    {
        case LANG_JAPANESE:
        case LANG_KOREAN:
        case LANG_CHINESE:
            return ExpectedHanGroup();
        default:
            return GroupExists(GRP_SC) ? GRP_SC : ExpectedHanGroup();
    }
}

static void
TestLinkedHanRender(PCWSTR pszBase, RENDER_MODE Mode)
{
    int Expected, Actual;

    Expected = (Mode == RENDER_GDIPLUS) ? ExpectedGdiplusHanGroup() : ExpectedHanGroup();
    Actual = ActualGroup(pszBase, TEST_HAN_STRING, Mode);
    ok(Actual == Expected, "%ls (mode %d) Han drew %ls glyphs, expected %ls\n",
       pszBase, Mode,
       Actual >= 0 ? s_GroupNames[Actual] : L"unknown",
       Expected >= 0 ? s_GroupNames[Expected] : L"unknown");
}

static void
TestLinkedHangulRender(PCWSTR pszBase, RENDER_MODE Mode)
{
    int Actual;

    if (!GroupExists(GRP_KO))
    {
        ok(FALSE, "%ls (mode %d) has no Korean font to draw Hangul with\n", pszBase, Mode);
        return;
    }

    Actual = ActualGroup(pszBase, TEST_HANGUL_STRING, Mode);
    ok(Actual == GRP_KO, "%ls (mode %d) Hangul drew %ls glyphs, expected Korean\n",
       pszBase, Mode,
       Actual >= 0 ? s_GroupNames[Actual] : L"unknown");
}

START_TEST(FontLinkCjk)
{
    HDC hdc;
    HMODULE hUsp10, hGdiplus;
    UINT i;

    hUsp10 = LoadLibraryW(L"usp10.dll");
    if (hUsp10)
    {
        s_pScriptStringAnalyse = (PSSA)GetProcAddress(hUsp10, "ScriptStringAnalyse");
        s_pScriptStringOut = (PSSO)GetProcAddress(hUsp10, "ScriptStringOut");
        s_pScriptStringFree = (PSSF)GetProcAddress(hUsp10, "ScriptStringFree");
    }
    ok(s_pScriptStringAnalyse && s_pScriptStringOut && s_pScriptStringFree,
       "usp10 ScriptString entry points missing\n");

    hGdiplus = LoadLibraryW(L"gdiplus.dll");
    if (hGdiplus)
    {
        s_pGdiplusStartup = (PGDIPSTARTUP)GetProcAddress(hGdiplus, "GdiplusStartup");
        s_pGdipCreateFromHDC = (PGDIPCFFHDC)GetProcAddress(hGdiplus, "GdipCreateFromHDC");
        s_pGdipCreateFontFromLogfontW = (PGDIPCFLF)GetProcAddress(hGdiplus, "GdipCreateFontFromLogfontW");
        s_pGdipDrawString = (PGDIPDS)GetProcAddress(hGdiplus, "GdipDrawString");
        s_pGdipCreateSolidFill = (PGDIPCSF)GetProcAddress(hGdiplus, "GdipCreateSolidFill");
        s_pGdipCreateStringFormat = (PGDIPCSFMT)GetProcAddress(hGdiplus, "GdipCreateStringFormat");
        s_pGdipDeleteFont = (PGDIPDEL)GetProcAddress(hGdiplus, "GdipDeleteFont");
        s_pGdipDeleteBrush = (PGDIPDEL)GetProcAddress(hGdiplus, "GdipDeleteBrush");
        s_pGdipDeleteGraphics = (PGDIPDEL)GetProcAddress(hGdiplus, "GdipDeleteGraphics");
        s_pGdipDeleteStringFormat = (PGDIPDEL)GetProcAddress(hGdiplus, "GdipDeleteStringFormat");
        s_pGdiplusShutdown = (PGDIPSHUTDOWN)GetProcAddress(hGdiplus, "GdiplusShutdown");
    }
    ok(s_pGdiplusStartup && s_pGdipDrawString, "gdiplus entry points missing\n");

    hdc = CreateCompatibleDC(NULL);
    for (i = 0; i < _countof(s_Candidates); i++)
    {
        s_Exists[i] = FaceExists(hdc, s_Candidates[i].Face);
        if (s_Exists[i])
            trace("face present: %ls\n", s_Candidates[i].Face);
    }
    DeleteDC(hdc);

    TestLinkedHanRender(L"Segoe UI", RENDER_GDI);
    TestLinkedHanRender(L"Tahoma", RENDER_GDI);
    TestLinkedHanRender(L"Microsoft Sans Serif", RENDER_GDI);
    TestLinkedHanRender(L"MS Shell Dlg", RENDER_GDI);
    TestLinkedHangulRender(L"Segoe UI", RENDER_GDI);
    TestLinkedHangulRender(L"Tahoma", RENDER_GDI);

    if (s_pScriptStringAnalyse)
    {
        TestLinkedHanRender(L"Tahoma", RENDER_UNISCRIBE);
        TestLinkedHangulRender(L"Tahoma", RENDER_UNISCRIBE);
    }

    if (s_pGdipDrawString)
    {
        ULONG_PTR Token = 0;
        GpStartupInput Input = { 1, NULL, FALSE, FALSE };

        if (s_pGdiplusStartup(&Token, &Input, NULL) == 0)
        {
            TestLinkedHanRender(L"Tahoma", RENDER_GDIPLUS);
            TestLinkedHangulRender(L"Tahoma", RENDER_GDIPLUS);
            s_pGdiplusShutdown(Token);
        }
    }
}
