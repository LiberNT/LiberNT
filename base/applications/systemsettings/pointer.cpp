/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mouse pointer style, color and size
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define OEMRESOURCE
#include "settings.h"

#define CURSORS_KEY L"Control Panel\\Cursors"
#define ACCESSIBILITY_KEY L"Software\\Microsoft\\Accessibility"

struct PointerRole
{
    LPCWSTR Value;
    LPCWSTR File;
    WORD Resource;
};

static const PointerRole PointerRoles[] =
{
    { L"Arrow", L"arrow", OCR_NORMAL },
    { L"Help", L"helpsel", OCR_HELP },
    { L"AppStarting", L"busy", OCR_APPSTARTING },
    { L"Wait", L"wait", OCR_WAIT },
    { L"Crosshair", L"cross", OCR_CROSS },
    { L"IBeam", L"ibeam", OCR_IBEAM },
    { L"No", L"unavail", OCR_NO },
    { L"SizeNS", L"ns", OCR_SIZENS },
    { L"SizeWE", L"ew", OCR_SIZEWE },
    { L"SizeNWSE", L"nwse", OCR_SIZENWSE },
    { L"SizeNESW", L"nesw", OCR_SIZENESW },
    { L"SizeAll", L"move", OCR_SIZEALL },
    { L"UpArrow", L"up", OCR_UP },
    { L"Hand", L"link", OCR_HAND },
};

static const int PointerImageSizes[] = { 64, 96, 128, 192, 256 };
static const DWORD PointerTypes[] = { 3, 4, 2, 6 };

struct PointerImage
{
    int Width = 0;
    int Height = 0;
    POINT Hotspot = { 0, 0 };
    std::vector<DWORD> Pixels;
};

static bool
DecodeDib(const BYTE *data, DWORD size, POINT hotspot, PointerImage &image)
{
    const BITMAPINFOHEADER *header = (const BITMAPINFOHEADER *)data;
    const RGBQUAD *palette;
    const BYTE *colorBits, *maskBits;
    DWORD colors, colorStride, maskStride;
    bool alpha = false;
    int width, height;

    if (size < sizeof(*header) || header->biSize < sizeof(*header) || header->biWidth <= 0 ||
        header->biHeight <= 0 || (header->biBitCount != 1 && header->biBitCount != 32))
    {
        return false;
    }
    width = header->biWidth;
    height = header->biHeight / 2;
    colors = header->biBitCount == 1 ? (header->biClrUsed ? header->biClrUsed : 2) : 0;
    palette = (const RGBQUAD *)(data + header->biSize);
    colorBits = (const BYTE *)(palette + colors);
    colorStride = ((width * header->biBitCount + 31) / 32) * 4;
    maskStride = ((width + 31) / 32) * 4;
    maskBits = colorBits + colorStride * height;
    if ((DWORD)(maskBits + maskStride * height - data) > size)
        return false;

    if (header->biBitCount == 32)
    {
        for (int i = 0; i < width * height && !alpha; i++)
            alpha = colorBits[i * 4 + 3] != 0;
    }

    image.Width = width;
    image.Height = height;
    image.Hotspot = hotspot;
    image.Pixels.assign(width * height, 0);
    for (int y = 0; y < height; y++)
    {
        int row = height - 1 - y;

        for (int x = 0; x < width; x++)
        {
            bool transparent = ((maskBits[row * maskStride + x / 8] >> (7 - x % 8)) & 1) != 0;
            DWORD pixel = 0;

            if (header->biBitCount == 32)
            {
                const BYTE *source = colorBits + row * colorStride + x * 4;
                BYTE a = alpha ? source[3] : (transparent ? 0 : 255);

                if (a)
                    pixel = ((DWORD)a << 24) | ((DWORD)source[2] << 16) | ((DWORD)source[1] << 8) | source[0];
            }
            else if (!transparent)
            {
                const RGBQUAD &entry = palette[(colorBits[row * colorStride + x / 8] >> (7 - x % 8)) & 1];

                pixel = 0xFF000000 | ((DWORD)entry.rgbRed << 16) | ((DWORD)entry.rgbGreen << 8) | entry.rgbBlue;
            }
            image.Pixels[y * width + x] = pixel;
        }
    }
    return true;
}

static bool
DecodeCursorFile(const BYTE *data, DWORD size, PointerImage &image)
{
    WORD count, best = 0, bestBits = 0;

    if (size < 6 || ((const WORD *)data)[1] != 2)
        return false;
    count = ((const WORD *)data)[2];
    if (!count || 6u + count * 16u > size)
        return false;
    for (WORD i = 0; i < count; i++)
    {
        const BYTE *entry = data + 6 + i * 16;
        DWORD offset = *(const DWORD *)(entry + 12);

        if (offset + sizeof(BITMAPINFOHEADER) <= size &&
            ((const BITMAPINFOHEADER *)(data + offset))->biBitCount > bestBits)
        {
            best = i;
            bestBits = ((const BITMAPINFOHEADER *)(data + offset))->biBitCount;
        }
    }
    {
        const BYTE *entry = data + 6 + best * 16;
        DWORD length = *(const DWORD *)(entry + 8), offset = *(const DWORD *)(entry + 12);
        POINT hotspot = { *(const WORD *)(entry + 4), *(const WORD *)(entry + 6) };

        if (offset > size || length > size - offset)
            return false;
        return DecodeDib(data + offset, length, hotspot, image);
    }
}

static bool
DecodeAnimatedFirstFrame(const BYTE *data, DWORD size, PointerImage &image)
{
    DWORD offset = 12;

    if (size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "ACON", 4))
        return false;
    while (offset + 8 <= size)
    {
        DWORD length = *(const DWORD *)(data + offset + 4);

        if (length > size - offset - 8)
            return false;
        if (!memcmp(data + offset, "LIST", 4) && length >= 4 && !memcmp(data + offset + 8, "fram", 4))
        {
            DWORD frame = offset + 12, end = offset + 8 + length;

            while (frame + 8 <= end)
            {
                DWORD frameLength = *(const DWORD *)(data + frame + 4);

                if (frameLength > end - frame - 8)
                    return false;
                if (!memcmp(data + frame, "icon", 4))
                    return DecodeCursorFile(data + frame + 8, frameLength, image);
                frame += 8 + frameLength + (frameLength & 1);
            }
            return false;
        }
        offset += 8 + length + (length & 1);
    }
    return false;
}

static bool
LoadPointerSource(HMODULE module, WORD id, PointerImage &image)
{
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_GROUP_CURSOR);

    if (resource)
    {
        const BYTE *directory = (const BYTE *)LockResource(LoadResource(module, resource));
        DWORD directorySize = SizeofResource(module, resource);
        WORD count, best = 0, bestBits = 0;
        const BYTE *data;
        DWORD size;

        if (!directory || directorySize < 6)
            return false;
        count = ((const WORD *)directory)[2];
        for (WORD i = 0; i < count && 6u + (i + 1u) * 14u <= directorySize; i++)
        {
            const BYTE *entry = directory + 6 + i * 14;

            if (!best || *(const WORD *)(entry + 6) > bestBits)
            {
                best = *(const WORD *)(entry + 12);
                bestBits = *(const WORD *)(entry + 6);
            }
        }
        resource = best ? FindResourceW(module, MAKEINTRESOURCEW(best), RT_CURSOR) : NULL;
        data = resource ? (const BYTE *)LockResource(LoadResource(module, resource)) : NULL;
        size = resource ? SizeofResource(module, resource) : 0;
        if (!data || size < 4)
            return false;
        return DecodeDib(data + 4, size - 4, { ((const WORD *)data)[0], ((const WORD *)data)[1] }, image);
    }

    resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_ANICURSOR);
    if (!resource)
        return false;
    return DecodeAnimatedFirstFrame((const BYTE *)LockResource(LoadResource(module, resource)),
                                    SizeofResource(module, resource), image);
}

static PointerImage
ScalePointer(const PointerImage &source, int size)
{
    PointerImage image;

    image.Width = size;
    image.Height = size * source.Height / source.Width;
    image.Hotspot.x = source.Hotspot.x * size / source.Width;
    image.Hotspot.y = source.Hotspot.y * size / source.Width;
    image.Pixels.resize(image.Width * image.Height);
    for (int y = 0; y < image.Height; y++)
    {
        for (int x = 0; x < image.Width; x++)
            image.Pixels[y * image.Width + x] =
                source.Pixels[(y * source.Height / image.Height) * source.Width + x * source.Width / image.Width];
    }
    return image;
}

static void
StylePointer(PointerImage &image, PointerStyle style, COLORREF color)
{
    for (DWORD &pixel : image.Pixels)
    {
        DWORD a = pixel >> 24, r = (pixel >> 16) & 0xFF, g = (pixel >> 8) & 0xFF, b = pixel & 0xFF;

        if (!a || max(r, max(g, b)) - min(r, min(g, b)) >= 48)
            continue;
        if (style == PointerStyle::Black && a >= 128)
        {
            r = 255 - r;
            g = 255 - g;
            b = 255 - b;
        }
        else if (style == PointerStyle::Custom)
        {
            DWORD luminance = (r * 299 + g * 587 + b * 114) / 1000;

            r = GetRValue(color) * luminance / 255;
            g = GetGValue(color) * luminance / 255;
            b = GetBValue(color) * luminance / 255;
        }
        pixel = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

static void
AppendBytes(std::vector<BYTE> &data, const void *bytes, size_t length)
{
    data.insert(data.end(), (const BYTE *)bytes, (const BYTE *)bytes + length);
}

static std::vector<BYTE>
EncodeImage(const PointerImage &image, PointerStyle style)
{
    std::vector<BYTE> data;
    BITMAPINFOHEADER header = { sizeof(header) };
    bool inverted = style == PointerStyle::Inverted;
    DWORD colorStride = inverted ? ((image.Width + 31) / 32) * 4 : image.Width * 4;
    DWORD maskStride = ((image.Width + 31) / 32) * 4;
    std::vector<BYTE> colorBits(colorStride * image.Height), maskBits(maskStride * image.Height);

    for (int y = 0; y < image.Height; y++)
    {
        int row = image.Height - 1 - y;

        for (int x = 0; x < image.Width; x++)
        {
            DWORD pixel = image.Pixels[y * image.Width + x];
            BYTE a = (BYTE)(pixel >> 24);

            if (inverted)
            {
                if (a >= 128)
                    colorBits[row * colorStride + x / 8] |= (BYTE)(0x80 >> (x % 8));
                maskBits[row * maskStride + x / 8] |= (BYTE)(0x80 >> (x % 8));
                continue;
            }
            colorBits[row * colorStride + x * 4 + 0] = (BYTE)pixel;
            colorBits[row * colorStride + x * 4 + 1] = (BYTE)(pixel >> 8);
            colorBits[row * colorStride + x * 4 + 2] = (BYTE)(pixel >> 16);
            colorBits[row * colorStride + x * 4 + 3] = a;
            if (!a)
                maskBits[row * maskStride + x / 8] |= (BYTE)(0x80 >> (x % 8));
        }
    }

    header.biWidth = image.Width;
    header.biHeight = image.Height * 2;
    header.biPlanes = 1;
    header.biBitCount = inverted ? 1 : 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = (DWORD)(colorBits.size() + maskBits.size());
    AppendBytes(data, &header, sizeof(header));
    if (inverted)
    {
        static const RGBQUAD Palette[2] = { { 0, 0, 0, 0 }, { 255, 255, 255, 0 } };

        AppendBytes(data, Palette, sizeof(Palette));
    }
    AppendBytes(data, colorBits.data(), colorBits.size());
    AppendBytes(data, maskBits.data(), maskBits.size());
    return data;
}

static std::vector<BYTE>
EncodeCursor(const PointerImage &source, PointerStyle style, COLORREF color, int baseSize)
{
    std::vector<std::vector<BYTE>> images;
    std::vector<PointerImage> scaled;
    std::vector<int> sizes(1, baseSize);
    std::vector<BYTE> file;
    WORD header[3] = { 0, 2, 0 };
    DWORD offset;

    for (int size : PointerImageSizes)
    {
        if (size > baseSize)
            sizes.push_back(size);
    }
    for (int size : sizes)
    {
        PointerImage image = ScalePointer(source, size);

        StylePointer(image, style, color);
        images.push_back(EncodeImage(image, style));
        scaled.push_back(image);
    }
    header[2] = (WORD)images.size();
    offset = (DWORD)(sizeof(header) + images.size() * 16);
    AppendBytes(file, header, sizeof(header));
    for (size_t i = 0; i < images.size(); i++)
    {
        BYTE entry[16] = { 0 };

        entry[0] = (BYTE)(scaled[i].Width >= 256 ? 0 : scaled[i].Width);
        entry[1] = (BYTE)(scaled[i].Height >= 256 ? 0 : scaled[i].Height);
        *(WORD *)(entry + 4) = (WORD)scaled[i].Hotspot.x;
        *(WORD *)(entry + 6) = (WORD)scaled[i].Hotspot.y;
        *(DWORD *)(entry + 8) = (DWORD)images[i].size();
        *(DWORD *)(entry + 12) = offset;
        offset += (DWORD)images[i].size();
        AppendBytes(file, entry, sizeof(entry));
    }
    for (const std::vector<BYTE> &image : images)
        AppendBytes(file, image.data(), image.size());
    return file;
}

static bool
WriteFileData(const std::wstring &path, const std::vector<BYTE> &data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok;

    if (file == INVALID_HANDLE_VALUE)
        return false;
    ok = WriteFile(file, data.data(), (DWORD)data.size(), &written, NULL) && written == data.size();
    CloseHandle(file);
    return ok != FALSE;
}

static std::wstring
PointerDirectory()
{
    WCHAR path[MAX_PATH];

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path)))
        return std::wstring();
    return std::wstring(path) + L"\\Microsoft\\Windows\\Cursors";
}

static void
SetCursorValue(HKEY key, LPCWSTR name, const std::wstring &path)
{
    RegSetValueExW(key, name, 0, path.empty() ? REG_SZ : REG_EXPAND_SZ, (const BYTE *)path.c_str(),
                   (DWORD)((path.size() + 1) * sizeof(WCHAR)));
}

PointerStyle
CurrentPointerStyle(COLORREF *color)
{
    DWORD type = ReadDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorType", PointerTypes[0]);

    if (color)
        *color = ReadDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorColor", RGB(0xBF, 0xFF, 0x00));
    for (UINT i = 0; i < _countof(PointerTypes); i++)
    {
        if (PointerTypes[i] == type)
            return (PointerStyle)i;
    }
    return PointerStyle::White;
}

int
CurrentPointerSize()
{
    DWORD size = ReadDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorSize", 0);

    if (size < 1 || size > 15)
    {
        DWORD base = ReadDword(HKEY_CURRENT_USER, CURSORS_KEY, L"CursorBaseSize", 32);

        size = base >= 32 && base <= 256 ? (base - 32) / 16 + 1 : 1;
    }
    return (int)size;
}

bool
ApplyPointer(PointerStyle style, COLORREF color, int size)
{
    static const LPCWSTR Schemes[] = { L"LiberNT Default", L"LiberNT Black", L"LiberNT Inverted", L"LiberNT Default" };
    bool generate, ok = true;
    std::wstring directory;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    int baseSize;
    HKEY key;

    size = max(1, min(15, size));
    baseSize = 32 + 16 * (size - 1);
    generate = style != PointerStyle::White || size > 1;
    if (generate)
    {
        directory = PointerDirectory();
        if (directory.empty())
            return false;
        SHCreateDirectoryExW(NULL, directory.c_str(), NULL);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CURSORS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    for (const PointerRole &role : PointerRoles)
    {
        PointerImage source;
        std::wstring path;

        if (generate)
        {
            path = directory + L"\\" + role.File + L"_eoa.cur";
            if (!LoadPointerSource(user32, role.Resource, source) ||
                !WriteFileData(path, EncodeCursor(source, style, color, baseSize)))
            {
                ok = false;
                path.clear();
            }
        }
        SetCursorValue(key, role.Value, path);
    }
    RegSetValueExW(key, NULL, 0, REG_SZ, (const BYTE *)Schemes[(int)style],
                   (DWORD)((wcslen(Schemes[(int)style]) + 1) * sizeof(WCHAR)));
    RegSetValueExW(key, L"CursorBaseSize", 0, REG_DWORD, (const BYTE *)&baseSize, sizeof(DWORD));
    RegCloseKey(key);

    WriteDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorType", PointerTypes[(int)style]);
    WriteDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorSize", (DWORD)size);
    if (style == PointerStyle::Custom)
        WriteDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorColor", color & 0x00FFFFFF);
    SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    return ok;
}
