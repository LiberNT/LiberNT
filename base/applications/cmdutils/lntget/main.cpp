/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     lntget command-line interface
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "arp.hpp"
#include "portable.hpp"
#include "dependencies.hpp"
#include "hash.hpp"
#include "installer.hpp"
#include "manifest.hpp"
#include "details.hpp"
#include "gui.hpp"
#include "source.hpp"
#include "util.hpp"
#include "yaml.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace lntget
{

namespace
{

struct CommandOptions
{
    std::string query;
    SearchField field = SearchField::Any;
    bool exact = false;
    std::size_t count = 20;
    bool hasCount = false;
    bool all = false;
    SelectionOptions selection;
    bool hasDownloadDirectory = false;
    std::wstring downloadDirectory;
    bool hasVersion = false;
    std::string version;
};

void PrintHelp()
{
    std::printf(
        "lntget %.*s - LiberNT package manager for the WinGet community repository\n\n"
        "Usage:\n"
        "  lntget search [[-q] <query>] [--id|--name|--moniker|--tag|--command <value>] [-e] [-n <count>]\n"
        "  lntget show <query> [--id <id>] [-e] [-v <version>]\n"
        "  lntget download <query> [--id <id>] [-v <version>] [--architecture <arch>]\n"
        "  lntget install <query> [--id <id>] [-v <version>] [--architecture <arch>] [--silent|--interactive]\n"
        "  lntget list [[-q] <query>] [--id|--name <value>] [-e] [-n <count>]\n"
        "  lntget upgrade [[-q] <query>] [--id <id>] [-e] [-v <version>] [--all] [--silent|--interactive]\n"
        "  lntget uninstall <query> [--id <id>] [--name <name>] [--product-code <code>] [-e] [--silent|--interactive]\n"
        "  lntget source list|update|import <source2.msix>\n"
        "  lntget gui [[-q] <query>]\n"
        "  lntget selftest\n\n"
        "Common WinGet-compatible options:\n"
        "  -q, --query <query>        Search text\n"
        "  --id <id>                 Match a package identifier\n"
        "  --name <name>             Match only package names\n"
        "  --moniker <moniker>       Match only package monikers\n"
        "  --tag <tag>               Match only package tags\n"
        "  --command <command>       Match only provided commands\n"
        "  -e, --exact               Use an exact match\n"
        "  -v, --version <version>   Use this package version instead of the latest\n"
        "  -n, --count <count>       Limit search results (1-1000)\n"
        "  -r, --all                 Upgrade every package that has an available upgrade\n"
        "  -s, --source winget       Select the community source\n"
        "  -a, --architecture <arch> x86, x64, arm, or arm64\n"
        "  --installer-type <type>   Require an installer type\n"
        "  --scope <scope>           user or machine\n"
        "  --locale <locale>         Require an installer locale\n"
        "  -d, --download-directory <p>\n"
        "                            Store the verified installer in p\n"
        "  -h, --silent              Run with silent installer switches\n"
        "  -i, --interactive         Run interactively\n"
        "  --disable-interactivity   Never prompt for user input\n"
        "  --accept-package-agreements\n"
        "                            Accept package license agreements\n"
        "  --accept-source-agreements\n"
        "                            Accepted; the winget source has no agreements\n",
        static_cast<int>(sizeof(Version) - 1), Version);
}

Status ParseCount(const std::string &value, std::size_t &count)
{
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end || parsed < 1 || parsed > 1000)
        return Status::Fail(ERROR_INVALID_PARAMETER, "--count must be between 1 and 1000");
    count = parsed;
    return Status::Ok();
}

Status ParseOptions(const std::vector<std::string> &arguments, std::size_t first, CommandOptions &options)
{
    bool modeSpecified = false;
    const auto selectField = [&options](SearchField field) -> Status {
        if (options.field != SearchField::Any && options.field != field)
            return Status::Fail(ERROR_INVALID_PARAMETER, "only one search field option may be specified");
        options.field = field;
        return Status::Ok();
    };
    for (std::size_t index = first; index < arguments.size(); ++index)
    {
        const std::string &argument = arguments[index];
        const auto next = [&]() -> const std::string * {
            if (index + 1 >= arguments.size())
                return nullptr;
            return &arguments[++index];
        };
        if (argument == "-q" || argument == "--query")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            options.query = *value;
        }
        else if (argument == "--id")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--id requires a value");
            Status status = selectField(SearchField::Id);
            if (!status) return status;
            options.query = *value;
        }
        else if (argument == "--name" || argument == "--moniker" || argument == "--tag" || argument == "--command")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            const SearchField field = argument == "--name" ? SearchField::Name :
                                      argument == "--moniker" ? SearchField::Moniker :
                                      argument == "--tag" ? SearchField::Tag : SearchField::Command;
            Status status = selectField(field);
            if (!status) return status;
            options.query = *value;
        }
        else if (argument == "--product-code")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--product-code requires a value");
            Status status = selectField(SearchField::ProductCode);
            if (!status) return status;
            options.query = *value;
        }
        else if (argument == "-e" || argument == "--exact")
        {
            options.exact = true;
        }
        else if (argument == "-v" || argument == "--version")
        {
            const std::string *value = next();
            if (!value || value->empty()) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            options.hasVersion = true;
            options.version = *value;
        }
        else if (argument == "-n" || argument == "--count")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            Status status = ParseCount(*value, options.count);
            if (!status) return status;
            options.hasCount = true;
        }
        else if (argument == "-r" || argument == "--all" || argument == "--recurse")
        {
            options.all = true;
        }
        else if (argument == "-a" || argument == "--architecture")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--architecture requires a value");
            const std::string architecture = AsciiLower(*value);
            if (architecture != "x86" && architecture != "x64" && architecture != "arm" && architecture != "arm64")
                return Status::Fail(ERROR_INVALID_PARAMETER, "unsupported architecture: " + *value);
            options.selection.hasArchitecture = true;
            options.selection.architecture = architecture;
        }
        else if (argument == "--installer-type")
        {
            const std::string *value = next();
            if (!value || value->empty()) return Status::Fail(ERROR_INVALID_PARAMETER, "--installer-type requires a value");
            options.selection.hasInstallerType = true;
            options.selection.installerType = AsciiLower(*value);
        }
        else if (argument == "--scope")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--scope requires a value");
            const std::string scope = AsciiLower(*value);
            if (scope != "user" && scope != "machine") return Status::Fail(ERROR_INVALID_PARAMETER, "--scope must be user or machine");
            options.selection.hasScope = true;
            options.selection.scope = scope;
        }
        else if (argument == "--locale")
        {
            const std::string *value = next();
            if (!value || value->empty()) return Status::Fail(ERROR_INVALID_PARAMETER, "--locale requires a value");
            options.selection.hasLocale = true;
            options.selection.locale = *value;
        }
        else if (argument == "-d" || argument == "--download-directory")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, "--download-directory requires a value");
            options.hasDownloadDirectory = true;
            options.downloadDirectory = WideFromUtf8(*value);
        }
        else if (argument == "-h" || argument == "--silent")
        {
            if (modeSpecified) return Status::Fail(ERROR_INVALID_PARAMETER, "--silent and --interactive cannot be combined");
            options.selection.mode = InstallMode::Silent;
            modeSpecified = true;
        }
        else if (argument == "-i" || argument == "--interactive")
        {
            if (modeSpecified) return Status::Fail(ERROR_INVALID_PARAMETER, "--silent and --interactive cannot be combined");
            options.selection.mode = InstallMode::Interactive;
            modeSpecified = true;
        }
        else if (argument == "-s" || argument == "--source")
        {
            const std::string *value = next();
            if (!value) return Status::Fail(ERROR_INVALID_PARAMETER, argument + " requires a value");
            if (!AsciiEquals(*value, "winget")) return Status::Fail(ERROR_NOT_SUPPORTED, "only the winget community source is currently supported");
        }
        else if (argument == "--disable-interactivity")
        {
            options.selection.disableInteractivity = true;
        }
        else if (argument == "--accept-package-agreements")
        {
            options.selection.acceptPackageAgreements = true;
        }
        else if (argument == "--accept-source-agreements")
        {
        }
        else if (!argument.empty() && argument[0] == '-')
        {
            return Status::Fail(ERROR_INVALID_PARAMETER, "unknown option: " + argument);
        }
        else if (options.query.empty())
        {
            options.query = argument;
        }
        else
        {
            return Status::Fail(ERROR_INVALID_PARAMETER, "unexpected argument: " + argument);
        }
    }
    return Status::Ok();
}

int Report(const Status &status)
{
    if (status)
        return 0;
    std::fprintf(stderr, "lntget: %s (0x%08lx)\n", status.message.c_str(), status.code);
    return status.code ? static_cast<int>(status.code) : 1;
}

Status ResolveManifest(SourceManager &source, const CommandOptions &options, PackageRecord &package, Manifest &manifest)
{
    if (options.query.empty())
        return Status::Fail(ERROR_INVALID_PARAMETER, "a package query or --id is required");
    PackageQuery query;
    query.text = options.query;
    query.field = options.field;
    query.exact = options.exact;
    Status status = source.Resolve(query, package);
    if (!status)
        return status;
    if (options.hasVersion)
        package.version = options.version;
    std::string yaml;
    status = source.FetchInstallerManifest(package, yaml);
    if (!status)
        return status;
    status = ParseInstallerManifest(yaml, manifest);
    if (!status)
        return status;
    if (!AsciiEquals(package.id, manifest.id) || package.version != manifest.version)
        return Status::Fail(ERROR_BAD_FORMAT, "manifest identity does not match the source index");
    return Status::Ok();
}

Status SearchCommand(SourceManager &source, const CommandOptions &options)
{
    PackageQuery query;
    query.text = options.query;
    query.field = options.field;
    query.exact = options.exact;
    query.count = options.count;
    std::vector<PackageRecord> packages;
    Status status = source.Search(query, packages);
    if (!status)
        return status;
    if (packages.empty())
        return Status::Fail(ERROR_NOT_FOUND, "no package found matching input criteria");

    std::size_t nameWidth = 4;
    std::size_t idWidth = 2;
    std::size_t versionWidth = 7;
    for (const PackageRecord &package : packages)
    {
        nameWidth = std::min<std::size_t>(40, std::max(nameWidth, package.name.size()));
        idWidth = std::min<std::size_t>(48, std::max(idWidth, package.id.size()));
        versionWidth = std::min<std::size_t>(20, std::max(versionWidth, package.version.size()));
    }
    std::printf("%-*s  %-*s  %-*s  Source\n", static_cast<int>(nameWidth), "Name", static_cast<int>(idWidth), "Id", static_cast<int>(versionWidth), "Version");
    std::printf("%-*s  %-*s  %-*s  ------\n", static_cast<int>(nameWidth), std::string(nameWidth, '-').c_str(), static_cast<int>(idWidth), std::string(idWidth, '-').c_str(), static_cast<int>(versionWidth), std::string(versionWidth, '-').c_str());
    for (const PackageRecord &package : packages)
        std::printf("%-*.*s  %-*.*s  %-*.*s  winget\n", static_cast<int>(nameWidth), static_cast<int>(nameWidth), package.name.c_str(), static_cast<int>(idWidth), static_cast<int>(idWidth), package.id.c_str(), static_cast<int>(versionWidth), static_cast<int>(versionWidth), package.version.c_str());
    std::printf("LNTGET_SEARCH_DONE count=%Iu\n", packages.size());
    return Status::Ok();
}

void PrintPackage(const PackageRecord &package, const Manifest &manifest, const InstallerEntry &installer)
{
    std::printf("Found %s [%s]\n", package.name.c_str(), package.id.c_str());
    std::printf("Version:        %s\n", package.version.c_str());
    std::printf("Source:         winget\n");
    std::printf("Architecture:   %s\n", installer.architecture.c_str());
    if (installer.nestedType.empty())
        std::printf("Installer type: %s\n", installer.type.c_str());
    else
        std::printf("Installer type: %s (%s)\n", installer.type.c_str(), installer.nestedType.c_str());
    for (const PackageDependency &dependency : installer.packageDependencies)
        std::printf("Dependency:     %s%s%s\n", dependency.id.c_str(), dependency.minimumVersion.empty() ? "" : " >= ",
                    dependency.minimumVersion.c_str());
    std::printf("Installer URL:  %s\n", installer.url.c_str());
    std::printf("Installer SHA:  %s\n", installer.sha256.c_str());
    std::printf("Manifest:       %s %s (%Iu installer%s)\n", manifest.id.c_str(), manifest.version.c_str(), manifest.installers.size(), manifest.installers.size() == 1 ? "" : "s");
}

void PrintPackageAgreements(const Manifest &manifest)
{
    if (manifest.agreements.empty()) return;
    std::printf("Package agreements:\n");
    for (const PackageAgreement &agreement : manifest.agreements)
    {
        std::printf("  %s\n", agreement.label.empty() ? "Agreement" : agreement.label.c_str());
        if (!agreement.text.empty()) std::printf("    %s\n", agreement.text.c_str());
        if (!agreement.url.empty()) std::printf("    %s\n", agreement.url.c_str());
    }
}

Status ShowCommand(SourceManager &source, const CommandOptions &options)
{
    PackageRecord package;
    Manifest manifest;
    Status status = ResolveManifest(source, options, package, manifest);
    if (!status)
        return status;
    InstallerEntry installer;
    status = SelectInstaller(manifest, options.selection, installer);
    if (!status)
        return status;
    PrintPackage(package, manifest, installer);
    PrintPackageAgreements(manifest);
    std::printf("LNTGET_SHOW_DONE id=%s version=%s\n", package.id.c_str(), package.version.c_str());
    return Status::Ok();
}

Status DownloadOrInstallCommand(SourceManager &source, const CommandOptions &options, bool install)
{
    PackageRecord package;
    Manifest manifest;
    Status status = ResolveManifest(source, options, package, manifest);
    if (!status)
        return status;
    if (manifest.hasAgreements)
    {
        PrintPackageAgreements(manifest);
        if (!options.selection.acceptPackageAgreements)
            return Status::Fail(ERROR_CANCELLED, "package agreements must be accepted with --accept-package-agreements");
    }
    InstallerEntry installer;
    SelectionOptions selection = options.selection;
    selection.downloadOnly = !install;
    status = SelectInstaller(manifest, selection, installer);
    if (!status)
        return status;
    PrintPackage(package, manifest, installer);
    if (install)
    {
        status = InstallPackageDependencies(source, installer, selection);
        if (!status)
            return status;
    }

    InstallerService service;
    std::wstring path;
    status = service.Download(package, installer, options.hasDownloadDirectory ? &options.downloadDirectory : nullptr, path);
    if (!status)
        return status;
    std::printf("Installer downloaded to: %s\n", Utf8FromWide(path).c_str());
    if (!install)
    {
        std::printf("LNTGET_DOWNLOAD_DONE id=%s path=%s\n", package.id.c_str(), Utf8FromWide(path).c_str());
        return Status::Ok();
    }

    DWORD exitCode = 0;
    TrackedPackage tracked;
    status = service.Install(package, installer, path, selection.mode, exitCode, &tracked);
    if (!status)
        return status;
    std::printf("Successfully installed %s.\n", package.name.c_str());
    for (const std::string &identity : tracked.arpIdentities)
        std::printf("Registered as:  %s\n", identity.c_str());
    std::printf("LNTGET_INSTALL_DONE id=%s exit=%lu registered=%Iu\n", package.id.c_str(), exitCode, tracked.arpIdentities.size());
    return Status::Ok();
}

struct InstalledGroup
{
    std::string id;
    std::string name;
    std::string installerType;
    std::string available;
    std::vector<const InstalledPackage *> entries;
};

Status ValidateInstalledField(const CommandOptions &options)
{
    if (options.field == SearchField::Any || options.field == SearchField::Id || options.field == SearchField::Name ||
        options.field == SearchField::ProductCode)
        return Status::Ok();
    return Status::Fail(ERROR_NOT_SUPPORTED, "installed packages can only be matched by query, --id, --name, or --product-code");
}

bool InstalledMatches(const InstalledPackage &package, const CommandOptions &options)
{
    if (options.query.empty())
        return true;
    const auto match = [&options](const std::string &value) {
        if (value.empty()) return false;
        return options.exact ? value == options.query : AsciiContains(value, options.query);
    };
    const std::string identity = package.entry.Identity();
    const std::string name = Utf8FromWide(package.entry.name);
    if (options.field == SearchField::Id)
        return match(package.packageId) || match(identity);
    if (options.field == SearchField::Name)
        return match(name);
    if (options.field == SearchField::ProductCode)
        return _wcsicmp(package.entry.keyName.c_str(), WideFromUtf8(options.query).c_str()) == 0;
    return match(package.packageId) || match(identity) || match(name) || match(package.packageName);
}

std::string InstalledId(const InstalledPackage &package)
{
    return package.correlated ? package.packageId : package.entry.Identity();
}

std::vector<InstalledGroup> GroupInstalled(const std::vector<InstalledPackage> &packages, const CommandOptions &options)
{
    std::vector<InstalledGroup> groups;
    for (const InstalledPackage &package : packages)
    {
        if (!InstalledMatches(package, options))
            continue;
        const std::string id = InstalledId(package);
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&id](const InstalledGroup &candidate) { return AsciiEquals(candidate.id, id); });
        if (group == groups.end())
        {
            groups.push_back(InstalledGroup());
            group = groups.end() - 1;
            group->id = id;
            group->name = Utf8FromWide(package.entry.name);
            group->installerType = package.installerType;
        }
        if (!package.available.empty())
            group->available = package.available;
        group->entries.push_back(&package);
    }
    return groups;
}

Status CollectInstalled(SourceManager &source, std::vector<InstalledPackage> &packages)
{
    const Status status = CollectInstalledPackages(&source, packages);
    if (!status)
        std::fprintf(stderr, "lntget: installed packages are shown without the winget source: %s\n", status.message.c_str());
    return Status::Ok();
}

Status ListCommand(SourceManager &source, const CommandOptions &options, bool upgradesOnly)
{
    Status status = ValidateInstalledField(options);
    if (!status)
        return status;
    std::vector<InstalledPackage> packages;
    CollectInstalled(source, packages);
    std::vector<const InstalledPackage *> rows;
    for (const InstalledPackage &package : packages)
    {
        if (InstalledMatches(package, options) && (!upgradesOnly || !package.available.empty()))
            rows.push_back(&package);
    }
    if (options.hasCount && rows.size() > options.count)
        rows.resize(options.count);
    if (rows.empty())
    {
        if (upgradesOnly)
        {
            std::printf("No installed package has an available upgrade.\n");
            std::printf("LNTGET_UPGRADE_LIST_DONE count=0\n");
            return Status::Ok();
        }
        return Status::Fail(ERROR_NOT_FOUND, "no installed package found matching input criteria");
    }

    std::size_t nameWidth = 4;
    std::size_t idWidth = 2;
    std::size_t versionWidth = 7;
    std::size_t availableWidth = 9;
    for (const InstalledPackage *package : rows)
    {
        nameWidth = std::min<std::size_t>(40, std::max(nameWidth, Utf8FromWide(package->entry.name).size()));
        idWidth = std::min<std::size_t>(48, std::max(idWidth, InstalledId(*package).size()));
        versionWidth = std::min<std::size_t>(20, std::max(versionWidth, Utf8FromWide(package->entry.version).size()));
        availableWidth = std::min<std::size_t>(20, std::max(availableWidth, package->available.size()));
    }
    std::printf("%-*s  %-*s  %-*s  %-*s  Source\n", static_cast<int>(nameWidth), "Name", static_cast<int>(idWidth), "Id",
                static_cast<int>(versionWidth), "Version", static_cast<int>(availableWidth), "Available");
    std::printf("%s  %s  %s  %s  ------\n", std::string(nameWidth, '-').c_str(), std::string(idWidth, '-').c_str(),
                std::string(versionWidth, '-').c_str(), std::string(availableWidth, '-').c_str());
    for (const InstalledPackage *package : rows)
    {
        const std::string name = Utf8FromWide(package->entry.name);
        const std::string version = package->entry.version.empty() ? std::string("Unknown") : Utf8FromWide(package->entry.version);
        std::printf("%-*.*s  %-*.*s  %-*.*s  %-*.*s  %s\n", static_cast<int>(nameWidth), static_cast<int>(nameWidth), name.c_str(),
                    static_cast<int>(idWidth), static_cast<int>(idWidth), InstalledId(*package).c_str(),
                    static_cast<int>(versionWidth), static_cast<int>(versionWidth), version.c_str(),
                    static_cast<int>(availableWidth), static_cast<int>(availableWidth), package->available.c_str(),
                    package->correlated ? "winget" : "");
    }
    std::printf("LNTGET_%s_DONE count=%Iu\n", upgradesOnly ? "UPGRADE_LIST" : "LIST", rows.size());
    return Status::Ok();
}

Status SelectInstalledGroup(const std::vector<InstalledGroup> &groups, InstalledGroup &selected)
{
    if (groups.empty())
        return Status::Fail(ERROR_NOT_FOUND, "no installed package found matching input criteria");
    if (groups.size() > 1)
    {
        std::printf("Multiple installed packages match:\n");
        for (const InstalledGroup &group : groups)
            std::printf("  %s [%s]\n", group.name.c_str(), group.id.c_str());
        return Status::Fail(ERROR_MORE_DATA, "multiple installed packages match; refine the query or use --id with -e");
    }
    selected = groups[0];
    return Status::Ok();
}

Status UninstallCommand(SourceManager &source, const CommandOptions &options)
{
    if (options.query.empty())
        return Status::Fail(ERROR_INVALID_PARAMETER, "a package query or --id is required");
    Status status = ValidateInstalledField(options);
    if (!status)
        return status;
    std::vector<InstalledPackage> packages;
    CollectInstalled(source, packages);
    InstalledGroup group;
    status = SelectInstalledGroup(GroupInstalled(packages, options), group);
    if (!status)
        return status;

    std::printf("Found %s [%s]\n", group.name.c_str(), group.id.c_str());
    std::printf("Starting package uninstall...\n");
    bool tracked = false;
    for (const InstalledPackage *package : group.entries)
    {
        DWORD exitCode = 0;
        status = UninstallArpEntry(package->entry, package->installerType, options.selection.mode, exitCode);
        if (!status)
            return status;
        tracked = tracked || !package->installerType.empty();
        std::printf("Removed %s\n", package->entry.Identity().c_str());
    }
    if (tracked)
    {
        status = RemoveTrackedPackage(group.id);
        if (!status)
            std::fprintf(stderr, "lntget: %s\n", status.message.c_str());
    }
    std::printf("Successfully uninstalled %s.\n", group.name.c_str());
    std::printf("LNTGET_UNINSTALL_DONE id=%s entries=%Iu\n", group.id.c_str(), group.entries.size());
    return Status::Ok();
}

Status UpgradeCommand(SourceManager &source, const CommandOptions &options)
{
    if (options.all && options.hasVersion)
        return Status::Fail(ERROR_INVALID_PARAMETER, "--version cannot be combined with --all");
    if (options.query.empty() && !options.all)
        return ListCommand(source, options, true);
    Status status = ValidateInstalledField(options);
    if (!status)
        return status;
    std::vector<InstalledPackage> packages;
    CollectInstalled(source, packages);
    std::vector<InstalledGroup> groups = GroupInstalled(packages, options);
    if (options.all)
    {
        std::size_t upgraded = 0;
        std::size_t failed = 0;
        Status failure = Status::Ok();
        for (const InstalledGroup &group : groups)
        {
            if (group.available.empty())
                continue;
            CommandOptions install = options;
            install.query = group.id;
            install.field = SearchField::Id;
            install.exact = true;
            status = DownloadOrInstallCommand(source, install, true);
            if (!status)
            {
                std::fprintf(stderr, "lntget: %s failed to upgrade: %s\n", group.id.c_str(), status.message.c_str());
                if (failure)
                    failure = status;
                ++failed;
                continue;
            }
            ++upgraded;
        }
        std::printf("LNTGET_UPGRADE_DONE count=%Iu failed=%Iu\n", upgraded, failed);
        if (!failure)
            return Status::Fail(failure.code, DecimalString(failed) + " package(s) failed to upgrade");
        return Status::Ok();
    }

    InstalledGroup group;
    status = SelectInstalledGroup(groups, group);
    if (!status)
        return status;
    if (group.available.empty() && !options.hasVersion)
        return Status::Fail(ERROR_NOT_FOUND, "no applicable upgrade found for " + group.name);
    std::printf("Upgrading %s [%s] to %s\n", group.name.c_str(), group.id.c_str(), (options.hasVersion ? options.version : group.available).c_str());
    CommandOptions install = options;
    install.query = group.id;
    install.field = SearchField::Id;
    install.exact = true;
    status = DownloadOrInstallCommand(source, install, true);
    if (!status)
        return status;
    std::printf("LNTGET_UPGRADE_DONE count=1\n");
    return Status::Ok();
}

Status SelfTestCommand()
{
    Status status = RunYamlSelfTests();
    if (!status)
        return status;
    status = RunManifestSelfTests();
    if (!status)
        return status;
    status = RunHashSelfTests();
    if (!status)
        return status;
    status = RunDetailsSelfTests();
    if (!status)
        return status;
    status = RunSQLiteIndexSelfTests();
    if (!status)
        return status;
    status = RunInstallerSelfTests();
    if (!status)
        return status;
    status = RunArpSelfTests();
    if (!status)
        return status;
    status = RunSourceSelfTests();
    if (!status)
        return status;
    status = RunPortableSelfTests();
    if (!status)
        return status;
    status = RunHttpSelfTests();
    if (!status)
        return status;
    std::size_t count = 0;
    status = ParseCount("1000", count);
    if (!status || count != 1000 || ParseCount("1001", count))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command count parser self-test failed");
    CommandOptions options;
    static const char *const FieldConflict[] = {"lntget", "search", "--name", "Seven Zip", "--moniker", "7z"};
    status = ParseOptions(std::vector<std::string>(FieldConflict, FieldConflict + sizeof(FieldConflict) / sizeof(FieldConflict[0])), 2, options);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command field conflict self-test failed");
    options = CommandOptions();
    static const char *const ModeConflict[] = {"lntget", "install", "--silent", "--interactive", "Example.Tool"};
    status = ParseOptions(std::vector<std::string>(ModeConflict, ModeConflict + sizeof(ModeConflict) / sizeof(ModeConflict[0])), 2, options);
    if (status)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command mode conflict self-test failed");
    options = CommandOptions();
    static const char *const VersionOption[] = {"lntget", "install", "--id", "jqlang.jq", "-v", "1.8.1"};
    status = ParseOptions(std::vector<std::string>(VersionOption, VersionOption + sizeof(VersionOption) / sizeof(VersionOption[0])), 2, options);
    if (!status || !options.hasVersion || options.version != "1.8.1" || options.query != "jqlang.jq")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "command version option self-test failed");
    std::printf("LNTGET_SELFTEST_DONE architecture=%s\n", ArchitectureName(CurrentMachineArchitecture()).c_str());
    return Status::Ok();
}

} // namespace

int Main(const std::vector<std::string> &arguments)
{
    if (arguments.size() < 2 || arguments[1] == "--help" || arguments[1] == "-?" || arguments[1] == "help")
    {
        PrintHelp();
        return 0;
    }
    if (arguments[1] == "--version" || arguments[1] == "-v")
    {
        std::printf(
            "lntget v%.*s\n"
            "Copyright (C) 2026 Ahmed Arif\n"
            "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n\n"
            "WinGet community repository and source index:\n"
            "Copyright (c) Microsoft Corporation. All rights reserved.\n"
            "Licensed under the MIT License.\n",
            static_cast<int>(sizeof(Version) - 1), Version);
        return 0;
    }
    if (arguments[1] == "selftest")
        return Report(SelfTestCommand());

    SourceManager source;
    if (arguments[1] == "source")
    {
        if (arguments.size() >= 3 && arguments[2] == "list" && arguments.size() == 3)
        {
            std::printf("Name    Argument                                      Type\n");
            std::printf("------- --------------------------------------------- ----------------------------\n");
            std::printf("winget  https://cdn.winget.microsoft.com/cache       Microsoft.PreIndexed.Package\n");
            std::printf("LNTGET_SOURCE_LIST_DONE count=1\n");
            return 0;
        }
        if (arguments.size() >= 3 && arguments[2] == "update" && arguments.size() == 3)
        {
            const Status status = source.Update();
            if (status) std::printf("LNTGET_SOURCE_UPDATE_DONE\n");
            return Report(status);
        }
        if (arguments.size() == 4 && arguments[2] == "import")
        {
            const Status status = source.Import(WideFromUtf8(arguments[3]));
            if (status) std::printf("LNTGET_SOURCE_IMPORT_DONE\n");
            return Report(status);
        }
        return Report(Status::Fail(ERROR_INVALID_PARAMETER, "usage: lntget source list|update|import <source2.msix>"));
    }

    CommandOptions options;
    Status status = ParseOptions(arguments, 2, options);
    if (!status)
        return Report(status);
    if (arguments[1] == "gui" || arguments[1] == "--gui") return Report(RunCatalogGui(source, options.query));
    if (arguments[1] == "search") return Report(SearchCommand(source, options));
    if (arguments[1] == "show") return Report(ShowCommand(source, options));
    if (arguments[1] == "download") return Report(DownloadOrInstallCommand(source, options, false));
    if (arguments[1] == "install") return Report(DownloadOrInstallCommand(source, options, true));
    if (arguments[1] == "list" || arguments[1] == "ls") return Report(ListCommand(source, options, false));
    if (arguments[1] == "upgrade" || arguments[1] == "update") return Report(UpgradeCommand(source, options));
    if (arguments[1] == "uninstall" || arguments[1] == "remove" || arguments[1] == "rm") return Report(UninstallCommand(source, options));
    return Report(Status::Fail(ERROR_INVALID_PARAMETER, "unknown command: " + arguments[1]));
}

} // namespace lntget

int wmain(int argc, wchar_t **argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
        arguments.push_back(lntget::Utf8FromWide(argv[index]));
    return lntget::Main(arguments);
}
