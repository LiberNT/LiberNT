/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Personalization pages: mode, effects, background, taskbar and fonts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
extern "C"
{
#include "../../system/dwm/settings.h"
}
#include <commdlg.h>
#include <shobjidl.h>
#include <algorithm>

#define PERSONALIZE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define EXPLORER_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
#define ADVANCED_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define DESKTOP_KEY L"Control Panel\\Desktop"

static void
NotifyCompositor()
{
    HWND compositor = FindWindowExW(HWND_MESSAGE, NULL, DWM_SETTINGS_CLASS, NULL);
    DWORD_PTR reply = 0;

    if (compositor)
        SendMessageTimeoutW(compositor, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &reply);
}

static void
SetDarkMode(bool dark)
{
    DWORD light = dark ? 0 : 1;

    WriteDword(HKEY_CURRENT_USER, PERSONALIZE_KEY, L"AppsUseLightTheme", light);
    WriteDword(HKEY_CURRENT_USER, PERSONALIZE_KEY, L"SystemUsesLightTheme", light);
    DwmSettingsWriteColorScheme(dark ? DWM_COLOR_SCHEME_DARK : DWM_COLOR_SCHEME_LIGHT);
    BroadcastSetting(L"ImmersiveColorSet");
    BroadcastSetting(REACTOS_DWM_SETTINGS_KEY);
}

static void
SetEffect(DWORD effect, bool on)
{
    DwmSettingsWrite(effect, on);
    if (effect == DWM_EFFECT_ACRYLIC)
    {
        DwmSettingsWrite(DWM_EFFECT_BLUR, on);
        WriteDword(HKEY_CURRENT_USER, PERSONALIZE_KEY, L"EnableTransparency", on);
        BroadcastSetting(L"ImmersiveColorSet");
    }
    NotifyCompositor();
}

void
BuildPersonalization(PageBuilder &page)
{
    std::vector<std::wstring> modes;
    DWORD effects = DWM_EFFECT_ALL;
    bool dark = ReadDword(HKEY_CURRENT_USER, PERSONALIZE_KEY, L"AppsUseLightTheme",
                          DwmSettingsReadColorScheme() == DWM_COLOR_SCHEME_LIGHT) == 0;

    if (!page.Indexing)
        DwmSettingsRead(&effects);
    modes.push_back(Str(IDS_LIGHT));
    modes.push_back(Str(IDS_DARK));
    page.Group(Str(IDS_APPEARANCE));
    page.Choice(Str(IDS_MODE), Str(IDS_MODE_DESC), L"dark_theme_20_regular", modes, dark ? 1 : 0,
                [](int index) { SetDarkMode(index == 1); }).Keywords = Str(IDS_MODE_KEYWORDS);
    page.Toggle(Str(IDS_TRANSPARENCY), Str(IDS_TRANSPARENCY_DESC), L"window_apps_20_regular",
                (effects & DWM_EFFECT_ACRYLIC) != 0, [](int on) { SetEffect(DWM_EFFECT_ACRYLIC, on != 0); });

    page.Group(Str(IDS_VISUAL_EFFECTS));
    page.Toggle(Str(IDS_ANIMATIONS), Str(IDS_ANIMATIONS_DESC), L"slide_text_20_regular",
                (effects & DWM_EFFECT_ANIMATIONS) != 0, [](int on) { SetEffect(DWM_EFFECT_ANIMATIONS, on != 0); });
    page.Toggle(Str(IDS_SHADOWS), Str(IDS_SHADOWS_DESC), L"window_multiple_20_regular",
                (effects & DWM_EFFECT_SHADOWS) != 0, [](int on) { SetEffect(DWM_EFFECT_SHADOWS, on != 0); });
    page.Toggle(Str(IDS_CONTENT_BLUR), Str(IDS_CONTENT_BLUR_DESC), L"window_apps_20_regular",
                (effects & DWM_EFFECT_CONTENT_BLUR) != 0, [](int on) { SetEffect(DWM_EFFECT_CONTENT_BLUR, on != 0); });

    page.Group();
    page.Navigate(Str(IDS_PAGE_BACKGROUND), Str(IDS_BACKGROUND_DESC), L"image_20_regular", PageId::Background)
        .Keywords = Str(IDS_BACKGROUND_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_COLORS), Str(IDS_COLORS_DESC), L"color_20_regular", PageId::Colors)
        .Keywords = Str(IDS_COLORS_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_LOCKSCREEN), Str(IDS_LOCKSCREEN_DESC), L"lock_closed_20_regular", PageId::LockScreen)
        .Keywords = Str(IDS_LOCKSCREEN_KEYWORDS);
    page.Navigate(Str(IDS_PAGE_TASKBAR), Str(IDS_TASKBAR_DESC), L"panel_bottom_20_regular", PageId::Taskbar);
    page.Navigate(Str(IDS_PAGE_FONTS), Str(IDS_FONTS_DESC), L"text_font_20_regular", PageId::Fonts);
}

struct WallpaperStyle
{
    UINT Name;
    LPCWSTR Style;
    LPCWSTR Tile;
};

static const WallpaperStyle WallpaperStyles[] =
{
    { IDS_FIT_FILL, L"10", L"0" },
    { IDS_FIT_FIT, L"6", L"0" },
    { IDS_FIT_STRETCH, L"2", L"0" },
    { IDS_FIT_TILE, L"0", L"1" },
    { IDS_FIT_CENTER, L"0", L"0" },
};

static std::wstring
CurrentWallpaper()
{
    WCHAR path[MAX_PATH] = L"";

    SystemParametersInfoW(SPI_GETDESKWALLPAPER, _countof(path), path, 0);
    return path;
}

static void
SetWallpaper(HWND owner, const std::wstring &path)
{
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        ShowError(owner, GetLastError());
}

static HICON
Thumbnail(const std::wstring &path, int size)
{
    IShellItemImageFactory *factory = NULL;
    HBITMAP bitmap = NULL;
    HICON icon = NULL;
    SIZE extent = { size, size };

    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), NULL, IID_PPV_ARG(IShellItemImageFactory, &factory))))
    {
        if (SUCCEEDED(factory->GetImage(extent, SIIGBF_BIGGERSIZEOK, &bitmap)))
        {
            ICONINFO info = { TRUE };
            BITMAP data;
            GetObjectW(bitmap, sizeof(data), &data);
            info.hbmColor = bitmap;
            info.hbmMask = CreateBitmap(data.bmWidth, data.bmHeight, 1, 1, NULL);
            icon = CreateIconIndirect(&info);
            DeleteObject(info.hbmMask);
            DeleteObject(bitmap);
        }
        factory->Release();
    }
    return icon;
}

static void
CollectImages(const std::wstring &folder, int depth, std::vector<std::wstring> &images)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);

    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        std::wstring path = folder + L"\\" + data.cFileName;
        LPCWSTR extension = PathFindExtensionW(data.cFileName);
        if (data.cFileName[0] == L'.')
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth > 0)
                CollectImages(path, depth - 1, images);
            continue;
        }
        if (!lstrcmpiW(extension, L".jpg") || !lstrcmpiW(extension, L".jpeg") || !lstrcmpiW(extension, L".png") ||
            !lstrcmpiW(extension, L".bmp"))
            images.push_back(path);
    } while (FindNextFileW(find, &data) && images.size() < 48);
    FindClose(find);
}

static const COLORREF BackgroundColors[] =
{
    RGB(0x00, 0x00, 0x00), RGB(0x20, 0x20, 0x20), RGB(0x00, 0x2B, 0x55), RGB(0x00, 0x63, 0xB1),
    RGB(0x00, 0x78, 0x73), RGB(0x10, 0x7C, 0x10), RGB(0x5C, 0x2D, 0x91), RGB(0x88, 0x17, 0x98),
    RGB(0xC3, 0x00, 0x52), RGB(0xCA, 0x50, 0x10), RGB(0x76, 0x76, 0x76), RGB(0x3A, 0x6E, 0xA5),
};

static const UINT BackgroundColorNames[] =
{
    IDS_COLOR_BLACK, IDS_COLOR_GRAPHITE, IDS_COLOR_NAVY, IDS_COLOR_BLUE, IDS_COLOR_TEAL, IDS_COLOR_GREEN,
    IDS_COLOR_PURPLE, IDS_COLOR_ORCHID, IDS_COLOR_RUBY, IDS_COLOR_ORANGE, IDS_COLOR_GRAY, IDS_COLOR_STEEL,
};

static void
SetBackgroundColor(COLORREF color)
{
    INT element = COLOR_DESKTOP;

    SetSysColors(1, &element, &color);
    WriteString(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background",
                Printf(L"%u %u %u", GetRValue(color), GetGValue(color), GetBValue(color)));
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)L"", SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

static bool
BrowseImage(HWND owner, std::wstring &path)
{
    WCHAR file[MAX_PATH] = L"";
    std::wstring filter = Str(IDS_IMAGE_FILTER);
    OPENFILENAMEW ofn = { sizeof(ofn) };

    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = _countof(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn))
        return false;
    path = file;
    return true;
}

void
BuildBackground(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::wstring wallpaper = page.Indexing ? std::wstring() : CurrentWallpaper(), style, tile;
    std::vector<std::wstring> kinds, fits, colors, images;
    bool picture = !wallpaper.empty();
    int fit = -1, colorIndex = -1;
    WCHAR windows[MAX_PATH];

    if (!page.Indexing)
    {
        SettingRow &hero = page.Hero(picture ? std::wstring(PathFindFileNameW(wallpaper.c_str())) : Str(IDS_SOLID_COLOR),
                                     picture ? wallpaper : Str(IDS_NO_PICTURE), L"image_20_regular");
        if (picture)
            hero.Image = Thumbnail(wallpaper, 64);
    }
    kinds.push_back(Str(IDS_PICTURE));
    kinds.push_back(Str(IDS_SOLID_COLOR));
    page.Group();
    page.Choice(Str(IDS_BACKGROUND_KIND), std::wstring(), L"image_20_regular", kinds, picture ? 0 : 1,
                [host](int index)
                {
                    if (index == 1)
                        SetBackgroundColor(GetSysColor(COLOR_DESKTOP));
                    else
                    {
                        std::wstring path;
                        if (BrowseImage(host->Window(), path))
                            SetWallpaper(host->Window(), path);
                    }
                });
    if (picture || page.Indexing)
    {
        style = ReadString(HKEY_CURRENT_USER, DESKTOP_KEY, L"WallpaperStyle");
        tile = ReadString(HKEY_CURRENT_USER, DESKTOP_KEY, L"TileWallpaper");
        for (UINT i = 0; i < _countof(WallpaperStyles); i++)
        {
            fits.push_back(Str(WallpaperStyles[i].Name));
            if (style == WallpaperStyles[i].Style && (tile.empty() ? L"0" : tile) == WallpaperStyles[i].Tile)
                fit = (int)i;
        }
        page.Choice(Str(IDS_FIT), Str(IDS_FIT_DESC), L"image_edit_20_regular", fits, fit,
                    [host, wallpaper](int index)
                    {
                        WriteString(HKEY_CURRENT_USER, DESKTOP_KEY, L"WallpaperStyle", WallpaperStyles[index].Style);
                        WriteString(HKEY_CURRENT_USER, DESKTOP_KEY, L"TileWallpaper", WallpaperStyles[index].Tile);
                        SetWallpaper(host->Window(), wallpaper);
                    });
        page.Button(Str(IDS_BROWSE_PHOTOS), Str(IDS_BROWSE_PHOTOS_DESC), L"folder_open_20_regular", Str(IDS_BROWSE),
                    [host](int)
                    {
                        std::wstring path;
                        if (BrowseImage(host->Window(), path))
                            SetWallpaper(host->Window(), path);
                    });
        if (!page.Indexing && GetWindowsDirectoryW(windows, _countof(windows)))
            CollectImages(std::wstring(windows) + L"\\Web\\Wallpaper", 2, images);
        if (!images.empty())
            page.Group(Str(IDS_GALLERY));
        for (size_t i = 0; i < images.size(); i++)
        {
            std::wstring path = images[i];
            SettingRow &row = page.Button(PathFindFileNameW(path.c_str()), std::wstring(), NULL,
                                          Str(lstrcmpiW(path.c_str(), wallpaper.c_str()) ? IDS_USE : IDS_IN_USE),
                                          [host, path](int) { SetWallpaper(host->Window(), path); });
            row.Image = Thumbnail(path, 48);
            row.Enabled = lstrcmpiW(path.c_str(), wallpaper.c_str()) != 0;
        }
    }
    if (!picture || page.Indexing)
    {
        COLORREF current = GetSysColor(COLOR_DESKTOP);
        for (UINT i = 0; i < _countof(BackgroundColors); i++)
        {
            colors.push_back(Str(BackgroundColorNames[i]));
            if (BackgroundColors[i] == current)
                colorIndex = (int)i;
        }
        if (colorIndex < 0)
        {
            colors.push_back(Printf(L"RGB(%u, %u, %u)", GetRValue(current), GetGValue(current), GetBValue(current)));
            colorIndex = (int)colors.size() - 1;
        }
        page.Choice(Str(IDS_BACKGROUND_COLOR), std::wstring(), L"color_20_regular", colors, colorIndex,
                    [](int index)
                    {
                        if (index < (int)_countof(BackgroundColors))
                            SetBackgroundColor(BackgroundColors[index]);
                    });
    }
}

#define ACCENT_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent"

static const BYTE DefaultAccentPalette[32] =
{
    0x99, 0xEB, 0xFF, 0x00, 0x4C, 0xC2, 0xFF, 0x00, 0x00, 0x91, 0xF8, 0x00, 0x00, 0x78, 0xD4, 0x00,
    0x00, 0x67, 0xC0, 0x00, 0x00, 0x3E, 0x92, 0x00, 0x00, 0x1A, 0x68, 0x00, 0xF7, 0x63, 0x0C, 0x00,
};

static const COLORREF AccentColors[] =
{
    RGB(0x00, 0x78, 0xD4), RGB(0x87, 0x64, 0xB8), RGB(0xE3, 0x00, 0x8C), RGB(0xD1, 0x34, 0x38),
    RGB(0xF7, 0x63, 0x0C), RGB(0xFF, 0xB9, 0x00), RGB(0x10, 0x7C, 0x10), RGB(0x76, 0x76, 0x76),
};

static const UINT AccentNames[] =
{
    IDS_ACCENT_BLUE, IDS_ACCENT_PURPLE, IDS_ACCENT_PINK, IDS_ACCENT_RED, IDS_ACCENT_ORANGE, IDS_ACCENT_YELLOW,
    IDS_ACCENT_GREEN, IDS_ACCENT_GRAPHITE,
};

static COLORREF
MixColor(COLORREF color, COLORREF with, UINT percent)
{
    return RGB((GetRValue(color) * (100 - percent) + GetRValue(with) * percent + 50) / 100,
               (GetGValue(color) * (100 - percent) + GetGValue(with) * percent + 50) / 100,
               (GetBValue(color) * (100 - percent) + GetBValue(with) * percent + 50) / 100);
}

static void
SetAccentColor(UINT index)
{
    static const UINT Mix[] = { 70, 45, 20, 0, 15, 45, 70 };
    BYTE palette[32];
    HKEY key;

    if (index == 0)
    {
        CopyMemory(palette, DefaultAccentPalette, sizeof(palette));
    }
    else
    {
        ZeroMemory(palette, sizeof(palette));
        for (UINT i = 0; i < 8; i++)
        {
            COLORREF color = AccentColors[index];

            if (i < 3)
                color = MixColor(color, RGB(0xFF, 0xFF, 0xFF), Mix[i]);
            else if (i > 3 && i < 7)
                color = MixColor(color, RGB(0x00, 0x00, 0x00), Mix[i]);
            palette[i * 4] = GetRValue(color);
            palette[i * 4 + 1] = GetGValue(color);
            palette[i * 4 + 2] = GetBValue(color);
        }
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ACCENT_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, L"AccentPalette", 0, REG_BINARY, palette, sizeof(palette));
    RegCloseKey(key);
    BroadcastSetting(L"ImmersiveColorSet");
}

static int
CurrentAccent()
{
    BYTE palette[32];
    DWORD size = sizeof(palette);

    if (RegGetValueW(HKEY_CURRENT_USER, ACCENT_KEY, L"AccentPalette", RRF_RT_REG_BINARY, NULL, palette, &size) !=
            ERROR_SUCCESS || size < sizeof(palette))
        return 0;
    for (UINT i = 0; i < _countof(AccentColors); i++)
    {
        if (RGB(palette[12], palette[13], palette[14]) == AccentColors[i])
            return (int)i;
    }
    return -1;
}

void
BuildColors(PageBuilder &page)
{
    std::vector<std::wstring> names;
    int current = page.Indexing ? 0 : CurrentAccent();

    for (UINT i = 0; i < _countof(AccentNames); i++)
        names.push_back(Str(AccentNames[i]));
    if (current < 0)
    {
        names.push_back(Str(IDS_ACCENT_CUSTOM));
        current = (int)names.size() - 1;
    }
    page.Group();
    page.Choice(Str(IDS_ACCENT_COLOR), Str(IDS_ACCENT_COLOR_DESC), L"color_20_regular", names, current,
                [](int index)
                {
                    if (index < (int)_countof(AccentColors))
                        SetAccentColor((UINT)index);
                }).Keywords = Str(IDS_COLORS_KEYWORDS);
}

struct ScreenSaver
{
    std::wstring Path;
    std::wstring Name;
};

static std::vector<ScreenSaver>
ScreenSavers()
{
    std::vector<ScreenSaver> savers;
    WCHAR directory[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (!GetSystemDirectoryW(directory, _countof(directory)))
        return savers;
    find = FindFirstFileW((std::wstring(directory) + L"\\*.scr").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return savers;
    do
    {
        ScreenSaver saver;
        WCHAR name[256];
        HMODULE module;

        saver.Path = std::wstring(directory) + L"\\" + data.cFileName;
        saver.Name = data.cFileName;
        saver.Name.erase(saver.Name.rfind(L'.'));
        module = LoadLibraryExW(saver.Path.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module)
        {
            if (LoadStringW(module, 1, name, _countof(name)) > 0)
                saver.Name = name;
            FreeLibrary(module);
        }
        savers.push_back(saver);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(savers.begin(), savers.end(),
              [](const ScreenSaver &a, const ScreenSaver &b) { return lstrcmpiW(a.Name.c_str(), b.Name.c_str()) < 0; });
    return savers;
}

static const UINT ScreenSaverMinutes[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };

static void
SetScreenSaver(const std::wstring &path)
{
    if (path.empty())
    {
        HKEY key;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, DESKTOP_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
        {
            RegDeleteValueW(key, L"SCRNSAVE.EXE");
            RegCloseKey(key);
        }
    }
    else
    {
        WriteString(HKEY_CURRENT_USER, DESKTOP_KEY, L"SCRNSAVE.EXE", path);
    }
    SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, !path.empty(), NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

void
BuildLockScreen(PageBuilder &page)
{
    std::vector<ScreenSaver> savers;
    std::vector<std::wstring> names, waits;
    std::wstring current;
    BOOL active = FALSE, secure = FALSE;
    UINT timeout = 600;
    int selected = 0, wait = 0;

    if (!page.Indexing)
    {
        savers = ScreenSavers();
        current = ReadString(HKEY_CURRENT_USER, DESKTOP_KEY, L"SCRNSAVE.EXE");
        SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &active, 0);
        SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT, 0, &timeout, 0);
        SystemParametersInfoW(SPI_GETSCREENSAVESECURE, 0, &secure, 0);
    }
    names.push_back(Str(IDS_NONE));
    for (size_t i = 0; i < savers.size(); i++)
    {
        names.push_back(savers[i].Name);
        if (active && !lstrcmpiW(savers[i].Path.c_str(), current.c_str()))
            selected = (int)i + 1;
    }
    for (UINT i = 0; i < _countof(ScreenSaverMinutes); i++)
    {
        waits.push_back(Format(ScreenSaverMinutes[i] == 1 ? IDS_MINUTE : IDS_MINUTES, (ULONG)ScreenSaverMinutes[i]));
        if (ScreenSaverMinutes[i] * 60 <= timeout)
            wait = (int)i;
    }

    page.Group(Str(IDS_SCREEN_SAVER));
    page.Choice(Str(IDS_SCREEN_SAVER), Str(IDS_SCREEN_SAVER_DESC), L"desktop_20_regular", names, selected,
                [savers](int index)
                {
                    SetScreenSaver(index > 0 && index <= (int)savers.size() ? savers[index - 1].Path : std::wstring());
                }).Keywords = Str(IDS_LOCKSCREEN_KEYWORDS);
    if (selected == 0)
        return;
    page.Choice(Str(IDS_SCREEN_SAVER_WAIT), Str(IDS_SCREEN_SAVER_WAIT_DESC), L"clock_20_regular", waits, wait,
                [](int index)
                {
                    SystemParametersInfoW(SPI_SETSCREENSAVETIMEOUT, ScreenSaverMinutes[index] * 60, NULL,
                                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                });
    page.Toggle(Str(IDS_SCREEN_SAVER_SECURE), Str(IDS_SCREEN_SAVER_SECURE_DESC), L"lock_closed_20_regular",
                secure != FALSE,
                [](int on)
                {
                    SystemParametersInfoW(SPI_SETSCREENSAVESECURE, on != 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                });
    page.Button(Str(IDS_PREVIEW), Str(IDS_SCREEN_SAVER_PREVIEW_DESC), L"desktop_20_regular", Str(IDS_PREVIEW),
                [current](int) { Launch(current.c_str(), L"/s"); });
}

static void
SetTaskbarValue(LPCWSTR key, LPCWSTR value, DWORD data)
{
    WriteDword(HKEY_CURRENT_USER, key, value, data);
    BroadcastSetting(L"TraySettings");
}

static void
AddTaskbarToggle(PageBuilder &page, UINT title, UINT detail, LPCWSTR icon, LPCWSTR key, LPCWSTR value,
                 DWORD fallback, bool invert)
{
    DWORD data = ReadDword(HKEY_CURRENT_USER, key, value, fallback);
    std::wstring keyText = key, valueText = value;

    page.Toggle(Str(title), detail ? Str(detail) : std::wstring(), icon, invert ? data == 0 : data != 0,
                [keyText, valueText, invert](int on)
                {
                    SetTaskbarValue(keyText.c_str(), valueText.c_str(), invert ? !on : (on != 0));
                });
}

void
BuildTaskbar(PageBuilder &page)
{
    APPBARDATA bar = { sizeof(bar) };
    UINT_PTR state;

    bar.hWnd = FindWindowW(L"Shell_TrayWnd", NULL);
    state = page.Indexing ? 0 : SHAppBarMessage(ABM_GETSTATE, &bar);
    page.Group(Str(IDS_TASKBAR_BEHAVIORS));
    page.Toggle(Str(IDS_AUTOHIDE), Str(IDS_AUTOHIDE_DESC), L"panel_bottom_20_regular", (state & ABS_AUTOHIDE) != 0,
                [state](int on)
                {
                    APPBARDATA bar = { sizeof(bar) };
                    bar.hWnd = FindWindowW(L"Shell_TrayWnd", NULL);
                    bar.lParam = on ? (state | ABS_AUTOHIDE) : (state & ~ABS_AUTOHIDE);
                    SHAppBarMessage(ABM_SETSTATE, &bar);
                }).Enabled = page.Indexing || bar.hWnd != NULL;
    AddTaskbarToggle(page, IDS_LOCK_TASKBAR, IDS_LOCK_TASKBAR_DESC, L"lock_closed_20_regular", ADVANCED_KEY,
                     L"TaskbarSizeMove", 0, true);
    AddTaskbarToggle(page, IDS_SMALL_BUTTONS, 0, L"apps_20_regular", ADVANCED_KEY, L"TaskbarSmallIcons", 1, false);
    AddTaskbarToggle(page, IDS_COMBINE_BUTTONS, IDS_COMBINE_BUTTONS_DESC, L"window_multiple_20_regular", ADVANCED_KEY,
                     L"TaskbarGlomming", 0, false);
    AddTaskbarToggle(page, IDS_SHOW_DESKTOP_CORNER, IDS_SHOW_DESKTOP_CORNER_DESC, L"desktop_20_regular", ADVANCED_KEY,
                     L"TaskbarSd", 1, false);

    page.Group(Str(IDS_SYSTEM_TRAY));
    AddTaskbarToggle(page, IDS_SHOW_SECONDS, 0, L"clock_20_regular", ADVANCED_KEY, L"ShowSeconds", 0, false);
    AddTaskbarToggle(page, IDS_HIDE_INACTIVE_ICONS, IDS_HIDE_INACTIVE_ICONS_DESC, L"apps_list_20_regular", EXPLORER_KEY,
                     L"EnableAutotray", 1, false);
}

static int CALLBACK
FontFamilyProc(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM parameter)
{
    std::vector<std::pair<std::wstring, DWORD>> *families = (std::vector<std::pair<std::wstring, DWORD>> *)parameter;

    UNREFERENCED_PARAMETER(metrics);
    if (font->lfFaceName[0] == L'@')
        return 1;
    for (size_t i = 0; i < families->size(); i++)
    {
        if (!lstrcmpiW((*families)[i].first.c_str(), font->lfFaceName))
            return 1;
    }
    families->push_back(std::make_pair(std::wstring(font->lfFaceName), type));
    return 1;
}

static void
InstallFont(HWND owner)
{
    WCHAR file[MAX_PATH] = L"", fonts[MAX_PATH];
    std::wstring filter = Str(IDS_FONT_FILTER), target, name;
    OPENFILENAMEW ofn = { sizeof(ofn) };

    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = _countof(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn) || FAILED(SHGetFolderPathW(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, fonts)))
        return;
    target = std::wstring(fonts) + L"\\" + PathFindFileNameW(file);
    if (!CopyFileW(file, target.c_str(), TRUE) && GetLastError() != ERROR_FILE_EXISTS)
    {
        ShowError(owner, GetLastError());
        return;
    }
    if (!AddFontResourceW(target.c_str()))
    {
        ShowError(owner, ERROR_INVALID_DATA);
        return;
    }
    name = PathFindFileNameW(file);
    name = name.substr(0, name.find_last_of(L'.')) + L" (TrueType)";
    if (!WriteString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", name.c_str(),
                     PathFindFileNameW(file)))
        ShowError(owner, ERROR_ACCESS_DENIED);
    SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL);
}

void
BuildFonts(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<std::pair<std::wstring, DWORD>> families;
    LOGFONTW query = { };
    HDC dc;

    page.Group();
    page.Button(Str(IDS_INSTALL_FONT), Str(IDS_INSTALL_FONT_DESC), L"add_circle_20_regular", Str(IDS_BROWSE),
                [host](int) { InstallFont(host->Window()); });
    page.Link(Str(IDS_FONTS_FOLDER), Str(IDS_FONTS_FOLDER_DESC), L"folder_open_20_regular",
              [](int)
              {
                  WCHAR path[MAX_PATH];
                  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, path)))
                      ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
              });
    if (page.Indexing)
        return;
    dc = GetDC(NULL);
    query.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &query, FontFamilyProc, (LPARAM)&families, 0);
    ReleaseDC(NULL, dc);
    std::sort(families.begin(), families.end(), [](const std::pair<std::wstring, DWORD> &a,
                                                   const std::pair<std::wstring, DWORD> &b)
              { return lstrcmpiW(a.first.c_str(), b.first.c_str()) < 0; });
    page.Group(Format(IDS_FONT_COUNT, (UINT)families.size()));
    for (size_t i = 0; i < families.size(); i++)
    {
        UINT type = (families[i].second & TRUETYPE_FONTTYPE) ? IDS_FONT_TRUETYPE :
                    ((families[i].second & RASTER_FONTTYPE) ? IDS_FONT_RASTER : IDS_FONT_VECTOR);
        page.Info(families[i].first, Str(type), L"text_font_20_regular");
    }
}
