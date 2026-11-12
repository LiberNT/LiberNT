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
    std::wstring DriverVersion;
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

struct SearchPlan
{
    std::vector<std::wstring> Terms;
    std::vector<std::wstring> Ids;
    std::vector<CatalogUpdate> Updates;
    std::wstring OtherArchitectures;
    std::wstring OtherModel;
    DWORD Error = ERROR_SUCCESS;
    bool Complete = false;

    size_t Term = 0;
    unsigned Page = 0;
    size_t Detailed = 0;
    std::vector<std::wstring> Seen;
    std::vector<std::wstring> Others;
    DWORD Failure = ERROR_SUCCESS;
    bool Rows = false;
    bool Fits = false;
    bool Supported = false;
};

class ProgressCallback
{
public:
    virtual void operator()(ULONGLONG received, ULONGLONG total) = 0;
};

class ThrottleCallback
{
public:
    virtual void operator()(DWORD seconds) = 0;
};

class Catalog
{
public:
    Catalog();
    ~Catalog();
    Catalog(const Catalog &) = delete;
    Catalog &operator=(const Catalog &) = delete;

    void SetCancel(volatile LONG *cancel);
    void SetThrottleCallback(ThrottleCallback *callback);
    void SetThrottleWait(bool wait);
    void SetDetailWorkers(unsigned workers);
    volatile LONG *Cancel() const;
    ThrottleCallback *Throttle() const;
    bool Deferred() const;
    DWORD WaitForSearchWindow();
    DWORD SearchPage(const std::wstring &query, unsigned page, std::vector<CatalogUpdate> &updates, bool &last);
    DWORD QueryDetails(CatalogUpdate &update);
    DWORD QueryAllDetails(std::vector<CatalogUpdate> &updates);
    DWORD Advance(SearchPlan &plan);
    DWORD FindUpdates(const std::vector<std::wstring> &queries, std::vector<CatalogUpdate> &updates);
    DWORD QueryFiles(const std::wstring &id, std::vector<CatalogFile> &files);
    DWORD Download(const CatalogFile &file, const std::wstring &path, ProgressCallback *progress);
    const std::wstring &OtherArchitectures() const;
    const std::wstring &OtherModel() const;
    unsigned Searches() const;

private:
    class Sink
    {
    public:
        virtual DWORD Write(const BYTE *data, DWORD size, ULONGLONG total) = 0;
        virtual DWORD Reset() = 0;
    };

    struct Segment
    {
        Catalog *Owner;
        const std::wstring *Url;
        const std::wstring *Path;
        ULONGLONG From;
        ULONGLONG To;
        ULONGLONG Done;
        volatile LONG64 *Received;
        DWORD Error;
    };

    static DWORD WINAPI SegmentMain(LPVOID parameter);
    bool Cancelled() const;
    DWORD Finish(SearchPlan &plan) const;
    DWORD NextTerm(SearchPlan &plan) const;
    DWORD LockSearches(bool &locked);
    DWORD WaitThrottle(ULONGLONG delay);
    DWORD SendOnce(LPCWSTR verb, const std::wstring &url, const std::string &body, Sink &sink, DWORD &status);
    DWORD Send(LPCWSTR verb, const std::wstring &url, const std::string &body, Sink &sink);
    DWORD Fetch(LPCWSTR verb, const std::wstring &url, const std::string &body, std::string &text);
    DWORD OpenRequest(LPCWSTR verb, const std::wstring &url, HINTERNET &connection, HINTERNET &request);
    DWORD QuerySize(const std::wstring &url, ULONGLONG &size, bool &ranges);
    DWORD FetchRange(Segment &segment, HANDLE file, DWORD &status);
    DWORD FetchSegment(Segment &segment);
    DWORD DownloadStream(const std::wstring &url, const std::wstring &path, ProgressCallback *progress);
    DWORD DownloadSegments(const std::wstring &url, const std::wstring &path, ULONGLONG size,
                           ProgressCallback *progress);

    HINTERNET m_Session;
    volatile LONG *m_Cancel;
    std::map<std::wstring, CatalogUpdate> m_Details;
    std::map<std::wstring, std::pair<std::vector<CatalogUpdate>, bool>> m_Pages;
    std::wstring m_OtherArchitectures;
    std::wstring m_OtherModel;
    ThrottleCallback *m_Throttle;
    HANDLE m_Lock;
    unsigned m_Searches;
    unsigned m_DetailWorkers;
    bool m_Wait;
    bool m_Deferred;
};

DWORD ParseResults(const std::string &html, const std::wstring &query, std::vector<CatalogUpdate> &updates,
                   size_t &rows);
std::wstring EncodeSearchPage(const std::vector<CatalogUpdate> &updates, bool last, ULONGLONG time);
bool DecodeSearchPage(const std::wstring &text, const std::wstring &query, ULONGLONG now,
                      std::vector<CatalogUpdate> &updates, bool &last);
void UseDriverVersion(CatalogUpdate &update);
std::wstring HardwareIdCore(const std::wstring &id);
std::vector<std::wstring> CatalogSearchTerms(const std::vector<std::wstring> &ids);
bool SupportsHardwareIds(const CatalogUpdate &update, const std::vector<std::wstring> &ids);
std::vector<std::pair<ULONGLONG, ULONGLONG>> DownloadRanges(ULONGLONG size);
ULONGLONG ThrottleDelay(ULONGLONG windowStart, ULONGLONG now);
ULONGLONG ThrottleWindowStart(ULONGLONG windowStart, ULONGLONG now);
ULONGLONG ThrottleJitter(ULONGLONG seed);
std::wstring NativeArchitecture();
SystemTarget CurrentSystem();
bool ArchitectureMatches(const std::wstring &architecture);
int ProductFit(const std::wstring &products, const SystemTarget &system);
int CompareVersions(const std::wstring &left, const std::wstring &right);
ULONG DateKey(const std::wstring &date);

}
