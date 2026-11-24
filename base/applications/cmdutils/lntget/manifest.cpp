/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinGet YAML manifest reader and installer selection
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "manifest.hpp"
#include "util.hpp"
#include "yaml.hpp"

#include <algorithm>
#include <cassert>
#include <ctype.h>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace lntget
{

namespace
{

bool ParseCode(const std::string &value, DWORD &code)
{
    const std::string owned = Trim(value);
    if (owned.empty() || owned[0] == '-')
        return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(owned.c_str(), &end, 0);
    if (errno == ERANGE || !end || *end || parsed > std::numeric_limits<DWORD>::max())
        return false;
    code = static_cast<DWORD>(parsed);
    return true;
}

bool ParseReturnCode(const std::string &value, DWORD &code)
{
    const std::string owned = Trim(value);
    DWORD magnitude = 0;
    if (owned.empty() || owned[0] != '-')
        return ParseCode(owned, code);
    if (!ParseCode(owned.substr(1), magnitude) || magnitude > 0x80000000UL)
        return false;
    code = 0UL - magnitude;
    return true;
}

bool ParseBoolean(const std::string &value, bool &result)
{
    if (AsciiEquals(value, "true"))
    {
        result = true;
        return true;
    }
    if (AsciiEquals(value, "false"))
    {
        result = false;
        return true;
    }
    return false;
}

Status ScalarField(const YamlNode &node, const std::string &key, std::string &value)
{
    if (node.IsNull())
    {
        value.clear();
        return Status::Ok();
    }
    if (!node.IsScalar())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + key + " is not a scalar");
    value = node.scalar;
    return Status::Ok();
}

Status ListField(const YamlNode &node, const std::string &key, std::vector<std::string> &values)
{
    values.clear();
    if (node.IsNull())
        return Status::Ok();
    if (!node.IsSequence())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + key + " is not a list");
    for (const YamlNode &item : node.items)
    {
        if (!item.IsScalar())
            return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + key + " contains a value that is not a scalar");
        values.push_back(item.scalar);
    }
    return Status::Ok();
}

Status MappingListField(const YamlNode &node, const std::string &key)
{
    if (!node.IsNull() && !node.IsSequence())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + key + " is not a list");
    for (const YamlNode &item : node.items)
    {
        if (!item.IsMapping())
            return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + key + " contains an entry that is not a mapping");
    }
    return Status::Ok();
}

Status NestedFilesField(const YamlNode &node, std::vector<NestedInstallerFile> &files)
{
    files.clear();
    Status status = MappingListField(node, "NestedInstallerFiles");
    for (std::size_t index = 0; status && index < node.items.size(); ++index)
    {
        NestedInstallerFile file;
        for (const auto &member : node.items[index].members)
        {
            if (member.first == "RelativeFilePath") status = ScalarField(member.second, member.first, file.relativePath);
            else if (member.first == "PortableCommandAlias") status = ScalarField(member.second, member.first, file.portableCommandAlias);
            if (!status) break;
        }
        files.push_back(file);
    }
    return status;
}

Status SwitchesField(const YamlNode &node, InstallerSwitches &switches)
{
    if (node.IsNull())
        return Status::Ok();
    if (!node.IsMapping())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest field InstallerSwitches is not a mapping");
    for (const auto &member : node.members)
    {
        std::string *target = nullptr;
        if (member.first == "Silent") target = &switches.silent;
        else if (member.first == "SilentWithProgress") target = &switches.silentWithProgress;
        else if (member.first == "Interactive") target = &switches.interactive;
        else if (member.first == "Custom") target = &switches.custom;
        if (!target || member.second.IsNull()) continue;
        const Status status = ScalarField(member.second, member.first, *target);
        if (!status) return status;
    }
    return Status::Ok();
}

Status SuccessCodesField(const YamlNode &node, std::vector<DWORD> &codes)
{
    std::vector<std::string> values;
    Status status = ListField(node, "InstallerSuccessCodes", values);
    if (!status) return status;
    codes.clear();
    for (const std::string &value : values)
    {
        DWORD code = 0;
        if (!ParseReturnCode(value, code))
            return Status::Fail(ERROR_BAD_FORMAT, "InstallerSuccessCodes contains a non-numeric value");
        codes.push_back(code);
    }
    return Status::Ok();
}

Status ExpectedReturnCodesField(const YamlNode &node, std::map<DWORD, std::string> &codes)
{
    codes.clear();
    Status status = MappingListField(node, "ExpectedReturnCodes");
    for (std::size_t index = 0; status && index < node.items.size(); ++index)
    {
        const YamlNode &entry = node.items[index];
        const YamlNode *codeNode = entry.Find("InstallerReturnCode");
        const YamlNode *responseNode = entry.Find("ReturnResponse");
        DWORD code = 0;
        if (!codeNode || !codeNode->IsScalar() || !ParseReturnCode(codeNode->scalar, code))
            return Status::Fail(ERROR_BAD_FORMAT, "ExpectedReturnCodes contains a non-numeric code");
        std::string response;
        if (responseNode)
            status = ScalarField(*responseNode, "ReturnResponse", response);
        codes[code] = response.empty() ? "expected installer response" : response;
    }
    return status;
}

Status DependenciesField(const YamlNode &node, InstallerEntry &installer)
{
    installer.packageDependencies.clear();
    installer.hasUnsupportedDependencies = false;
    if (node.IsNull())
        return Status::Ok();
    if (!node.IsMapping())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest field Dependencies is not a mapping");
    for (const auto &member : node.members)
    {
        const YamlNode &value = member.second;
        if (member.first != "PackageDependencies")
        {
            if (!value.IsNull() && !value.IsSequence())
                return Status::Fail(ERROR_BAD_FORMAT, "manifest field " + member.first + " is not a list");
            if (member.first == "WindowsFeatures" || member.first == "WindowsLibraries" || member.first == "ExternalDependencies")
                installer.hasUnsupportedDependencies = installer.hasUnsupportedDependencies || !value.items.empty();
            continue;
        }
        Status status = MappingListField(value, member.first);
        if (!status) return status;
        for (const YamlNode &entry : value.items)
        {
            PackageDependency dependency;
            const YamlNode *id = entry.Find("PackageIdentifier");
            const YamlNode *minimumVersion = entry.Find("MinimumVersion");
            if (!id || !id->IsScalar() || id->scalar.empty())
                return Status::Fail(ERROR_BAD_FORMAT, "a package dependency has no PackageIdentifier");
            dependency.id = id->scalar;
            if (minimumVersion)
                status = ScalarField(*minimumVersion, "MinimumVersion", dependency.minimumVersion);
            if (!status) return status;
            installer.packageDependencies.push_back(dependency);
        }
    }
    return Status::Ok();
}

Status ApplyInstallerFields(const YamlNode &fields, InstallerEntry &installer)
{
    assert(fields.IsMapping());
    for (const auto &member : fields.members)
    {
        const std::string &key = member.first;
        const YamlNode &value = member.second;
        if (value.IsNull())
            continue;
        Status status;
        if (key == "Architecture") status = ScalarField(value, key, installer.architecture);
        else if (key == "InstallerType") status = ScalarField(value, key, installer.type);
        else if (key == "InstallerUrl") status = ScalarField(value, key, installer.url);
        else if (key == "InstallerSha256") status = ScalarField(value, key, installer.sha256);
        else if (key == "Scope") status = ScalarField(value, key, installer.scope);
        else if (key == "InstallerLocale") status = ScalarField(value, key, installer.locale);
        else if (key == "MinimumOSVersion") status = ScalarField(value, key, installer.minimumOsVersion);
        else if (key == "NestedInstallerType") status = ScalarField(value, key, installer.nestedType);
        else if (key == "NestedInstallerFiles") status = NestedFilesField(value, installer.nestedFiles);
        else if (key == "InstallerSwitches") status = SwitchesField(value, installer.switches);
        else if (key == "UnsupportedOSArchitectures") status = ListField(value, key, installer.unsupportedOsArchitectures);
        else if (key == "InstallModes") status = ListField(value, key, installer.installModes);
        else if (key == "Commands") status = ListField(value, key, installer.commands);
        else if (key == "InstallerSuccessCodes") status = SuccessCodesField(value, installer.successCodes);
        else if (key == "ExpectedReturnCodes") status = ExpectedReturnCodesField(value, installer.expectedReturnCodes);
        else if (key == "Dependencies") status = DependenciesField(value, installer);
        else if (key == "Authentication") installer.hasAuthentication = true;
        else if (key == "Markets") installer.hasMarketRestrictions = true;
        else if (key == "DownloadCommandProhibited")
        {
            std::string text;
            status = ScalarField(value, key, text);
            if (status && !text.empty() && !ParseBoolean(text, installer.downloadCommandProhibited))
                status = Status::Fail(ERROR_BAD_FORMAT, "manifest field DownloadCommandProhibited is not a boolean");
        }
        if (!status) return status;
    }
    return Status::Ok();
}


int ArchitectureRank(const std::string &installerArchitecture, MachineArchitecture machine)
{
    const std::string architecture = AsciiLower(installerArchitecture);
    if (architecture == "neutral") return 3;
    switch (machine)
    {
        case MachineArchitecture::Arm64:
            if (architecture == "arm64") return 0;
            if (architecture == "x64") return 1;
            if (architecture == "x86") return 2;
            if (architecture == "arm") return 4;
            break;
        case MachineArchitecture::X64:
            if (architecture == "x64") return 0;
            if (architecture == "x86") return 1;
            break;
        case MachineArchitecture::X86:
            if (architecture == "x86") return 0;
            break;
        case MachineArchitecture::Arm:
            if (architecture == "arm") return 0;
            break;
        case MachineArchitecture::Riscv64:
            if (architecture == "riscv64") return 0;
            if (architecture == "x64") return 1;
            if (architecture == "x86") return 2;
            break;
        case MachineArchitecture::Unknown:
            break;
    }
    return -1;
}

int TypeRank(const std::string &type)
{
    if (AsciiEquals(type, "wix") || AsciiEquals(type, "msi")) return 0;
    if (AsciiEquals(type, "inno") || AsciiEquals(type, "nullsoft") || AsciiEquals(type, "burn")) return 1;
    if (AsciiEquals(type, "exe") || AsciiEquals(type, "portable")) return 2;
    return -1;
}

int InstallerTypeRank(const InstallerEntry &installer)
{
    if (!AsciiEquals(installer.type, "zip"))
        return TypeRank(installer.type);
    if (installer.nestedFiles.empty() || (!AsciiEquals(installer.nestedType, "portable") && installer.nestedFiles.size() != 1))
        return -1;
    for (const NestedInstallerFile &file : installer.nestedFiles)
    {
        if (file.relativePath.empty())
            return -1;
    }
    return TypeRank(installer.nestedType);
}

bool ContainsCaseInsensitive(const std::vector<std::string> &values, const std::string &wanted)
{
    for (const std::string &value : values)
    {
        if (AsciiEquals(value, wanted))
            return true;
    }
    return false;
}

bool HasNonInteractiveArguments(const InstallerEntry &installer, InstallMode mode)
{
    const std::string type = EffectiveInstallerType(installer);
    if (AsciiEquals(type, "msi") || AsciiEquals(type, "wix") || AsciiEquals(type, "inno") ||
        AsciiEquals(type, "nullsoft") || AsciiEquals(type, "burn") || AsciiEquals(type, "portable"))
        return true;
    if (mode == InstallMode::Silent)
        return !installer.switches.silent.empty();
    return !installer.switches.silentWithProgress.empty() || !installer.switches.silent.empty();
}

bool SupportsMode(const InstallerEntry &installer, InstallMode mode)
{
    if (installer.installModes.empty())
        return true;
    if (mode == InstallMode::Interactive)
        return ContainsCaseInsensitive(installer.installModes, "interactive");
    if (mode == InstallMode::Silent)
        return ContainsCaseInsensitive(installer.installModes, "silent");
    return ContainsCaseInsensitive(installer.installModes, "silentWithProgress") ||
           ContainsCaseInsensitive(installer.installModes, "silent");
}

bool ParseVersion(const std::string &text, DWORD (&parts)[4])
{
    std::fill(parts, parts + 4, 0);
    std::size_t offset = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        const std::size_t dot = text.find('.', offset);
        const std::string component = dot == std::string::npos ? text.substr(offset) : text.substr(offset, dot - offset);
        DWORD value = 0;
        if (!ParseCode(component, value))
            return false;
        parts[index] = value;
        if (dot == std::string::npos)
            return true;
        offset = dot + 1;
    }
    return offset == text.size();
}

void CurrentOsVersion(DWORD (&version)[4])
{
    struct RtlVersionInfo
    {
        ULONG size;
        ULONG major;
        ULONG minor;
        ULONG build;
        ULONG platform;
        WCHAR servicePack[128];
    } information{};
    information.size = sizeof(information);
    using RtlGetVersionFunction = LONG (WINAPI *)(RtlVersionInfo *);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    std::fill(version, version + 4, 0);
    if (rtlGetVersion && rtlGetVersion(&information) >= 0)
    {
        version[0] = information.major;
        version[1] = information.minor;
        version[2] = information.build;
    }
}

bool MeetsMinimumOsVersion(const std::string &minimum)
{
    if (minimum.empty())
        return true;
    DWORD required[4]{};
    if (!ParseVersion(minimum, required))
        return false;
    DWORD current[4]{};
    CurrentOsVersion(current);
    return !std::lexicographical_compare(current, current + 4, required, required + 4);
}

bool IsSha256(const std::string &value)
{
    if (value.size() != 64)
        return false;
    for (const char character : value)
    {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F')))
            return false;
    }
    return true;
}

Status ParsePackageAgreements(const YamlNode *node, std::vector<PackageAgreement> &agreements)
{
    agreements.clear();
    if (!node)
        return Status::Ok();
    Status status = MappingListField(*node, "Agreements");
    if (!status) return status;
    if (node->items.size() > 32)
        return Status::Fail(ERROR_BAD_FORMAT, "package manifest contains too many agreements");
    for (const YamlNode &entry : node->items)
    {
        PackageAgreement agreement;
        for (const auto &member : entry.members)
        {
            if (member.first == "AgreementLabel") status = ScalarField(member.second, member.first, agreement.label);
            else if (member.first == "Agreement") status = ScalarField(member.second, member.first, agreement.text);
            else if (member.first == "AgreementUrl") status = ScalarField(member.second, member.first, agreement.url);
            if (!status) return status;
        }
        agreements.push_back(agreement);
    }
    for (const PackageAgreement &agreement : agreements)
    {
        if (agreement.label.size() > 100 || agreement.text.size() > 10000 || agreement.url.size() > 2048)
            return Status::Fail(ERROR_BAD_FORMAT, "package manifest contains an invalid agreement");
    }
    return Status::Ok();
}


Status SelectInstallerForMachine(const Manifest &manifest, const SelectionOptions &options, MachineArchitecture machine, InstallerEntry &installer)
{
    if (options.hasArchitecture && ArchitectureRank(options.architecture, machine) < 0)
        return Status::Fail(ERROR_NOT_SUPPORTED, "requested architecture " + options.architecture +
                            " cannot run on " + ArchitectureName(machine));

    int bestRank = 1000;
    const InstallerEntry *best = nullptr;
    bool constrained = false;
    bool unsupportedType = false;
    for (const InstallerEntry &candidate : manifest.installers)
    {
        int architectureRank = ArchitectureRank(candidate.architecture, machine);
        if (options.hasArchitecture)
        {
            if (AsciiEquals(candidate.architecture, options.architecture)) architectureRank = 0;
            else if (AsciiEquals(candidate.architecture, "neutral")) architectureRank = 1;
            else architectureRank = -1;
        }
        const int typeRank = InstallerTypeRank(candidate);
        unsupportedType = unsupportedType || typeRank < 0;
        if (architectureRank < 0 || typeRank < 0 || candidate.url.empty() || !IsSha256(candidate.sha256) ||
            (options.hasInstallerType && !AsciiEquals(candidate.type, options.installerType)))
            continue;
        if (ContainsCaseInsensitive(candidate.unsupportedOsArchitectures, ArchitectureName(machine)) ||
            !MeetsMinimumOsVersion(candidate.minimumOsVersion) || candidate.hasUnsupportedDependencies ||
            candidate.hasAuthentication || candidate.hasMarketRestrictions ||
            (options.hasScope && !AsciiEquals(candidate.scope, options.scope)) ||
            (options.hasLocale && !AsciiEquals(candidate.locale, options.locale)) ||
            (options.downloadOnly && candidate.downloadCommandProhibited) || !SupportsMode(candidate, options.mode) ||
            (options.mode != InstallMode::Interactive && !HasNonInteractiveArguments(candidate, options.mode)))
        {
            constrained = true;
            continue;
        }
        const int rank = architectureRank * 10 + typeRank;
        if (rank < bestRank)
        {
            bestRank = rank;
            best = &candidate;
        }
    }
    if (!best)
    {
        if (constrained)
            return Status::Fail(ERROR_NOT_SUPPORTED, "matching installers require an unsupported OS, dependency, authentication, market, download, or install-mode capability");
        if (unsupportedType)
            return Status::Fail(ERROR_NOT_SUPPORTED, "matching manifests only contain installer types lntget does not implement");
        return Status::Fail(ERROR_NOT_SUPPORTED, "no supported installer applies to architecture " +
                            (options.hasArchitecture ? options.architecture : ArchitectureName(machine)));
    }
    installer = *best;
    return Status::Ok();
}

} // namespace

Status ParseInstallerManifest(const std::string &yaml, Manifest &manifest)
{
    manifest = Manifest();
    YamlNode root;
    Status status = ParseYaml(yaml, root);
    if (!status) return status;
    if (!root.IsMapping())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest is not a YAML mapping");
    const YamlNode *id = root.Find("PackageIdentifier");
    const YamlNode *version = root.Find("PackageVersion");
    if (id) status = ScalarField(*id, "PackageIdentifier", manifest.id);
    if (status && version) status = ScalarField(*version, "PackageVersion", manifest.version);
    if (!status) return status;
    if (manifest.id.empty() || manifest.version.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest is missing PackageIdentifier or PackageVersion");

    InstallerEntry defaults;
    status = ApplyInstallerFields(root, defaults);
    if (!status) return status;
    const YamlNode *installers = root.Find("Installers");
    if (installers)
    {
        status = MappingListField(*installers, "Installers");
        if (!status) return status;
        for (const YamlNode &entry : installers->items)
        {
            InstallerEntry installer(defaults);
            installer.nestedType.clear();
            installer.nestedFiles.clear();
            installer.packageDependencies.clear();
            installer.hasUnsupportedDependencies = false;
            status = ApplyInstallerFields(entry, installer);
            if (!status) return status;
            if (AsciiEquals(installer.type, "zip"))
            {
                if (installer.nestedFiles.empty()) installer.nestedFiles = defaults.nestedFiles;
                if (installer.nestedType.empty()) installer.nestedType = defaults.nestedType;
            }
            if (installer.packageDependencies.empty() && !installer.hasUnsupportedDependencies)
            {
                installer.packageDependencies = defaults.packageDependencies;
                installer.hasUnsupportedDependencies = defaults.hasUnsupportedDependencies;
            }
            manifest.installers.push_back(installer);
        }
    }
    if (manifest.installers.empty() && (!defaults.architecture.empty() || !defaults.url.empty()))
        manifest.installers.push_back(defaults);

    manifest.defaultArchitecture = defaults.architecture;
    manifest.defaultType = defaults.type;
    manifest.defaultScope = defaults.scope;
    manifest.defaultLocale = defaults.locale;
    manifest.defaultMinimumOsVersion = defaults.minimumOsVersion;
    manifest.defaultUrl = defaults.url;
    manifest.defaultSha256 = defaults.sha256;
    manifest.defaultSwitches = defaults.switches;
    manifest.defaultUnsupportedOsArchitectures = defaults.unsupportedOsArchitectures;
    manifest.defaultInstallModes = defaults.installModes;
    manifest.defaultSuccessCodes = defaults.successCodes;
    manifest.defaultExpectedReturnCodes = defaults.expectedReturnCodes;
    manifest.defaultHasUnsupportedDependencies = defaults.hasUnsupportedDependencies;
    manifest.defaultHasAuthentication = defaults.hasAuthentication;
    manifest.defaultHasMarketRestrictions = defaults.hasMarketRestrictions;
    manifest.defaultDownloadCommandProhibited = defaults.downloadCommandProhibited;
    status = ParsePackageAgreements(root.Find("Agreements"), manifest.agreements);
    if (!status) return status;
    manifest.hasAgreements = !manifest.agreements.empty();
    return Status::Ok();
}

Status ParseLocaleManifest(const std::string &yaml, PackageDetails &details)
{
    details = PackageDetails();
    YamlNode root;
    Status status = ParseYaml(yaml, root);
    if (!status) return status;
    if (!root.IsMapping())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest is not a YAML mapping");
    static const struct
    {
        const char *key;
        std::string PackageDetails::*field;
    } Fields[] = {
        {"Publisher", &PackageDetails::publisher},
        {"PublisherUrl", &PackageDetails::publisherUrl},
        {"PublisherSupportUrl", &PackageDetails::supportUrl},
        {"Author", &PackageDetails::author},
        {"PackageName", &PackageDetails::packageName},
        {"PackageUrl", &PackageDetails::packageUrl},
        {"License", &PackageDetails::license},
        {"LicenseUrl", &PackageDetails::licenseUrl},
        {"Copyright", &PackageDetails::copyright},
        {"ShortDescription", &PackageDetails::shortDescription},
        {"Description", &PackageDetails::description},
        {"ReleaseNotesUrl", &PackageDetails::releaseNotesUrl},
    };
    for (const auto &field : Fields)
    {
        const YamlNode *node = root.Find(field.key);
        if (node) details.*field.field = node->Text();
    }
    const YamlNode *documentations = root.Find("Documentations");
    if (documentations && documentations->IsSequence())
    {
        for (const YamlNode &entry : documentations->items)
        {
            const YamlNode *url = entry.Find("DocumentUrl");
            if (url && !url->Text().empty())
            {
                details.documentationUrl = url->Text();
                break;
            }
        }
    }
    const YamlNode *tags = root.Find("Tags");
    if (tags && tags->IsSequence())
    {
        for (const YamlNode &tag : tags->items)
        {
            if (tag.IsScalar()) details.tags.push_back(tag.scalar);
        }
    }
    return Status::Ok();
}


MachineArchitecture CurrentMachineArchitecture()
{
    SYSTEM_INFO information{};
    GetNativeSystemInfo(&information);
    switch (information.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return MachineArchitecture::X86;
        case PROCESSOR_ARCHITECTURE_AMD64: return MachineArchitecture::X64;
        case PROCESSOR_ARCHITECTURE_ARM: return MachineArchitecture::Arm;
        case PROCESSOR_ARCHITECTURE_ARM64: return MachineArchitecture::Arm64;
        case PROCESSOR_ARCHITECTURE_RISCV64: return MachineArchitecture::Riscv64;
        default: return MachineArchitecture::Unknown;
    }
}

std::string ArchitectureName(MachineArchitecture architecture)
{
    switch (architecture)
    {
        case MachineArchitecture::X86: return "x86";
        case MachineArchitecture::X64: return "x64";
        case MachineArchitecture::Arm: return "arm";
        case MachineArchitecture::Arm64: return "arm64";
        case MachineArchitecture::Riscv64: return "riscv64";
        default: return "unknown";
    }
}

std::string EffectiveInstallerType(const InstallerEntry &installer)
{
    return AsciiEquals(installer.type, "zip") ? installer.nestedType : installer.type;
}

Status SelectInstaller(const Manifest &manifest, const SelectionOptions &options, InstallerEntry &installer)
{
    return SelectInstallerForMachine(manifest, options, CurrentMachineArchitecture(), installer);
}

Status RunManifestSelfTests()
{
    static constexpr char Fixture[] =
        "PackageIdentifier: Example.Tool\n"
        "PackageVersion: 1.2.3\n"
        "InstallerType: exe\n"
        "InstallerSwitches:\n"
        "  Silent: /S\n"
        "Installers:\n"
        "- InstallerUrl: https://example.test/tool-x86.exe\n"
        "  InstallerSuccessCodes: [42]\n"
        "  ExpectedReturnCodes:\n"
        "  - InstallerReturnCode: 1602\n"
        "    ReturnResponse: cancelledByUser\n"
        "  Architecture: x86\n"
        "  InstallerSha256: 0000000000000000000000000000000000000000000000000000000000000000\n"
        "- InstallerSha256: 1111111111111111111111111111111111111111111111111111111111111111\n"
        "  UnsupportedOSArchitectures: [x86]\n"
        "  InstallerUrl: https://example.test/tool-arm64.exe\n"
        "  Architecture: arm64\n";
    Manifest manifest;
    Status status = ParseInstallerManifest(Fixture, manifest);
    if (!status) return status;
    if (manifest.id != "Example.Tool" || manifest.version != "1.2.3" || manifest.installers.size() != 2)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "manifest parser self-test failed");
    if (manifest.installers[1].type != "exe" || manifest.installers[1].switches.silent != "/S")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "manifest inheritance self-test failed");
    if (manifest.installers[0].architecture != "x86" || manifest.installers[0].successCodes.size() != 1 ||
        manifest.installers[0].successCodes[0] != 42)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "order-independent installer parser self-test failed");
    if (manifest.installers[0].expectedReturnCodes[1602] != "cancelledByUser")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "expected return code parser self-test failed");

    static constexpr char SignedCodeFixture[] =
        "PackageIdentifier: Example.SignedCodes\n"
        "PackageVersion: 1.0\n"
        "InstallerType: exe\n"
        "Installers:\n"
        "- Architecture: x64\n"
        "  InstallerUrl: https://example.test/x64.exe\n"
        "  InstallerSha256: 4444444444444444444444444444444444444444444444444444444444444444\n"
        "  InstallerSuccessCodes: [-2147024809]\n"
        "  ExpectedReturnCodes:\n"
        "  - InstallerReturnCode: -2147219440\n"
        "    ReturnResponse: cancelledByUser\n"
        "  - InstallerReturnCode: -2147483648\n"
        "    ReturnResponse: contactSupport\n";
    Manifest signedCodes;
    status = ParseInstallerManifest(SignedCodeFixture, signedCodes);
    if (!status || signedCodes.installers.size() != 1 || signedCodes.installers[0].successCodes.size() != 1 ||
        signedCodes.installers[0].successCodes[0] != 0x80070057 ||
        signedCodes.installers[0].expectedReturnCodes[0x80040810] != "cancelledByUser" ||
        signedCodes.installers[0].expectedReturnCodes[0x80000000] != "contactSupport")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "signed installer return code self-test failed");
    if (ParseInstallerManifest("PackageIdentifier: Example.Bad\nPackageVersion: 1.0\nInstallerType: exe\n"
                               "Installers:\n- Architecture: x64\n  InstallerUrl: https://example.test/x64.exe\n"
                               "  InstallerSha256: 5555555555555555555555555555555555555555555555555555555555555555\n"
                               "  ExpectedReturnCodes:\n  - InstallerReturnCode: -2147483649\n"
                               "    ReturnResponse: contactSupport\n", signedCodes))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "out-of-range installer return code self-test failed");

    static constexpr char NestedTailFixture[] =
        "PackageIdentifier: Example.NestedTail\n"
        "PackageVersion: 1.0\n"
        "InstallerType: exe\n"
        "InstallerSwitches:\n"
        "  Silent: /S\n"
        "Installers:\n"
        "- Architecture: x86\n"
        "  InstallerUrl: https://example.test/x86.exe\n"
        "  InstallerSha256: 2222222222222222222222222222222222222222222222222222222222222222\n"
        "  InstallModes:\n"
        "  - silent\n"
        "- Architecture: x64\n"
        "  InstallerUrl: https://example.test/x64.exe\n"
        "  InstallerSha256: 3333333333333333333333333333333333333333333333333333333333333333\n";
    Manifest nestedTail;
    status = ParseInstallerManifest(NestedTailFixture, nestedTail);
    if (!status || nestedTail.installers.size() != 2 || nestedTail.installers[0].architecture != "x86" ||
        nestedTail.installers[1].architecture != "x64" || nestedTail.installers[1].url != "https://example.test/x64.exe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "nested-list installer boundary self-test failed");

    static constexpr char MergedFixture[] =
        "Dependencies:\n"
        "  PackageDependencies:\n"
        "  - MinimumVersion: 8.1\n"
        "    PackageIdentifier: Example.Runtime\n"
        "InstallerSwitches:\n"
        "  Silent: -q -Vdir=\"C:\\Program Files\\Example\"\n"
        "    -Vservice$Boolean=true\n"
        "InstallerType: exe\n"
        "Installers:\n"
        "- AppsAndFeaturesEntries:\n"
        "  - DisplayName: Example\n"
        "    InstallerType: wix\n"
        "  Architecture: x64\n"
        "  InstallerSha256: 7777777777777777777777777777777777777777777777777777777777777777\n"
        "  InstallerUrl: \"https://example.test/\\u7D05.exe\"\n"
        "  Scope:\n"
        "- Architecture: x86\n"
        "  InstallerSha256: 8888888888888888888888888888888888888888888888888888888888888888\n"
        "  InstallerType: zip\n"
        "  InstallerUrl: https://example.test/tool.zip\n"
        "  NestedInstallerFiles:\n"
        "NestedInstallerFiles:\n"
        "- RelativeFilePath: Example Tool\n"
        "    V2.exe\n"
        "NestedInstallerType: portable\n"
        "PackageIdentifier: Example.Merged\n"
        "PackageVersion: 2.0\n"
        "Scope: machine\n";
    Manifest merged;
    status = ParseInstallerManifest(MergedFixture, merged);
    if (!status || merged.installers.size() != 2 || merged.installers[0].type != "exe" ||
        merged.installers[0].url != "https://example.test/\xE7\xB4\x85.exe" || merged.installers[0].scope != "machine" ||
        merged.installers[0].switches.silent != "-q -Vdir=\"C:\\Program Files\\Example\" -Vservice$Boolean=true" ||
        merged.installers[0].packageDependencies.size() != 1 ||
        merged.installers[0].packageDependencies[0].id != "Example.Runtime" ||
        merged.installers[0].packageDependencies[0].minimumVersion != "8.1" || !merged.installers[0].nestedType.empty() ||
        merged.installers[1].nestedType != "portable" || merged.installers[1].nestedFiles.size() != 1 ||
        merged.installers[1].nestedFiles[0].relativePath != "Example Tool V2.exe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "merged manifest parser self-test failed");

    SelectionOptions options;
    options.hasArchitecture = true;
    options.architecture = "arm64";
    InstallerEntry installer;
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "incompatible explicit architecture self-test failed");
    options.architecture = "x86";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (!status || installer.architecture != "x86")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "compatible architecture selection self-test failed");
    options.hasScope = true;
    options.scope = "machine";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "unspecified installer scope self-test failed");
    options.hasScope = false;
    options.hasLocale = true;
    options.locale = "en-US";
    status = SelectInstallerForMachine(manifest, options, MachineArchitecture::X64, installer);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "unspecified installer locale self-test failed");
    options.hasLocale = false;
    SelectionOptions riscv64Options;
    status = SelectInstallerForMachine(nestedTail, riscv64Options, MachineArchitecture::Riscv64, installer);
    if (!status || installer.architecture != "x64")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "riscv64 installer preference self-test failed");
    status = SelectInstallerForMachine(manifest, riscv64Options, MachineArchitecture::Riscv64, installer);
    if (!status || installer.architecture != "x86")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "riscv64 x86 fallback self-test failed");
    static constexpr char AgreementFixture[] =
        "PackageIdentifier: Example.Agreement\n"
        "PackageVersion: 1.0\n"
        "Agreements:\n"
        "- AgreementLabel: Terms of use\n"
        "  Agreement: |-\n"
        "    Read these terms before installing.\n"
        "  AgreementUrl: https://example.test/terms\n"
        "InstallerType: exe\n"
        "Architecture: x86\n"
        "InstallerUrl: https://example.test/agreement.exe\n"
        "InstallerSha256: 4444444444444444444444444444444444444444444444444444444444444444\n";
    Manifest agreements;
    status = ParseInstallerManifest(AgreementFixture, agreements);
    if (!status || !agreements.hasAgreements || agreements.agreements.size() != 1 ||
        agreements.agreements[0].label != "Terms of use" ||
        agreements.agreements[0].text != "Read these terms before installing." ||
        agreements.agreements[0].url != "https://example.test/terms")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "package agreement parser self-test failed");
    status = ParseInstallerManifest("PackageIdentifier: Example.Labels\nPackageVersion: 1.0\nAgreements:\n"
                                    "- AgreementUrl: https://example.test/eula\n- AgreementLabel: EULA\n", agreements);
    if (!status || agreements.agreements.size() != 2 || !agreements.hasAgreements ||
        agreements.agreements[0].url != "https://example.test/eula" || agreements.agreements[1].label != "EULA" ||
        !agreements.agreements[1].text.empty() || !agreements.agreements[1].url.empty())
        return Status::Fail(ERROR_ASSERTION_FAILURE, "partial package agreement self-test failed");
    if (ParseInstallerManifest("PackageIdentifier: Example.Long\nPackageVersion: 1.0\nAgreements:\n- AgreementLabel: " +
                               std::string(101, 'x') + "\n", agreements))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "package agreement label limit self-test failed");
    if (UrlEncodePath("manifests/a/A+B/1.0/A+B.yaml") != L"manifests/a/A%2BB/1.0/A%2BB.yaml")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "URL encoder self-test failed");

    static constexpr char NestedFixture[] =
        "PackageIdentifier: Example.Zip\n"
        "PackageVersion: \"2.5\"\n"
        "InstallerType: zip\n"
        "NestedInstallerType: inno\n"
        "NestedInstallerFiles:\n"
        "- RelativeFilePath: .\\Setup.exe\n"
        "Dependencies:\n"
        "  PackageDependencies:\n"
        "  - PackageIdentifier: Microsoft.VCRedist.2015+.x64\n"
        "    MinimumVersion: 14.0\n"
        "Installers:\n"
        "- Architecture: neutral\n"
        "  InstallerUrl: https://example.test/setup.zip\n"
        "  InstallerSha256: 5555555555555555555555555555555555555555555555555555555555555555\n"
        "- Architecture: x64\n"
        "  InstallerUrl: https://example.test/setup-x64.zip\n"
        "  InstallerSha256: 6666666666666666666666666666666666666666666666666666666666666666\n"
        "  Dependencies:\n"
        "    WindowsFeatures:\n"
        "    - IIS-WebServer\n";
    Manifest nested;
    status = ParseInstallerManifest(NestedFixture, nested);
    if (!status || nested.installers.size() != 2 || nested.installers[0].nestedType != "inno" ||
        nested.installers[0].nestedFiles.size() != 1 || nested.installers[0].nestedFiles[0].relativePath != ".\\Setup.exe" ||
        nested.installers[0].packageDependencies.size() != 1 ||
        nested.installers[0].packageDependencies[0].id != "Microsoft.VCRedist.2015+.x64" ||
        nested.installers[0].packageDependencies[0].minimumVersion != "14.0" ||
        nested.installers[0].hasUnsupportedDependencies || !nested.installers[1].hasUnsupportedDependencies ||
        EffectiveInstallerType(nested.installers[0]) != "inno")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "nested installer and dependency parser self-test failed");
    InstallerEntry nestedSelection;
    status = SelectInstallerForMachine(nested, SelectionOptions(), MachineArchitecture::X64, nestedSelection);
    if (!status || nestedSelection.url != "https://example.test/setup.zip")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "nested installer selection self-test failed");
    return Status::Ok();
}

} // namespace lntget
