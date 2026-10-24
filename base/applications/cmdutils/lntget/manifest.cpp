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

#include <algorithm>
#include <ctype.h>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace lntget
{

namespace
{

enum class NestedSection
{
    None,
    Switches,
    UnsupportedArchitectures,
    InstallModes,
    SuccessCodes,
    ExpectedReturnCodes,
    Dependencies,
    Authentication,
    Markets,
};

std::string RemoveYamlComment(const std::string &line)
{
    bool singleQuoted = false;
    bool doubleQuoted = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        if (line[index] == '\'' && !doubleQuoted)
            singleQuoted = !singleQuoted;
        else if (line[index] == '"' && !singleQuoted && (index == 0 || line[index - 1] != '\\'))
            doubleQuoted = !doubleQuoted;
        else if (line[index] == '#' && !singleQuoted && !doubleQuoted &&
                 (index == 0 || line[index - 1] == ' ' || line[index - 1] == '\t'))
            return line.substr(0, index);
    }
    return line;
}

bool SplitProperty(const std::string &line, std::string &key, std::string &value)
{
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos)
        return false;
    key = Trim(line.substr(0, colon));
    value = UnquoteYaml(line.substr(colon + 1));
    return !key.empty();
}

std::vector<std::string> ParseInlineList(const std::string &value)
{
    std::string text = Trim(value);
    if (text.size() < 2 || text[0] != '[' || text[text.size() - 1] != ']')
        return std::vector<std::string>();
    text = text.substr(1, text.size() - 2);
    std::vector<std::string> result;
    std::size_t offset = 0;
    while (offset <= text.size())
    {
        const std::size_t comma = text.find(',', offset);
        const std::string item = UnquoteYaml(text.substr(offset, comma == std::string::npos ? text.size() - offset : comma - offset));
        if (!item.empty())
            result.push_back(item);
        if (comma == std::string::npos)
            break;
        offset = comma + 1;
    }
    return result;
}

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

std::string FoldBlock(const std::vector<std::string> &lines, bool folded, bool keepTrailing)
{
    std::string text;
    for (const std::string &line : lines)
    {
        if (!folded)
        {
            text += line;
            text.push_back('\n');
            continue;
        }
        if (line.empty())
        {
            text.push_back('\n');
            continue;
        }
        if (!text.empty() && text[text.size() - 1] != '\n')
            text.push_back(' ');
        text += line;
    }
    if (!keepTrailing)
    {
        while (!text.empty() && (text[text.size() - 1] == '\n' || text[text.size() - 1] == ' '))
            text.pop_back();
    }
    return text;
}

void SetSwitch(InstallerSwitches &switches, const std::string &key, const std::string &value)
{
    if (key == "Silent") switches.silent = value;
    else if (key == "SilentWithProgress") switches.silentWithProgress = value;
    else if (key == "Interactive") switches.interactive = value;
    else if (key == "Custom") switches.custom = value;
}

NestedSection SetInstallerProperty(InstallerEntry &installer, const std::string &key, const std::string &value)
{
    if (key == "Architecture") installer.architecture = value;
    else if (key == "InstallerType") installer.type = value;
    else if (key == "InstallerUrl") installer.url = value;
    else if (key == "InstallerSha256") installer.sha256 = value;
    else if (key == "Scope") installer.scope = value;
    else if (key == "InstallerLocale") installer.locale = value;
    else if (key == "MinimumOSVersion") installer.minimumOsVersion = value;
    else if (key == "InstallerSwitches") return NestedSection::Switches;
    else if (key == "UnsupportedOSArchitectures")
    {
        installer.unsupportedOsArchitectures = ParseInlineList(value);
        return NestedSection::UnsupportedArchitectures;
    }
    else if (key == "InstallModes")
    {
        installer.installModes = ParseInlineList(value);
        return NestedSection::InstallModes;
    }
    else if (key == "InstallerSuccessCodes")
    {
        for (const std::string &item : ParseInlineList(value))
        {
            DWORD code = 0;
            if (ParseCode(item, code)) installer.successCodes.push_back(code);
        }
        return NestedSection::SuccessCodes;
    }
    else if (key == "ExpectedReturnCodes") return NestedSection::ExpectedReturnCodes;
    else if (key == "Dependencies")
    {
        installer.hasDependencies = !value.empty();
        return NestedSection::Dependencies;
    }
    else if (key == "Authentication")
    {
        installer.hasAuthentication = true;
        return NestedSection::Authentication;
    }
    else if (key == "Markets")
    {
        installer.hasMarketRestrictions = true;
        return NestedSection::Markets;
    }
    else if (key == "DownloadCommandProhibited")
    {
        bool prohibited = false;
        if (ParseBoolean(value, prohibited)) installer.downloadCommandProhibited = prohibited;
    }
    return NestedSection::None;
}

void InheritString(std::string &value, const std::string &fallback)
{
    if (value.empty()) value = fallback;
}

void InheritInstaller(InstallerEntry &installer, const InstallerEntry &defaults)
{
    InheritString(installer.architecture, defaults.architecture);
    InheritString(installer.type, defaults.type);
    InheritString(installer.url, defaults.url);
    InheritString(installer.sha256, defaults.sha256);
    InheritString(installer.scope, defaults.scope);
    InheritString(installer.locale, defaults.locale);
    InheritString(installer.minimumOsVersion, defaults.minimumOsVersion);
    InheritString(installer.switches.silent, defaults.switches.silent);
    InheritString(installer.switches.silentWithProgress, defaults.switches.silentWithProgress);
    InheritString(installer.switches.interactive, defaults.switches.interactive);
    InheritString(installer.switches.custom, defaults.switches.custom);
    if (installer.unsupportedOsArchitectures.empty()) installer.unsupportedOsArchitectures = defaults.unsupportedOsArchitectures;
    if (installer.installModes.empty()) installer.installModes = defaults.installModes;
    if (installer.successCodes.empty()) installer.successCodes = defaults.successCodes;
    if (installer.expectedReturnCodes.empty()) installer.expectedReturnCodes = defaults.expectedReturnCodes;
    installer.hasDependencies = installer.hasDependencies || defaults.hasDependencies;
    installer.hasAuthentication = installer.hasAuthentication || defaults.hasAuthentication;
    installer.hasMarketRestrictions = installer.hasMarketRestrictions || defaults.hasMarketRestrictions;
    installer.downloadCommandProhibited = installer.downloadCommandProhibited || defaults.downloadCommandProhibited;
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
        case MachineArchitecture::Unknown:
            break;
    }
    return -1;
}

int TypeRank(const std::string &type)
{
    if (AsciiEquals(type, "wix") || AsciiEquals(type, "msi")) return 0;
    if (AsciiEquals(type, "inno") || AsciiEquals(type, "nullsoft") || AsciiEquals(type, "burn")) return 1;
    if (AsciiEquals(type, "exe")) return 2;
    return -1;
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
    if (AsciiEquals(installer.type, "msi") || AsciiEquals(installer.type, "wix") ||
        AsciiEquals(installer.type, "inno") || AsciiEquals(installer.type, "nullsoft") || AsciiEquals(installer.type, "burn"))
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

Status ParsePackageAgreements(const std::string &yaml, std::vector<PackageAgreement> &agreements)
{
    agreements.clear();
    std::vector<std::string> lines;
    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line(yaml.substr(offset, end == std::string::npos ? yaml.size() - offset : end - offset));
        if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
        lines.push_back(line);
        if (end == std::string::npos) break;
        offset = end + 1;
    }

    bool inAgreements = false;
    std::size_t agreementsIndent = 0;
    std::size_t itemIndent = 0;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        std::string line = RemoveYamlComment(lines[index]);
        if (Trim(line).empty()) continue;
        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') ++indent;
        if (indent < line.size() && line[indent] == '\t')
            return Status::Fail(ERROR_BAD_FORMAT, "package agreements use a tab for YAML indentation");
        const std::string content = line.substr(indent);
        if (!inAgreements)
        {
            if (Trim(content) == "Agreements:")
            {
                inAgreements = true;
                agreementsIndent = indent;
            }
            continue;
        }
        if (indent <= agreementsIndent && content.rfind("- ", 0) != 0)
            break;

        std::string property = content;
        if (content.rfind("- ", 0) == 0 && (agreements.empty() || indent <= itemIndent))
        {
            if (agreements.size() >= 32)
                return Status::Fail(ERROR_BAD_FORMAT, "package manifest contains too many agreements");
            agreements.push_back(PackageAgreement());
            itemIndent = indent;
            property.erase(0, 2);
        }
        else if (agreements.empty() || indent <= itemIndent)
        {
            return Status::Fail(ERROR_BAD_FORMAT, "package agreement list is malformed");
        }

        std::string key;
        std::string value;
        if (!SplitProperty(property, key, value)) continue;
        const bool block = value == "|" || value == "|-" || value == "|+" ||
                           value == ">" || value == ">-" || value == ">+";
        if (block)
        {
            const bool folded = value[0] == '>';
            const bool keep = value.size() > 1 && value[1] == '+';
            std::vector<std::string> blockLines;
            std::size_t blockIndent = 0;
            while (index + 1 < lines.size())
            {
                const std::string &next = lines[index + 1];
                std::size_t nextIndent = 0;
                while (nextIndent < next.size() && next[nextIndent] == ' ') ++nextIndent;
                if (nextIndent == next.size())
                {
                    blockLines.push_back(std::string());
                    ++index;
                    continue;
                }
                if (nextIndent <= indent) break;
                if (!blockIndent) blockIndent = nextIndent;
                if (nextIndent < blockIndent) break;
                blockLines.push_back(next.substr(blockIndent));
                ++index;
            }
            value = FoldBlock(blockLines, folded, keep);
        }

        PackageAgreement &agreement = agreements.back();
        if (key == "AgreementLabel") agreement.label = value;
        else if (key == "Agreement") agreement.text = value;
        else if (key == "AgreementUrl") agreement.url = value;
    }
    for (const PackageAgreement &agreement : agreements)
    {
        if (agreement.label.size() > 100 || agreement.text.size() > 10000 || agreement.url.size() > 2048 ||
            (agreement.text.empty() && agreement.url.empty()))
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
        const int typeRank = TypeRank(candidate.type);
        unsupportedType = unsupportedType || typeRank < 0;
        if (architectureRank < 0 || typeRank < 0 || candidate.url.empty() || !IsSha256(candidate.sha256) ||
            (options.hasInstallerType && !AsciiEquals(candidate.type, options.installerType)))
            continue;
        if (ContainsCaseInsensitive(candidate.unsupportedOsArchitectures, ArchitectureName(machine)) ||
            !MeetsMinimumOsVersion(candidate.minimumOsVersion) || candidate.hasDependencies ||
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
    InstallerEntry defaults;
    InstallerEntry *current = nullptr;
    bool inInstallers = false;
    std::size_t installerItemIndent = 0;
    NestedSection section = NestedSection::None;
    std::size_t sectionIndent = 0;
    InstallerEntry *sectionTarget = nullptr;
    bool hasExpectedCode = false;
    DWORD expectedCode = 0;

    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line = RemoveYamlComment(yaml.substr(offset, end == std::string::npos ? yaml.size() - offset : end - offset));
        if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
        offset = end == std::string::npos ? yaml.size() + 1 : end + 1;
        if (Trim(line).empty()) continue;

        if (Trim(line) == "Agreements:") manifest.hasAgreements = true;

        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') ++indent;
        if (indent < line.size() && line[indent] == '\t')
            return Status::Fail(ERROR_BAD_FORMAT, "manifest uses a tab for YAML indentation");
        const std::string content = line.substr(indent);

        if (inInstallers && content.rfind("- ", 0) == 0 &&
            (!current || indent <= installerItemIndent))
        {
            manifest.installers.push_back(InstallerEntry());
            current = &manifest.installers.back();
            installerItemIndent = indent;
            section = NestedSection::None;
            sectionTarget = nullptr;
            hasExpectedCode = false;
            std::string key;
            std::string value;
            if (SplitProperty(content.substr(2), key, value))
            {
                section = SetInstallerProperty(*current, key, value);
                if (section != NestedSection::None)
                {
                    sectionIndent = indent;
                    sectionTarget = current;
                }
            }
            continue;
        }

        if (section != NestedSection::None && indent >= sectionIndent && content.rfind("- ", 0) == 0 && sectionTarget)
        {
            const std::string item = UnquoteYaml(content.substr(2));
            if (section == NestedSection::UnsupportedArchitectures) sectionTarget->unsupportedOsArchitectures.push_back(item);
            else if (section == NestedSection::InstallModes) sectionTarget->installModes.push_back(item);
            else if (section == NestedSection::SuccessCodes)
            {
                DWORD code = 0;
                if (!ParseCode(item, code)) return Status::Fail(ERROR_BAD_FORMAT, "InstallerSuccessCodes contains a non-numeric value");
                sectionTarget->successCodes.push_back(code);
            }
            else if (section == NestedSection::ExpectedReturnCodes)
            {
                std::string key;
                std::string value;
                if (SplitProperty(content.substr(2), key, value) && key == "InstallerReturnCode")
                {
                    DWORD code = 0;
                    if (!ParseCode(value, code)) return Status::Fail(ERROR_BAD_FORMAT, "ExpectedReturnCodes contains a non-numeric code");
                    hasExpectedCode = true;
                    expectedCode = code;
                    sectionTarget->expectedReturnCodes[code] = "expected installer response";
                }
            }
            else if (section == NestedSection::Dependencies) sectionTarget->hasDependencies = true;
            else if (section == NestedSection::Authentication) sectionTarget->hasAuthentication = true;
            else if (section == NestedSection::Markets) sectionTarget->hasMarketRestrictions = true;
            continue;
        }

        std::string key;
        std::string value;
        if (!SplitProperty(content, key, value))
            continue;

        if (section != NestedSection::None && indent > sectionIndent && sectionTarget)
        {
            if (section == NestedSection::Switches) SetSwitch(sectionTarget->switches, key, value);
            else if (section == NestedSection::ExpectedReturnCodes && key == "ReturnResponse" && hasExpectedCode)
                sectionTarget->expectedReturnCodes[expectedCode] = value;
            else if (section == NestedSection::Dependencies) sectionTarget->hasDependencies = true;
            else if (section == NestedSection::Authentication) sectionTarget->hasAuthentication = true;
            else if (section == NestedSection::Markets) sectionTarget->hasMarketRestrictions = true;
            continue;
        }

        section = NestedSection::None;
        sectionTarget = nullptr;
        hasExpectedCode = false;
        if (indent == 0 && key == "Installers")
        {
            inInstallers = true;
            current = nullptr;
            continue;
        }
        if (indent == 0)
        {
            inInstallers = false;
            if (key == "PackageIdentifier") manifest.id = value;
            else if (key == "PackageVersion") manifest.version = value;
            else
            {
                section = SetInstallerProperty(defaults, key, value);
                if (section != NestedSection::None)
                {
                    sectionIndent = indent;
                    sectionTarget = &defaults;
                }
            }
            continue;
        }
        if (inInstallers && current && indent > installerItemIndent)
        {
            section = SetInstallerProperty(*current, key, value);
            if (section != NestedSection::None)
            {
                sectionIndent = indent;
                sectionTarget = current;
            }
        }
    }

    if (manifest.id.empty() || manifest.version.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest is missing PackageIdentifier or PackageVersion");
    if (manifest.installers.empty() && (!defaults.architecture.empty() || !defaults.url.empty()))
        manifest.installers.push_back(defaults);
    for (InstallerEntry &installer : manifest.installers)
        InheritInstaller(installer, defaults);

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
    manifest.defaultHasDependencies = defaults.hasDependencies;
    manifest.defaultHasAuthentication = defaults.hasAuthentication;
    manifest.defaultHasMarketRestrictions = defaults.hasMarketRestrictions;
    manifest.defaultDownloadCommandProhibited = defaults.downloadCommandProhibited;
    Status agreementStatus = ParsePackageAgreements(yaml, manifest.agreements);
    if (!agreementStatus) return agreementStatus;
    if (manifest.hasAgreements && manifest.agreements.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "manifest declares package agreements without displayable terms");
    manifest.hasAgreements = !manifest.agreements.empty();
    return Status::Ok();
}

namespace
{

void SetDetailProperty(PackageDetails &details, const std::string &key, const std::string &value)
{
    if (key == "Publisher") details.publisher = value;
    else if (key == "PublisherUrl") details.publisherUrl = value;
    else if (key == "PublisherSupportUrl") details.supportUrl = value;
    else if (key == "Author") details.author = value;
    else if (key == "PackageName") details.packageName = value;
    else if (key == "PackageUrl") details.packageUrl = value;
    else if (key == "License") details.license = value;
    else if (key == "LicenseUrl") details.licenseUrl = value;
    else if (key == "Copyright") details.copyright = value;
    else if (key == "ShortDescription") details.shortDescription = value;
    else if (key == "Description") details.description = value;
    else if (key == "ReleaseNotesUrl") details.releaseNotesUrl = value;
    else if (key == "Documentations" || key == "DocumentUrl") details.documentationUrl = value;
}

bool FlowScalarComplete(const std::string &text, char quote)
{
    if (text.size() < 2)
        return false;
    std::size_t index = 1;
    while (index < text.size())
    {
        if (quote == '\'')
        {
            if (text[index] != '\'')
            {
                ++index;
                continue;
            }
            if (index + 1 < text.size() && text[index + 1] == '\'')
            {
                index += 2;
                continue;
            }
            return true;
        }
        if (text[index] == '\\')
        {
            index += 2;
            continue;
        }
        if (text[index] == '"')
            return true;
        ++index;
    }
    return false;
}

void AppendUtf8(std::string &text, unsigned long code)
{
    if (code < 0x80)
    {
        text.push_back(static_cast<char>(code));
    }
    else if (code < 0x800)
    {
        text.push_back(static_cast<char>(0xc0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else
    {
        text.push_back(static_cast<char>(0xe0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

std::string DecodeFlowScalar(const std::string &text, char quote)
{
    std::string value;
    std::size_t index = 1;
    while (index < text.size())
    {
        const char character = text[index];
        if (character == quote)
        {
            if (quote == '\'' && index + 1 < text.size() && text[index + 1] == '\'')
            {
                value.push_back('\'');
                index += 2;
                continue;
            }
            break;
        }
        if (character == '\n')
        {
            std::size_t breaks = 0;
            while (index < text.size() && (text[index] == '\n' || text[index] == ' '))
            {
                if (text[index] == '\n')
                    ++breaks;
                ++index;
            }
            if (breaks > 1)
                value.append(breaks - 1, '\n');
            else if (!value.empty())
                value.push_back(' ');
            continue;
        }
        if (quote == '"' && character == '\\' && index + 1 < text.size())
        {
            const char escape = text[index + 1];
            index += 2;
            switch (escape)
            {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '0': value.push_back('\0'); break;
                case 'u':
                {
                    unsigned long code = 0;
                    std::size_t digits = 0;
                    while (digits < 4 && index < text.size() && isxdigit(static_cast<unsigned char>(text[index])))
                    {
                        const char digit = text[index];
                        code = code * 16 + static_cast<unsigned long>(digit <= '9' ? digit - '0' : (digit | 0x20) - 'a' + 10);
                        ++index;
                        ++digits;
                    }
                    AppendUtf8(value, code);
                    break;
                }
                default: value.push_back(escape); break;
            }
            continue;
        }
        value.push_back(character);
        ++index;
    }
    return value;
}

} // namespace

Status ParseLocaleManifest(const std::string &yaml, PackageDetails &details)
{
    details = PackageDetails();
    std::vector<std::string> lines;
    std::size_t offset = 0;
    while (offset <= yaml.size())
    {
        const std::size_t end = yaml.find('\n', offset);
        std::string line(yaml.substr(offset, end == std::string::npos ? yaml.size() - offset : end - offset));
        offset = end == std::string::npos ? yaml.size() + 1 : end + 1;
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.pop_back();
        lines.push_back(line);
    }

    const auto indentOf = [](const std::string &line) {
        std::size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ')
            ++indent;
        return indent;
    };
    const auto isBlank = [&indentOf](const std::string &line) { return indentOf(line) == line.size(); };
    const auto isSequence = [](const std::string &line) {
        const std::string trimmed = Trim(line);
        return trimmed.size() > 1 && trimmed[0] == '-' && trimmed[1] == ' ';
    };

    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const std::string line = lines[index];
        if (isBlank(line) || indentOf(line))
            continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = Trim(line.substr(0, colon));
        const std::string raw = Trim(line.substr(colon + 1));
        if (key.empty())
            continue;

        if (key == "Tags")
        {
            while (index + 1 < lines.size() && isSequence(lines[index + 1]))
                details.tags.push_back(UnquoteYaml(Trim(lines[++index]).substr(2)));
            continue;
        }
        if (raw.empty())
            continue;

        if (raw == "|" || raw == "|-" || raw == "|+" || raw == ">" || raw == ">-" || raw == ">+")
        {
            std::vector<std::string> block;
            std::size_t blockIndent = 0;
            while (index + 1 < lines.size())
            {
                if (isBlank(lines[index + 1]))
                {
                    block.push_back(std::string());
                    ++index;
                    continue;
                }
                const std::size_t indent = indentOf(lines[index + 1]);
                if (!blockIndent)
                    blockIndent = indent;
                if (indent < blockIndent)
                    break;
                block.push_back(lines[++index].substr(blockIndent));
            }
            SetDetailProperty(details, key, FoldBlock(block, raw[0] == '>', raw.size() > 1 && raw[1] == '+'));
            continue;
        }

        if (raw[0] == '"' || raw[0] == '\'')
        {
            std::string text = raw;
            while (!FlowScalarComplete(text, raw[0]) && index + 1 < lines.size())
            {
                text.push_back('\n');
                text += Trim(lines[++index]);
            }
            SetDetailProperty(details, key, DecodeFlowScalar(text, raw[0]));
            continue;
        }

        std::string plain = raw;
        while (index + 1 < lines.size() && !isBlank(lines[index + 1]) && indentOf(lines[index + 1]) && !isSequence(lines[index + 1]))
        {
            plain.push_back(' ');
            plain += Trim(lines[++index]);
        }
        SetDetailProperty(details, key, plain);
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
        default: return "unknown";
    }
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
    if (UrlEncodePath("manifests/a/A+B/1.0/A+B.yaml") != L"manifests/a/A%2BB/1.0/A%2BB.yaml")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "URL encoder self-test failed");
    return Status::Ok();
}

} // namespace lntget
