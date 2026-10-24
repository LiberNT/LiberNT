/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared lntget contracts and data models
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace lntget
{

inline constexpr char Version[] = "0.1.0";
inline constexpr wchar_t CommunitySourceUrl[] = L"https://cdn.winget.microsoft.com/cache/source2.msix";

struct Status
{
    bool success = true;
    DWORD code = ERROR_SUCCESS;
    std::string message;

    static Status Ok();
    static Status Fail(DWORD error, const std::string &text);
    explicit operator bool() const { return success; }
};

struct PackageRecord
{
    std::string id;
    std::string name;
    std::string moniker;
    std::string version;
    uint8_t manifestHash[32]{};
};

struct CatalogEntry
{
    PackageRecord package;
    std::vector<uint32_t> tags;
    std::vector<uint32_t> commands;
};

struct Catalog
{
    std::vector<std::string> tags;
    std::vector<std::string> commands;
    std::vector<CatalogEntry> entries;
};

struct PackageDetails
{
    std::string publisher;
    std::string publisherUrl;
    std::string supportUrl;
    std::string author;
    std::string packageName;
    std::string packageUrl;
    std::string license;
    std::string licenseUrl;
    std::string copyright;
    std::string shortDescription;
    std::string description;
    std::string releaseNotesUrl;
    std::string documentationUrl;
    std::vector<std::string> tags;
};

struct InstallerSwitches
{
    std::string silent;
    std::string silentWithProgress;
    std::string interactive;
    std::string custom;
};

struct InstallerEntry
{
    std::string architecture;
    std::string type;
    std::string url;
    std::string sha256;
    std::string scope;
    std::string locale;
    std::string minimumOsVersion;
    InstallerSwitches switches;
    std::vector<std::string> unsupportedOsArchitectures;
    std::vector<std::string> installModes;
    std::vector<DWORD> successCodes;
    std::map<DWORD, std::string> expectedReturnCodes;
    bool hasDependencies = false;
    bool hasAuthentication = false;
    bool hasMarketRestrictions = false;
    bool downloadCommandProhibited = false;
};

struct PackageAgreement
{
    std::string label;
    std::string text;
    std::string url;
};

struct Manifest
{
    std::string id;
    std::string version;
    std::string defaultArchitecture;
    std::string defaultType;
    std::string defaultScope;
    std::string defaultLocale;
    std::string defaultMinimumOsVersion;
    std::string defaultUrl;
    std::string defaultSha256;
    InstallerSwitches defaultSwitches;
    std::vector<std::string> defaultUnsupportedOsArchitectures;
    std::vector<std::string> defaultInstallModes;
    std::vector<DWORD> defaultSuccessCodes;
    std::map<DWORD, std::string> defaultExpectedReturnCodes;
    bool defaultHasDependencies = false;
    bool defaultHasAuthentication = false;
    bool defaultHasMarketRestrictions = false;
    bool defaultDownloadCommandProhibited = false;
    bool hasAgreements = false;
    std::vector<PackageAgreement> agreements;
    std::vector<InstallerEntry> installers;
};

enum class MachineArchitecture
{
    X86,
    X64,
    Arm,
    Arm64,
    Unknown,
};

enum class InstallMode
{
    Default,
    Silent,
    Interactive,
};

struct SelectionOptions
{
    bool hasArchitecture = false;
    std::string architecture;
    bool hasInstallerType = false;
    std::string installerType;
    bool hasScope = false;
    std::string scope;
    bool hasLocale = false;
    std::string locale;
    InstallMode mode = InstallMode::Default;
    bool disableInteractivity = false;
    bool downloadOnly = false;
    bool acceptPackageAgreements = false;
};

} // namespace lntget
