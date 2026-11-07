
#include "precomp.h"
#include "resource_1bpp.h"

#define ROS_HGDI_ERROR (HANDLE)~(ULONG_PTR)0 // This makes MSVC compiler happy

static void test_LoadImage_1bpp(void)
{
    HDC hdc1, hdc2 = NULL;
    HBITMAP hBmp1, hBmp2;
    BITMAP bitmap1, bitmap2;
    struct
    {
	    BITMAPINFOHEADER bmiHeader;
	    RGBQUAD bmiColors[256];
    } bmi;
    UINT size;
    HGLOBAL hMem = NULL;
    LPVOID lpBits;
    BYTE img[4 * 4] = { 0 };
    INT result;
    HGDIOBJ res_obj;

    hdc1 = CreateCompatibleDC(NULL);
    /* Load bitmap with BITMAPINFOHEADER (40 bytes) */
    hBmp1 = LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(201), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    res_obj = SelectObject(hdc1, hBmp1);
    if (res_obj == ROS_HGDI_ERROR || res_obj == NULL)
    {
        skip("Could not load 1 BPP bitmap\n");
        goto Cleanup;
    }
    GetObject(hBmp1, sizeof(BITMAP), &bitmap1);

    ok(bitmap1.bmBitsPixel == 1, "Should have been '1', but got '%d'\n", bitmap1.bmBitsPixel);
    ok(bitmap1.bmWidth == 4, "Should have been '4', but got '%ld'\n", bitmap1.bmWidth);
    ok(bitmap1.bmHeight == 4, "Should have been '4', but got '%ld'\n", bitmap1.bmHeight);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = bitmap1.bmWidth;
    bmi.bmiHeader.biHeight      = bitmap1.bmHeight;
    bmi.bmiHeader.biPlanes      = bitmap1.bmPlanes;
    bmi.bmiHeader.biBitCount    = bitmap1.bmBitsPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage   = 0;

    /* Get the size of the bitmap */
    size = ((bitmap1.bmWidth * bitmap1.bmBitsPixel + 31) / 32) * 4 * bitmap1.bmHeight;
    ok(size == 16, "Expected 16, but size is %d\n", size);

    hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    lpBits = GlobalLock(hMem);
    result = GetDIBits(hdc1, hBmp1, 0, bitmap1.bmHeight, lpBits, (PBITMAPINFO)&bmi, DIB_RGB_COLORS);
    if (!result)
    {
        skip("GetDIBits failed for 1 BPP bitmap\n");
        goto Cleanup;
    }

    /* Get bytes from bitmap (we know its 4x4 1BPP */
    memcpy(img, lpBits, 16);

    ok(img[0] == 0x60, "Got 0x%02x, expected x60\n", img[0]);
    ok(img[1] == 0, "Got 0x%02x, expected 0\n", img[1]);
    ok(img[2] == 0, "Got 0x%02x, expected 0\n", img[2]);
    ok(img[3] == 0, "Got 0x%02x, expected 0\n", img[3]);

    ok(img[4] == 0x90, "Got 0x%02x, expected x90\n", img[4]);
    ok(img[5] == 0, "Got 0x%02x, expected 0x60\n", img[5]);
    ok(img[6] == 0, "Got 0x%02x, expected 0\n", img[6]);
    ok(img[7] == 0, "Got 0x%02x, expected 0\n", img[7]);

    ok(img[8] == 0x90, "Got 0x%02x, expected x90\n", img[8]);
    ok(img[9] == 0, "Got 0x%02x, expected 0\n", img[9]);
    ok(img[10] == 0, "Got 0x%02x, expected 0\n", img[10]);
    ok(img[11] == 0, "Got 0x%02x, expected 0\n", img[11]);

    ok(img[12] == 0x60, "Got 0x%02x, expected x60\n", img[12]);
    ok(img[13] == 0, "Got 0x%02x, expected 0x60\n", img[13]);
    ok(img[14] == 0, "Got 0x%02x, expected 0\n", img[14]);
    ok(img[15] == 0, "Got 0x%02x, expected 0\n", img[15]);

    hdc2 = CreateCompatibleDC(NULL);
    /* Load bitmap with BITMAPCOREHEADER (12 bytes) */
    hBmp2 = LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(202), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    res_obj = SelectObject(hdc2, hBmp2);
    if (res_obj == ROS_HGDI_ERROR || res_obj == NULL)
    {
        skip("Could not load 1 BPP bitmap\n");
        goto Cleanup;
    }
    GetObject(hBmp2, sizeof(BITMAP), &bitmap2);
    ok(bitmap2.bmBitsPixel == 1, "Should have been '1', but got %d\n", bitmap2.bmBitsPixel);
    ok(bitmap2.bmWidth == 4, "Should have been '4', but got '%ld'\n", bitmap2.bmWidth);
    ok(bitmap2.bmHeight == 4, "Should have been '4', but got '%ld'\n", bitmap2.bmHeight);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = bitmap2.bmWidth;
    bmi.bmiHeader.biHeight      = bitmap2.bmHeight;
    bmi.bmiHeader.biPlanes      = bitmap2.bmPlanes;
    bmi.bmiHeader.biBitCount    = bitmap2.bmBitsPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage   = 0;

    /* Get the size of the bitmap */
    size = ((bitmap2.bmWidth * bitmap2.bmBitsPixel + 31) / 32) * 4 * bitmap2.bmHeight;
    ok(size == 16, "Expected 16, but size is %d\n", size);

    result = GetDIBits(hdc2, hBmp2, 0, bitmap2.bmHeight, lpBits, (PBITMAPINFO)&bmi, DIB_RGB_COLORS); // Check for success per Timo
    if (!result)
    {
        skip("GetDIBits failed for 1 BPP bitmap\n");
        goto Cleanup;
    }

    /* Clear img array for new test */
    memset(img, 0, 16);

    /* Get bytes from bitmap (we know its 4x4 1BPP */
    memcpy(img, lpBits, 16);

    ok(img[0] == 0x60 || broken(img[0] == 0) /* Vista Testbot */, "Got 0x%02x, expected x60\n", img[0]);
    ok(img[1] == 0, "Got 0x%02x, expected 0\n", img[1]);
    ok(img[2] == 0, "Got 0x%02x, expected 0\n", img[2]);
    ok(img[3] == 0, "Got 0x%02x, expected 0\n", img[3]);

    ok(img[4] == 0x90 || broken(img[4] == 0xf0) /* Vista Testbot */, "Got 0x%02x, expected x90\n", img[4]);
    ok(img[5] == 0, "Got 0x%02x, expected 0x60\n", img[5]);
    ok(img[6] == 0, "Got 0x%02x, expected 0\n", img[6]);
    ok(img[7] == 0, "Got 0x%02x, expected 0\n", img[7]);

    ok(img[8] == 0x90 || broken(img[8] == 0xf0) /* Vista Testbot */, "Got 0x%02x, expected x90\n", img[8]);
    ok(img[9] == 0, "Got 0x%02x, expected 0\n", img[9]);
    ok(img[10] == 0, "Got 0x%02x, expected 0\n", img[10]);
    ok(img[11] == 0, "Got 0x%02x, expected 0\n", img[11]);

    ok(img[12] == 0x60 || broken(img[12] == 0xf0) /* Vista Testbot */, "Got 0x%02x, expected x60\n", img[12]);
    ok(img[13] == 0, "Got 0x%02x, expected 0x60\n", img[13]);
    ok(img[14] == 0, "Got 0x%02x, expected 0\n", img[14]);
    ok(img[15] == 0, "Got 0x%02x, expected 0\n", img[15]);

Cleanup:
    if (hMem) GlobalUnlock(hMem);
    if (hMem) GlobalFree(hMem);
    if (hdc1) DeleteDC(hdc1);
    if (hdc2) DeleteDC(hdc2);
}

static void test_LoadImage_DataFile(void)
{
    static const struct
    {
        int result;
        LPCWSTR file;
        int res_id;
        UINT lr;
        BOOL same_handle;
        BOOL after_unload; /* LR_SHARED stays valid */
    }
    tests[] =
    {
        { 1, L"shell32.dll", 2,        0,         0, 0 },
        { 1, L"shell32.dll", 2,        LR_SHARED, 1, 1 },
        { 0, L"shell32.dll", 0xfff0,   0,         1, 0 }, /* Icon should not exist */
        { 1, L"regedit.exe", 100,      0,         0, 0 },
        { 1, L"regedit.exe", 100,      LR_SHARED, 1, 1 }
    };

    SIZE_T i;
    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        HANDLE handle1, handle2;
        HMODULE hMod = LoadLibraryExW(tests[i].file, NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (!((SIZE_T)hMod & 3))
        {
            skip("Could not load library as datafile %ls\n", tests[i].file);
            continue;
        }

        handle1 = LoadImage(hMod, MAKEINTRESOURCE(tests[i].res_id), IMAGE_ICON, 0, 0, tests[i].lr);
        ok(!!handle1 == !!tests[i].result, "Failed to load %ls,-%d from %p\n", tests[i].file, tests[i].res_id, hMod);

        handle2 = LoadImage(hMod, MAKEINTRESOURCE(tests[i].res_id), IMAGE_ICON, 0, 0, tests[i].lr);
        ok(!!(handle1 == handle2) == !!tests[i].same_handle, "Shared handles don't match\n");

        FreeLibrary(hMod);

        handle1 = LoadImage(hMod, MAKEINTRESOURCE(tests[i].res_id), IMAGE_ICON, 0, 0, tests[i].lr);
        ok(!!handle1 == !!tests[i].after_unload, "LR_%x handle should %sload after FreeLibrary\n", tests[i].lr, tests[i].after_unload ? "" : "not ");
    }
}

static void test_LoadIcon_SystemIds(void)
{
    static const WORD icomap[][2] = {
        { 100, (WORD)(SIZE_T)IDI_APPLICATION },
        { 101, (WORD)(SIZE_T)IDI_WARNING },
        { 102, (WORD)(SIZE_T)IDI_QUESTION },
        { 103, (WORD)(SIZE_T)IDI_ERROR },
        { 104, (WORD)(SIZE_T)IDI_INFORMATION },
        { 105, (WORD)(SIZE_T)IDI_WINLOGO }
    };
    HINSTANCE hInst = GetModuleHandleW(L"USER32");
    typedef BOOL (WINAPI*SHAIE)(HICON, HICON);
    typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
    PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext;
    DPI_AWARENESS_CONTEXT OldContext = NULL;
    SHAIE pfnSHAreIconsEqual;
    HMODULE hSHLWAPI = LoadLibraryA("SHLWAPI");
    if (!hSHLWAPI)
    {
        skip("Could not initialize\n");
        return;
    }
    pfnSHAreIconsEqual = (SHAIE)GetProcAddress(hSHLWAPI, MAKEINTRESOURCEA(548));
    if (!pfnSHAreIconsEqual)
    {
        FreeLibrary(hSHLWAPI);
        skip("Could not initialize\n");
        return;
    }

    pSetThreadDpiAwarenessContext = (PSETTHREADDPIAWARENESSCONTEXT)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                                "SetThreadDpiAwarenessContext");
    if (pSetThreadDpiAwarenessContext)
        OldContext = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    for (UINT i = 0; i < _countof(icomap); i++)
    {
        HICON hIcoRes = LoadIconW(hInst, MAKEINTRESOURCEW(icomap[i][0]));
        HICON hIcoSys = LoadIconW(NULL, MAKEINTRESOURCEW(icomap[i][1]));
        ok(hIcoRes && pfnSHAreIconsEqual(hIcoRes, hIcoSys), "SysIcon %d must be resource %d\n", icomap[i][1], icomap[i][0]);
    }
    if (pSetThreadDpiAwarenessContext && OldContext)
        pSetThreadDpiAwarenessContext(OldContext);
    FreeLibrary(hSHLWAPI);
}

static void test_LoadImage_OemShared(void)
{
    HANDLE Image;

    SetLastError(0xdeadbeef);
    Image = LoadImageW(NULL, (LPCWSTR)IDC_ARROW, IMAGE_CURSOR, 0, 0, 0);
    ok(Image == NULL, "OEM cursor without LR_SHARED loaded\n");
    ok(GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND, "OEM cursor error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Image = LoadImageW(NULL, (LPCWSTR)IDC_ARROW, IMAGE_CURSOR, 32, 32, LR_DEFAULTSIZE);
    ok(Image == NULL, "sized OEM cursor without LR_SHARED loaded\n");
    ok(GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND, "sized OEM cursor error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Image = LoadImageW(NULL, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 0, 0, 0);
    ok(Image == NULL, "OEM icon without LR_SHARED loaded\n");
    ok(GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND, "OEM icon error %lu\n", GetLastError());
    ok(LoadImageW(NULL, (LPCWSTR)IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_SHARED) != NULL, "shared OEM cursor failed\n");
    ok(LoadImageW(NULL, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED) != NULL, "shared OEM icon failed\n");
    Image = LoadImageW(NULL, MAKEINTRESOURCEW(32754), IMAGE_BITMAP, 0, 0, 0);
    ok(Image != NULL, "OEM bitmap without LR_SHARED failed\n");
    if (Image)
        DeleteObject(Image);
}

static const BYTE PngCursorImage[] =
{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a,
    0xf4, 0x00, 0x00, 0x00, 0x35, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd6, 0xb1, 0x09, 0x00,
    0x30, 0x0c, 0x03, 0x41, 0xef, 0xbf, 0x74, 0x32, 0x80, 0x0b, 0x57, 0x22, 0x98, 0xdc, 0x83, 0xfa,
    0x2b, 0x55, 0x25, 0xf5, 0x4e, 0x78, 0x33, 0x20, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xc0, 0x1e, 0xc0, 0xeb, 0x5b, 0xae, 0xbf, 0xba, 0x22, 0x68, 0xe4, 0x7e, 0x14, 0xce,
    0xe2, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

static DWORD BuildPngCursorFile(BYTE *Buffer, WORD xHotspot, WORD yHotspot)
{
    WORD *Header = (WORD *)Buffer;
    BYTE *Entry = Buffer + 3 * sizeof(WORD);
    DWORD Size = sizeof(PngCursorImage), Offset = 3 * sizeof(WORD) + 16;

    Header[0] = 0;
    Header[1] = 2;
    Header[2] = 1;
    Entry[0] = 32;
    Entry[1] = 32;
    Entry[2] = 0;
    Entry[3] = 0;
    memcpy(Entry + 4, &xHotspot, sizeof(WORD));
    memcpy(Entry + 6, &yHotspot, sizeof(WORD));
    memcpy(Entry + 8, &Size, sizeof(DWORD));
    memcpy(Entry + 12, &Offset, sizeof(DWORD));
    memcpy(Buffer + Offset, PngCursorImage, Size);
    return Offset + Size;
}

static DWORD AppendChunk(BYTE *Buffer, DWORD Position, const char *Id, const void *Data, DWORD Size)
{
    memcpy(Buffer + Position, Id, 4);
    memcpy(Buffer + Position + 4, &Size, sizeof(DWORD));
    memcpy(Buffer + Position + 8, Data, Size);
    Position += 8 + Size;
    if (Size & 1)
        Buffer[Position++] = 0;
    return Position;
}

static HANDLE LoadCursorFileData(const BYTE *Data, DWORD Size)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    HANDLE File, Cursor = NULL;
    DWORD Written;

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    if (!GetTempFileNameW(Directory, L"cur", 0, Path))
        return NULL;
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File != INVALID_HANDLE_VALUE)
    {
        WriteFile(File, Data, Size, &Written, NULL);
        CloseHandle(File);
        Cursor = LoadImageW(NULL, Path, IMAGE_CURSOR, 32, 32, LR_LOADFROMFILE);
    }
    DeleteFileW(Path);
    return Cursor;
}

static void CheckPngCursor(const char *Name, HANDLE Cursor)
{
    ICONINFO Info;

    ok(Cursor != NULL, "%s with PNG frames failed to load: %lu\n", Name, GetLastError());
    if (!Cursor)
        return;
    ok(GetIconInfo(Cursor, &Info), "GetIconInfo failed for the %s\n", Name);
    ok(!Info.fIcon, "%s loaded as an icon\n", Name);
    ok(Info.xHotspot == 5 && Info.yHotspot == 7, "%s hotspot %lu,%lu, expected 5,7\n", Name, Info.xHotspot,
       Info.yHotspot);
    if (Info.hbmColor)
        DeleteObject(Info.hbmColor);
    if (Info.hbmMask)
        DeleteObject(Info.hbmMask);
    DestroyCursor(Cursor);
}

static void test_LoadImage_PngCursorFiles(void)
{
    static const DWORD Header[9] = { 36, 2, 2, 0, 0, 0, 0, 2, 1 };
    BYTE Cursor[256], Animated[1024], List[600];
    DWORD CursorSize, ListSize, AnimatedSize, RiffSize;

    CursorSize = BuildPngCursorFile(Cursor, 5, 7);
    CheckPngCursor("cursor file", LoadCursorFileData(Cursor, CursorSize));

    Cursor[CursorSize] = 0;
    memcpy(List, "fram", 4);
    ListSize = AppendChunk(List, 4, "icon", Cursor, CursorSize + 1);
    ListSize = AppendChunk(List, ListSize, "icon", Cursor, CursorSize + 1);
    memcpy(Animated, "RIFF", 4);
    memcpy(Animated + 8, "ACON", 4);
    AnimatedSize = AppendChunk(Animated, 12, "anih", Header, sizeof(Header));
    AnimatedSize = AppendChunk(Animated, AnimatedSize, "LIST", List, ListSize);
    RiffSize = AnimatedSize - 8;
    memcpy(Animated + 4, &RiffSize, sizeof(DWORD));
    CheckPngCursor("animated cursor file", LoadCursorFileData(Animated, AnimatedSize));
}

START_TEST(LoadImage)
{
    char path[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    HANDLE handle;

    char **test_argv;
    int argc = winetest_get_mainargs( &test_argv );

    /* Now check its behaviour regarding Shared icons/cursors */
    handle = LoadImageW( GetModuleHandle(NULL), L"TESTCURSOR", IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE );
    ok(handle != 0, "\n");

    if (argc >= 3)
    {
        HANDLE arg;
        HICON hCopy;
        HBITMAP hbmp;
        HDC hdc, hdcScreen;
        ICONINFO ii;

        sscanf (test_argv[2], "%Iu", (ULONG_PTR*) &arg);

        ok(handle != arg, "Got same handles\n");

        /* Try copying it */
        hCopy = CopyIcon(arg);
        ok(hCopy != NULL, "\n");
        ok(DestroyIcon(hCopy), "\n");

        hCopy = CopyImage(arg, IMAGE_CURSOR, 0, 0, 0);
        ok(hCopy != NULL, "\n");
        ok(DestroyIcon(hCopy), "\n");
        /* Unlike the original, this one is not shared */
        ok(!DestroyIcon(hCopy), "\n");

        hCopy = CopyImage(arg, IMAGE_CURSOR, 0, 0, LR_COPYFROMRESOURCE);
        ok(hCopy != NULL, "\n");
        ok(DestroyIcon(hCopy), "\n");
        /* Unlike the original, this one is not shared */
        ok(!DestroyIcon(hCopy), "\n");

        hCopy = CopyImage(arg, IMAGE_CURSOR, 0, 0, LR_COPYFROMRESOURCE | LR_SHARED);
        ok(hCopy != NULL, "\n");
        ok(DestroyIcon(hCopy), "\n");
        /* This one is shared */
        ok(DestroyIcon(hCopy), "\n");

        hCopy = CopyImage(arg, IMAGE_CURSOR, 0, 0, LR_SHARED);
        ok(hCopy != NULL, "\n");
        ok(DestroyIcon(hCopy), "DestroyIcon should succeed.\n");
        /* This one is shared */
        ok(DestroyIcon(hCopy) == 0, "DestroyIcon should fail.\n");

        /* Try various usual functions */
        hdcScreen = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
        ok(hdcScreen != NULL, "\n");
        hdc = CreateCompatibleDC(hdcScreen);
        ok(hdc != NULL, "\n");
        hbmp = CreateCompatibleBitmap(hdcScreen, 64, 64);
        ok(hbmp != NULL, "\n");
        hbmp = SelectObject(hdc, hbmp);
        ok(hbmp != NULL, "\n");

        ok(DrawIcon(hdc, 0, 0, arg), "\n");
        hbmp = SelectObject(hdc, hbmp);
        DeleteObject(hbmp);
        DeleteDC(hdc);
        DeleteDC(hdcScreen);

        ok(GetIconInfo(arg, &ii), "\n");
        ok(ii.hbmMask != NULL, "\n");
        DeleteObject(ii.hbmMask);
        if(ii.hbmColor) DeleteObject(ii.hbmColor);

        /* LOAD_LIBRARY_AS_DATAFILE */
        test_LoadImage_DataFile();

        return;
    }

    /* Start child process */
    sprintf( path, "%s LoadImage %Iu", test_argv[0], (ULONG_PTR)handle );
    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    CreateProcessA( NULL, path, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi );
    WaitForSingleObject (pi.hProcess, INFINITE);

    test_LoadIcon_SystemIds();
    test_LoadImage_1bpp();
    test_LoadImage_OemShared();
    test_LoadImage_PngCursorFiles();
}
