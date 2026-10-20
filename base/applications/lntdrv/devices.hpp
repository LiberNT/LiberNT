/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device inventory, driver package install and install history
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace lntdrv
{

struct DeviceInfo
{
    std::wstring InstanceId;
    std::wstring Name;
    std::wstring Class;
    std::wstring DriverVersion;
    std::wstring DriverDate;
    FILETIME DriverDateData = {};
    std::wstring Provider;
    std::wstring Inf;
    std::wstring Service;
    std::vector<std::wstring> HardwareIds;
    std::vector<std::wstring> CompatibleIds;
    ULONG Problem = 0;
    bool Started = false;
    bool NameResolved = false;
};

struct PackageDriver
{
    std::wstring Inf;
    std::wstring HardwareId;
    std::wstring Description;
    FILETIME Date = {};
    DWORDLONG Version = 0;
    DWORD Rank = MAXDWORD;
};

struct HistoryEntry
{
    std::wstring Key;
    std::wstring Action;
    std::wstring UpdateId;
    std::wstring Title;
    std::wstring Version;
    std::wstring Device;
    std::wstring InstanceId;
    std::wstring HardwareId;
    std::wstring Inf;
    std::wstring PreviousInf;
    std::wstring PreviousVersion;
    std::wstring Installed;
    DWORD Result = 0;
    DWORD Reboot = 0;
};

enum class DriverOrder
{
    Missing,
    Older,
    Same,
    Newer
};

DWORD EnumerateDevices(std::vector<DeviceInfo> &devices);
bool FindDevice(const std::wstring &instanceId, DeviceInfo &device);
bool HasDriver(const DeviceInfo &device);
bool NeedsDriver(const DeviceInfo &device);
std::wstring DeviceStatus(const DeviceInfo &device);
std::wstring DeviceDetails(const DeviceInfo &device, const std::vector<HistoryEntry> &history);
std::vector<std::wstring> CatalogQueries(const DeviceInfo &device);
DriverOrder CompareVersionText(const DeviceInfo &device, const std::wstring &version);
DriverOrder CompareWithInstalled(const DeviceInfo &device, const PackageDriver &driver);
std::wstring ReadInfText(const std::wstring &path);
std::wstring CachedDeviceName(const DeviceInfo &device);
void StoreDeviceName(const DeviceInfo &device, const std::wstring &name);
std::wstring LocalDeviceName(const DeviceInfo &device);

std::wstring DownloadsRoot();
std::wstring PackageDirectory(const std::wstring &updateId);
DWORD PrepareDownloads();
void CleanupDownloads(DWORD days);
void DeleteTree(const std::wstring &path);
DWORD ExtractPackage(const std::wstring &file, const std::wstring &directory);
DWORD FindPackageDriver(const DeviceInfo &device, const std::vector<std::wstring> &infs, PackageDriver &driver);
std::vector<std::wstring> PackageInfs(const std::wstring &directory);
DWORD InstallPackageDriver(HWND owner, const PackageDriver &driver, bool force, BOOL &reboot);
DWORD RemoveCatalogDriver(const HistoryEntry &entry, BOOL &reboot);
DWORD RollBackDriver(HWND owner, const HistoryEntry &entry, BOOL &reboot);

DWORD SaveHistory(HistoryEntry &entry);
std::vector<HistoryEntry> LoadHistory();
std::wstring DriverSource(const DeviceInfo &device, const std::vector<HistoryEntry> &history);
const HistoryEntry *CatalogEntryInUse(const DeviceInfo &device, const std::vector<HistoryEntry> &history);

}
