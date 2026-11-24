/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installed application registry, install tracking and uninstall
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"
#include "util.hpp"

#include <map>

namespace lntget
{

struct ArpEntry
{
    bool machine = false;
    bool wow64View = false;
    std::wstring keyName;
    std::wstring name;
    std::wstring version;
    std::wstring publisher;
    std::wstring location;
    std::wstring uninstall;
    std::wstring quietUninstall;
    bool windowsInstaller = false;
    bool innoSetup = false;
    bool portable = false;
    bool installDirectoryCreated = false;
    bool installDirectoryAddedToPath = false;
    std::vector<std::wstring> symlinks;
    std::vector<std::wstring> pathEntries;
    bool filesTracked = false;
    std::vector<std::wstring> files;
    unsigned long long estimatedBytes = 0;

    std::string Identity() const;
};

struct TrackedPackage
{
    std::string id;
    std::string version;
    std::string installerType;
    std::vector<std::string> arpIdentities;
};

struct InstalledPackage
{
    ArpEntry entry;
    std::string packageId;
    std::string packageName;
    std::string available;
    std::string installerType;
    bool correlated = false;
};

typedef std::map<std::string, std::wstring> ArpSnapshot;

class SourceManager;

std::vector<ArpEntry> EnumerateArpEntries();
ArpSnapshot TakeArpSnapshot();
std::vector<std::string> ChangedArpIdentities(const ArpSnapshot &before, const ArpSnapshot &after);

Status RecordTrackedPackage(const TrackedPackage &package);
Status RemoveTrackedPackage(const std::string &id);
std::vector<TrackedPackage> LoadTrackedPackages();

int CompareVersions(const std::string &left, const std::string &right);

Status CollectInstalledPackages(SourceManager *source, std::vector<InstalledPackage> &packages);

std::wstring BuildUninstallCommand(const ArpEntry &entry, const std::string &installerType, InstallMode mode);
Status UninstallArpEntry(const ArpEntry &entry, const std::string &installerType, InstallMode mode, DWORD &exitCode,
                         InstallLockWait *lockWait = nullptr);

Status AppendPathVariable(bool machine, const std::wstring &directory);
Status RemovePathVariable(bool machine, const std::wstring &directory);
bool ReadArpString(HKEY key, const wchar_t *name, std::wstring &value);
std::vector<std::wstring> ReadArpMultiString(HKEY key, const wchar_t *name);
extern const wchar_t ArpUninstallKey[];

Status RunArpSelfTests();

} // namespace lntget
