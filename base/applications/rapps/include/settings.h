#pragma once

#include <wininet.h>

struct SETTINGS_INFO
{
    BOOL bSaveWndPos;
    BOOL bUpdateAtStart;
    BOOL bLogEnabled;
    WCHAR szDownloadDir[MAX_PATH];
    BOOL bDelInstaller;
    BOOL bSmallIcons;
    /* Window Pos */
    BOOL Maximized;
    INT Left;
    INT Top;
    INT Width;
    INT Height;
    /* Proxy settings */
    INT Proxy;
    WCHAR szProxyServer[MAX_PATH];
    WCHAR szNoProxyFor[MAX_PATH];
    /* Software source settings */
    BOOL bUseSource;
    WCHAR szSourceURL[INTERNET_MAX_URL_LENGTH];
    WCHAR szPackageArch[16];
};

typedef SETTINGS_INFO *PSETTINGS_INFO;

BOOL LoadSettings(PSETTINGS_INFO pSettingsInfo);
BOOL SaveSettings(HWND hwnd, PSETTINGS_INFO pSettingsInfo);
VOID FillDefaultSettings(PSETTINGS_INFO pSettingsInfo);
LPCWSTR GetPackageArchitecture();
UINT GetPackageArchitectures(LPCWSTR *Architectures, UINT Count);

extern SETTINGS_INFO SettingsInfo;
