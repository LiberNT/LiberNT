/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Microsoft Update Catalog client
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wininet.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace lntdrv
{

struct CatalogUpdate
{
    std::wstring Id;
    std::wstring Title;
    std::wstring Products;
    std::wstring Classification;
    std::wstring LastUpdated;
    std::wstring Version;
    std::wstring Size;
    ULONGLONG Bytes = 0;
    std::wstring Architecture;
    std::wstring Model;
    std::vector<std::wstring> HardwareIds;
    std::wstring Query;
    int Fit = 1;
    bool Detailed = false;
};

struct CatalogFile
{
    std::wstring Url;
    std::wstring FileName;
    std::vector<BYTE> Sha1;
};

struct SystemTarget
{
    ULONG Build = 0;
    bool Server = false;
};

using ProgressCallback = std::function<void(ULONGLONG received, ULONGLONG total)>;

class Catalog
{
public:
    Catalog();
    ~Catalog();
    Catalog(const Catalog &) = delete;
    Catalog &operator=(const Catalog &) = delete;

    void SetCancel(volatile LONG *cancel);
    DWORD SearchPage(const std::wstring &query, unsigned page, std::vector<CatalogUpdate> &updates, bool &last);
    DWORD QueryDetails(CatalogUpdate &update);
    DWORD QueryAllDetails(std::vector<CatalogUpdate> &updates);
    DWORD FindUpdates(const std::vector<std::wstring> &queries, std::vector<CatalogUpdate> &updates);
    DWORD QueryFiles(const std::wstring &id, std::vector<CatalogFile> &files);
    DWORD Download(const CatalogFile &file, const std::wstring &path, const ProgressCallback &progress);
    const std::wstring &OtherArchitectures() const;
    const std::wstring &OtherModel() const;

private:
    using Sink = std::function<DWORD(const BYTE *data, DWORD size, ULONGLONG total)>;

    bool Cancelled() const;
    DWORD SendOnce(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink, DWORD &status);
    DWORD Send(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink,
               const std::function<DWORD()> &reset);
    DWORD Fetch(LPCWSTR verb, const std::wstring &url, const std::string &body, std::string &text);

    HINTERNET m_Session;
    volatile LONG *m_Cancel;
    std::map<std::wstring, CatalogUpdate> m_Details;
    std::map<std::wstring, std::pair<std::vector<CatalogUpdate>, bool>> m_Pages;
    std::wstring m_OtherArchitectures;
    std::wstring m_OtherModel;
};

std::wstring NativeArchitecture();
SystemTarget CurrentSystem();
bool ArchitectureMatches(const std::wstring &architecture);
int ProductFit(const std::wstring &products, const SystemTarget &system);
int CompareVersions(const std::wstring &left, const std::wstring &right);
ULONG DateKey(const std::wstring &date);

}
