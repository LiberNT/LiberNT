/*
 * PROJECT:     LiberNT Drivers
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
    std::wstring Query;
    int Fit = 1;
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
    DWORD Search(const std::wstring &query, std::vector<CatalogUpdate> &updates);
    DWORD QueryDetails(CatalogUpdate &update);
    DWORD FindUpdates(const std::vector<std::wstring> &queries, std::vector<CatalogUpdate> &updates);
    DWORD QueryFiles(const std::wstring &id, std::vector<CatalogFile> &files);
    DWORD Download(const CatalogFile &file, const std::wstring &path, const ProgressCallback &progress);

private:
    using Sink = std::function<DWORD(const BYTE *data, DWORD size, ULONGLONG total)>;

    bool Cancelled() const;
    DWORD SendOnce(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink, DWORD &status);
    DWORD Send(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink,
               const std::function<DWORD()> &reset);
    DWORD Fetch(LPCWSTR verb, const std::wstring &url, const std::string &body, std::string &text);

    HINTERNET m_Session;
    volatile LONG *m_Cancel;
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> m_Details;
    std::map<std::wstring, std::vector<CatalogUpdate>> m_Searches;
};

std::wstring NativeArchitecture();
SystemTarget CurrentSystem();
bool ArchitectureMatches(const std::wstring &architecture);
int ProductFit(const std::wstring &products, const SystemTarget &system);
int CompareVersions(const std::wstring &left, const std::wstring &right);
ULONG DateKey(const std::wstring &date);

}
