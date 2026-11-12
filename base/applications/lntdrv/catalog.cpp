/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Microsoft Update Catalog client
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "catalog.hpp"
#include "text.hpp"
#include "trust.hpp"

#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <wctype.h>

namespace lntdrv
{

namespace
{

const WCHAR CatalogRoot[] = L"https://www.catalog.update.microsoft.com/";
const size_t MaximumPage = 8 * 1024 * 1024;
const size_t MaximumDetails = 100;
const size_t WantedMatches = 3;
const unsigned MaximumPages = 4;
const unsigned DetailWorkers = 6;
const WCHAR DetailsKey[] = L"SOFTWARE\\LiberNT\\Drivers\\Catalog";
const WCHAR SearchesKey[] = L"SOFTWARE\\LiberNT\\Drivers\\Catalog\\Searches";
const WCHAR ThrottleKey[] = L"SOFTWARE\\LiberNT\\Drivers\\Catalog\\Throttle";
const WCHAR SearchLock[] = L"Local\\LiberNT.DriverCatalog.Search";
const unsigned MaximumAttempts = 3;
const size_t RowsPerPage = 25;
const unsigned DownloadConnections = 8;
const ULONGLONG MinimumSegment = 1024 * 1024;
const size_t ListedHardwareIds = 300;
const ULONGLONG ThrottleWindow = 300 * 1000;
const ULONGLONG ThrottleMargin = 10 * 1000;
const ULONGLONG ThrottlePoll = 30 * 1000;
const ULONGLONG ThrottleSpread = 10 * 1000;
const ULONGLONG SearchLifetime = 24ULL * 60 * 60 * 10000000;
const size_t SearchFields = 8;

std::string Utf8(const std::wstring &text)
{
    if (text.empty())
        return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], size, NULL, NULL);
    return out;
}

std::wstring Wide(const std::string &text)
{
    if (text.empty())
        return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], size);
    return out;
}

std::wstring Upper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::towupper);
    return text;
}

std::string UrlEncode(const std::string &text)
{
    static const char Hex[] = "0123456789ABCDEF";
    std::string out;

    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
        {
            out += (char)c;
        }
        else
        {
            out += '%';
            out += Hex[c >> 4];
            out += Hex[c & 15];
        }
    }
    return out;
}

std::wstring HtmlText(const std::string &html)
{
    std::string out;
    bool space = false;

    for (size_t i = 0; i < html.size(); i++)
    {
        char c = html[i];

        if (c == '<')
        {
            size_t end = html.find('>', i);
            if (end == std::string::npos)
                break;
            i = end;
            space = true;
            continue;
        }
        if (c == '&')
        {
            size_t end = html.find(';', i);
            if (end != std::string::npos && end - i <= 8)
            {
                std::string name = html.substr(i + 1, end - i - 1);
                char decoded = 0;

                if (name == "amp")
                    decoded = '&';
                else if (name == "lt")
                    decoded = '<';
                else if (name == "gt")
                    decoded = '>';
                else if (name == "quot")
                    decoded = '"';
                else if (name == "apos" || name == "#39")
                    decoded = '\'';
                else if (name == "nbsp")
                    decoded = ' ';
                if (decoded)
                {
                    c = decoded;
                    i = end;
                }
            }
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            space = true;
            continue;
        }
        if (space && !out.empty())
            out += ' ';
        space = false;
        out += c;
    }
    return Wide(out);
}

bool IsGuid(const std::string &text)
{
    if (text.size() != 36)
        return false;
    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
                return false;
        }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }
    return true;
}

std::string Inner(const std::string &html, const std::string &key, const char *close)
{
    size_t pos = html.find(key);
    if (pos == std::string::npos)
        return std::string();
    pos = html.find('>', pos + key.size());
    if (pos == std::string::npos)
        return std::string();
    size_t end = html.find(close, pos + 1);
    if (end == std::string::npos)
        return std::string();
    return html.substr(pos + 1, end - pos - 1);
}

std::wstring Cell(const std::string &row, const std::string &id, int column)
{
    return HtmlText(Inner(row, "id=\"" + id + "_C" + Utf8(NumberText(column)) + "_R", "</td>"));
}

bool LastPage(const std::string &html)
{
    std::wstring summary = HtmlText(Inner(html, "id=\"ctl00_catalogBody_searchDuration\"", "</span>"));
    unsigned page = 0, pages = 0;
    size_t pos = summary.find(L"(page ");

    if (pos == std::wstring::npos || swscanf(summary.c_str() + pos, L"(page %u of %u)", &page, &pages) != 2)
        return true;
    return page >= pages;
}

std::vector<std::wstring> Split(const std::wstring &text, WCHAR separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;

    for (;;)
    {
        size_t end = text.find(separator, start);

        parts.push_back(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (end == std::wstring::npos)
            return parts;
        start = end + 1;
    }
}

ULONGLONG FileTimeNow()
{
    FILETIME now;
    ULARGE_INTEGER value;

    GetSystemTimeAsFileTime(&now);
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    return value.QuadPart;
}

}

std::wstring EncodeSearchPage(const std::vector<CatalogUpdate> &updates, bool last, ULONGLONG time)
{
    std::wstring text = NumberText(time) + (last ? L"\t1\n" : L"\t0\n");

    for (const CatalogUpdate &update : updates)
    {
        const std::wstring *fields[] = { &update.Id,          &update.Title,   &update.Products, &update.Classification,
                                         &update.LastUpdated, &update.Version, &update.Size };

        for (const std::wstring *field : fields)
        {
            if (field->find_first_of(L"\t\n") != std::wstring::npos)
                return std::wstring();
            text += *field + L"\t";
        }
        text += NumberText(update.Bytes) + L"\n";
    }
    return text;
}

bool DecodeSearchPage(const std::wstring &text, const std::wstring &query, ULONGLONG now,
                      std::vector<CatalogUpdate> &updates, bool &last)
{
    std::vector<std::wstring> lines = Split(text, L'\n'), header;
    ULONGLONG time;

    updates.clear();
    last = true;
    if (lines.size() < 2 || !lines.back().empty())
        return false;
    header = Split(lines[0], L'\t');
    if (header.size() != 2 || (header[1] != L"0" && header[1] != L"1"))
        return false;
    time = _wcstoui64(header[0].c_str(), NULL, 10);
    if (!time || now < time || now - time >= SearchLifetime)
        return false;
    for (size_t i = 1; i + 1 < lines.size(); i++)
    {
        std::vector<std::wstring> fields = Split(lines[i], L'\t');
        CatalogUpdate update;

        if (fields.size() != SearchFields || fields[0].empty())
        {
            updates.clear();
            return false;
        }
        update.Id = fields[0];
        update.Title = fields[1];
        update.Products = fields[2];
        update.Classification = fields[3];
        update.LastUpdated = fields[4];
        update.Version = fields[5];
        update.Size = fields[6];
        update.Bytes = _wcstoui64(fields[7].c_str(), NULL, 10);
        update.Query = query;
        updates.push_back(update);
    }
    last = header[1] == L"1";
    return true;
}

DWORD ParseResults(const std::string &html, const std::wstring &query, std::vector<CatalogUpdate> &updates,
                   size_t &rows)
{
    static const std::string Marker = "<tr id=\"";
    static const std::string Details = "goToDetails(\"";
    bool table = html.find("id=\"ctl00_catalogBody_updateMatches\"") != std::string::npos;
    size_t pos = 0, before = updates.size();

    rows = 0;
    if (!table)
    {
        if (html.find("id=\"ctl00_catalogBody_noResultText\"") != std::string::npos)
            return ERROR_SUCCESS;
        return html.find("id=\"ctl00_catalogBody_searchThrottled\"") != std::string::npos ? ERROR_LNTDRV_CATALOG_THROTTLED
                                                                                           : ERROR_LNTDRV_CATALOG_FORMAT;
    }
    while ((pos = html.find(Marker, pos)) != std::string::npos)
    {
        size_t start = pos + Marker.size(), end;
        std::string id = html.substr(start, 36), row, bytes;
        CatalogUpdate update;

        if (!IsGuid(id) || html.compare(start + 36, 2, "_R") != 0)
        {
            pos = start;
            continue;
        }
        end = html.find("</tr>", start);
        if (end == std::string::npos)
            end = html.size();
        row = html.substr(pos, end - pos);
        update.Id = Wide(id);
        update.Title = Cell(row, id, 1);
        update.Products = Cell(row, id, 2);
        update.Classification = Cell(row, id, 3);
        update.LastUpdated = Cell(row, id, 4);
        update.Version = Cell(row, id, 5);
        update.Size = HtmlText(Inner(row, "id=\"" + id + "_size\"", "</span>"));
        bytes = Inner(row, "id=\"" + id + "_originalSize\"", "</span>");
        update.Bytes = _strtoui64(bytes.c_str(), NULL, 10);
        update.Query = query;
        rows++;
        if (!update.Title.empty())
            updates.push_back(update);
        pos = end;
    }
    for (pos = 0; !rows && (pos = html.find(Details, pos)) != std::string::npos;)
    {
        std::string id = html.substr(pos + Details.size(), 36);
        size_t open = html.find('>', pos), close = html.find("</a>", pos);
        CatalogUpdate update;

        pos += Details.size();
        if (!IsGuid(id) || open == std::string::npos || close == std::string::npos || close < open)
            continue;
        update.Id = Wide(id);
        update.Title = HtmlText(html.substr(open + 1, close - open - 1));
        update.Query = query;
        if (!update.Title.empty())
            updates.push_back(update);
    }
    if (!rows)
        rows = updates.size() - before;
    return rows ? ERROR_SUCCESS : ERROR_LNTDRV_CATALOG_FORMAT;
}

namespace
{

bool AllowedDownloadHost(const std::wstring &host)
{
    static const WCHAR *const Suffixes[] = { L".windowsupdate.com", L".microsoft.com" };
    std::wstring lower = host;

    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    for (const WCHAR *suffix : Suffixes)
    {
        size_t length = wcslen(suffix);
        if (lower.size() > length && lower.compare(lower.size() - length, length, suffix) == 0)
            return true;
    }
    return false;
}

struct NamedBuild
{
    const WCHAR *Name;
    ULONG Build;
};

template <size_t Count>
ULONG FindBuild(const std::wstring &entry, const NamedBuild (&table)[Count], ULONG fallback)
{
    for (const NamedBuild &named : table)
    {
        if (entry.find(named.Name) != std::wstring::npos)
            return named.Build;
    }
    return fallback;
}

void CopyDetails(CatalogUpdate &update, const CatalogUpdate &details)
{
    update.Architecture = details.Architecture;
    update.Model = details.Model;
    update.DriverVersion = details.DriverVersion;
    update.HardwareIds = details.HardwareIds;
    update.Detailed = true;
    UseDriverVersion(update);
}

bool LoadDetails(CatalogUpdate &update)
{
    std::vector<WCHAR> value;
    DWORD type = 0, size = 0;
    HKEY key;
    LONG error;
    std::wstring text, ids;
    size_t first, second, third;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, DetailsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    error = RegQueryValueExW(key, update.Id.c_str(), NULL, &type, NULL, &size);
    if (error == ERROR_SUCCESS && type == REG_SZ && size)
    {
        value.resize(size / sizeof(WCHAR) + 1, L'\0');
        error = RegQueryValueExW(key, update.Id.c_str(), NULL, &type, (LPBYTE)value.data(), &size);
    }
    RegCloseKey(key);
    if (error != ERROR_SUCCESS || type != REG_SZ || value.empty())
        return false;
    text = value.data();
    first = text.find(L'|');
    second = first == std::wstring::npos ? first : text.find(L'|', first + 1);
    third = second == std::wstring::npos ? second : text.find(L'|', second + 1);
    if (third == std::wstring::npos)
        return false;
    update.Architecture = text.substr(0, first);
    update.Model = text.substr(first + 1, second - first - 1);
    update.DriverVersion = text.substr(third + 1);
    update.HardwareIds.clear();
    ids = text.substr(second + 1, third - second - 1);
    for (size_t start = 0; start < ids.size();)
    {
        size_t end = ids.find(L';', start);

        if (end == std::wstring::npos)
            end = ids.size();
        if (end > start)
            update.HardwareIds.push_back(ids.substr(start, end - start));
        start = end + 1;
    }
    update.Detailed = true;
    UseDriverVersion(update);
    return true;
}

void SaveDetails(const CatalogUpdate &update)
{
    std::wstring text = update.Architecture + L"|" + update.Model + L"|";
    HKEY key;

    for (size_t i = 0; i < update.HardwareIds.size(); i++)
        text += (i ? L";" : L"") + update.HardwareIds[i];
    text += L"|" + update.DriverVersion;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, DetailsKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, update.Id.c_str(), 0, REG_SZ, (const BYTE *)text.c_str(),
                   (DWORD)((text.size() + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
}

bool LoadSearch(const std::wstring &name, const std::wstring &query, std::vector<CatalogUpdate> &updates, bool &last)
{
    std::vector<WCHAR> value;
    DWORD type = 0, size = 0;
    HKEY key;
    LONG error;
    bool loaded = false;

    updates.clear();
    last = true;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SearchesKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    error = RegQueryValueExW(key, name.c_str(), NULL, &type, NULL, &size);
    if (error == ERROR_SUCCESS && type == REG_SZ && size)
    {
        value.resize(size / sizeof(WCHAR) + 1, L'\0');
        error = RegQueryValueExW(key, name.c_str(), NULL, &type, (LPBYTE)value.data(), &size);
        loaded = error == ERROR_SUCCESS && type == REG_SZ &&
                 DecodeSearchPage(value.data(), query, FileTimeNow(), updates, last);
        if (!loaded)
            RegDeleteValueW(key, name.c_str());
    }
    /* TODO: prune expired searches that are never read again */
    RegCloseKey(key);
    return loaded;
}

void SaveSearch(const std::wstring &name, const std::vector<CatalogUpdate> &updates, bool last)
{
    std::wstring text = EncodeSearchPage(updates, last, FileTimeNow());
    HKEY key;

    if (text.empty() ||
        RegCreateKeyExW(HKEY_CURRENT_USER, SearchesKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
    {
        return;
    }
    RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE *)text.c_str(), (DWORD)((text.size() + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
}

ULONGLONG NowMilliseconds()
{
    return FileTimeNow() / 10000;
}

ULONGLONG LoadWindowStart()
{
    ULONGLONG value = 0;
    DWORD type = 0, size = sizeof(value);
    HKEY key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, ThrottleKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(key, L"WindowStart", NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS ||
        type != REG_QWORD || size != sizeof(value))
    {
        value = 0;
    }
    RegCloseKey(key);
    return value;
}

void SaveWindowStart(ULONGLONG value)
{
    HKEY key;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, ThrottleKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, L"WindowStart", 0, REG_QWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
}

std::set<std::wstring> &RunningSearches()
{
    static std::set<std::wstring> running;
    return running;
}

bool ClaimSearch(const std::wstring &key)
{
    return RunningSearches().insert(key).second;
}

void ReleaseSearch(const std::wstring &key)
{
    RunningSearches().erase(key);
}

struct DetailsWork
{
    std::vector<CatalogUpdate *> *Items;
    volatile LONG Next;
    volatile LONG *Cancel;
    volatile LONG Error;
};

DWORD WINAPI DetailsWorker(LPVOID parameter)
{
    DetailsWork *work = (DetailsWork *)parameter;
    Catalog catalog;

    catalog.SetCancel(work->Cancel);
    for (;;)
    {
        LONG index = InterlockedIncrement(&work->Next);
        DWORD error;

        if (index >= (LONG)work->Items->size())
            break;
        error = catalog.QueryDetails(*(*work->Items)[index]);
        if (error == ERROR_CANCELLED || StopsCatalogScan(error))
            InterlockedExchange(&work->Error, (LONG)error);
    }
    return 0;
}

bool TransientError(DWORD error, DWORD status, unsigned attempt)
{
    return error == ERROR_INTERNET_TIMEOUT || error == ERROR_INTERNET_CONNECTION_RESET ||
           error == ERROR_INTERNET_CONNECTION_ABORTED || (error == ERROR_INTERNET_CANNOT_CONNECT && attempt == 0) ||
           (error == ERROR_BAD_NET_RESP && (status == 408 || status == 429 || status >= 500));
}

std::wstring Key(const CatalogUpdate &update)
{
    return update.Title + L"|" + update.Version + L"|" + NumberText(update.Bytes);
}

class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET handle) : m_Handle(handle) {}
    ~InternetHandle()
    {
        if (m_Handle)
            InternetCloseHandle(m_Handle);
    }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    HINTERNET Get() const { return m_Handle; }

private:
    HINTERNET m_Handle;
};

}

void UseDriverVersion(CatalogUpdate &update)
{
    if ((update.Version.empty() || !iswdigit(update.Version[0])) && !update.DriverVersion.empty() &&
        iswdigit(update.DriverVersion[0]))
    {
        update.Version = update.DriverVersion;
    }
}

std::wstring HardwareIdCore(const std::wstring &id)
{
    static const WCHAR *Tokens[] = { L"&DEV_", L"&PID_" };
    std::wstring upper = Upper(id);

    for (const WCHAR *token : Tokens)
    {
        size_t at = upper.find(token);
        size_t end;

        if (at == std::wstring::npos)
            continue;
        end = upper.find(L'&', at + wcslen(token));
        return id.substr(0, end == std::wstring::npos ? id.size() : end);
    }
    return std::wstring();
}

std::vector<std::wstring> CatalogSearchTerms(const std::vector<std::wstring> &ids)
{
    std::vector<std::wstring> terms;

    for (const std::wstring &id : ids)
    {
        std::wstring core = HardwareIdCore(id);
        const std::wstring &term = core.empty() ? id : core;
        bool seen = std::find_if(terms.begin(), terms.end(), [&term](const std::wstring &other) {
                        return _wcsicmp(other.c_str(), term.c_str()) == 0;
                    }) != terms.end();

        if (!term.empty() && !seen)
            terms.push_back(term);
    }
    return terms;
}

bool SupportsHardwareIds(const CatalogUpdate &update, const std::vector<std::wstring> &ids)
{
    if (update.HardwareIds.empty())
        return true;
    for (const std::wstring &id : ids)
    {
        std::wstring upper = Upper(id);

        if (std::find(update.HardwareIds.begin(), update.HardwareIds.end(), upper) != update.HardwareIds.end())
            return true;
    }
    if (update.HardwareIds.size() < ListedHardwareIds)
        return false;
    for (const std::wstring &id : ids)
    {
        std::wstring core = Upper(HardwareIdCore(id));

        if (core.empty())
            continue;
        for (const std::wstring &listed : update.HardwareIds)
        {
            if (listed.compare(0, core.size(), core) == 0 &&
                (listed.size() == core.size() || listed[core.size()] == L'&'))
            {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::pair<ULONGLONG, ULONGLONG>> DownloadRanges(ULONGLONG size)
{
    std::vector<std::pair<ULONGLONG, ULONGLONG>> ranges;
    ULONGLONG count = std::min<ULONGLONG>(DownloadConnections, size / MinimumSegment), start = 0;

    if (count < 2)
        return ranges;
    for (ULONGLONG i = 0; i < count; i++)
    {
        ULONGLONG length = size / count + (i < size % count ? 1 : 0);

        ranges.push_back(std::make_pair(start, start + length - 1));
        start += length;
    }
    return ranges;
}

ULONGLONG ThrottleDelay(ULONGLONG windowStart, ULONGLONG now)
{
    if (windowStart && now - windowStart < ThrottleWindow + ThrottleMargin)
        return ThrottleWindow + ThrottleMargin - (now - windowStart);
    return ThrottlePoll;
}

ULONGLONG ThrottleWindowStart(ULONGLONG windowStart, ULONGLONG now)
{
    return !windowStart || now - windowStart >= ThrottleWindow ? now : windowStart;
}

ULONGLONG ThrottleJitter(ULONGLONG seed)
{
    seed ^= seed >> 33;
    seed *= 0xFF51AFD7ED558CCDULL;
    seed ^= seed >> 33;
    return seed % ThrottleSpread;
}

Catalog::Catalog()
    : m_Cancel(NULL), m_Throttle(NULL), m_Lock(NULL), m_Searches(0), m_DetailWorkers(DetailWorkers), m_Wait(true),
      m_Deferred(false)
{
    DWORD timeout = 30000;

    ULONG connections = DownloadConnections + 2;

    m_Session = InternetOpenW(L"LiberNT-Driver-Installer/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    InternetSetOptionW(NULL, INTERNET_OPTION_MAX_CONNS_PER_SERVER, &connections, sizeof(connections));
    InternetSetOptionW(NULL, INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER, &connections, sizeof(connections));
    if (m_Session)
    {
        InternetSetOptionW(m_Session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(m_Session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(m_Session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    }
    m_Lock = CreateMutexW(NULL, FALSE, SearchLock);
}

Catalog::~Catalog()
{
    if (m_Lock)
        CloseHandle(m_Lock);
    if (m_Session)
        InternetCloseHandle(m_Session);
}

void Catalog::SetCancel(volatile LONG *cancel)
{
    m_Cancel = cancel;
}

void Catalog::SetThrottleCallback(ThrottleCallback *callback)
{
    m_Throttle = callback;
}

void Catalog::SetThrottleWait(bool wait)
{
    m_Wait = wait;
}

void Catalog::SetDetailWorkers(unsigned workers)
{
    m_DetailWorkers = workers ? workers : 1;
}

volatile LONG *Catalog::Cancel() const
{
    return m_Cancel;
}

ThrottleCallback *Catalog::Throttle() const
{
    return m_Throttle;
}

bool Catalog::Deferred() const
{
    return m_Deferred;
}

DWORD Catalog::WaitForSearchWindow()
{
    DWORD error;

    if (!m_Deferred)
        return ERROR_SUCCESS;
    error = WaitThrottle(ThrottleDelay(LoadWindowStart(), NowMilliseconds()) +
                         ThrottleJitter(GetTickCount64() ^ ((ULONGLONG)GetCurrentProcessId() << 32)));
    if (!error)
        m_Deferred = false;
    return error;
}

bool Catalog::Cancelled() const
{
    return m_Cancel && InterlockedCompareExchange(m_Cancel, 0, 0) != 0;
}

DWORD Catalog::LockSearches(bool &locked)
{
    locked = false;
    if (!m_Lock)
        return ERROR_SUCCESS;
    for (;;)
    {
        DWORD wait = WaitForSingleObject(m_Lock, 100);

        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            locked = true;
            return ERROR_SUCCESS;
        }
        if (wait != WAIT_TIMEOUT)
            return ERROR_SUCCESS;
        if (Cancelled())
            return ERROR_CANCELLED;
    }
}

DWORD Catalog::WaitThrottle(ULONGLONG delay)
{
    ULONGLONG end = GetTickCount64() + delay;

    if (m_Throttle)
        (*m_Throttle)((DWORD)((delay + 999) / 1000));
    while (!Cancelled() && GetTickCount64() < end)
        Sleep(100);
    if (m_Throttle)
        (*m_Throttle)(0);
    return Cancelled() ? ERROR_CANCELLED : ERROR_SUCCESS;
}

DWORD Catalog::OpenRequest(LPCWSTR verb, const std::wstring &url, HINTERNET &connection, HINTERNET &request)
{
    LPCWSTR accept[] = { L"*/*", NULL };
    URL_COMPONENTSW parts = { sizeof(parts) };
    WCHAR host[256], path[2048], extra[2048];
    DWORD flags;

    connection = NULL;
    request = NULL;
    if (!m_Session)
        return GetLastError() ? GetLastError() : ERROR_INTERNET_INTERNAL_ERROR;
    parts.lpszHostName = host;
    parts.dwHostNameLength = _countof(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = _countof(path);
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = _countof(extra);
    if (!InternetCrackUrlW(url.c_str(), 0, 0, &parts))
        return GetLastError();
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)
        return ERROR_INTERNET_UNRECOGNIZED_SCHEME;
    connection = InternetConnectW(m_Session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, NULL,
                                  NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!connection)
        return GetLastError();
    flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES |
            INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
            (parts.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0);
    request = HttpOpenRequestW(connection, verb,
                               (std::wstring(path, parts.dwUrlPathLength) +
                                std::wstring(extra, parts.dwExtraInfoLength)).c_str(),
                               NULL, NULL, accept, flags, 0);
    if (!request)
        return GetLastError();
    return ERROR_SUCCESS;
}

DWORD Catalog::SendOnce(LPCWSTR verb, const std::wstring &url, const std::string &body, Sink &sink, DWORD &status)
{
    static const WCHAR Form[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
    HINTERNET connectionHandle, requestHandle;
    DWORD size = sizeof(status), error;
    ULONGLONG total = 0;
    WCHAR length[32] = L"";
    std::vector<BYTE> buffer(64 * 1024);

    status = 0;
    error = OpenRequest(verb, url, connectionHandle, requestHandle);
    InternetHandle connection(connectionHandle), request(requestHandle);
    if (error)
        return error;
    if (!HttpSendRequestW(request.Get(), body.empty() ? NULL : Form, body.empty() ? 0 : (DWORD)-1L,
                          body.empty() ? NULL : (LPVOID)body.data(), (DWORD)body.size()))
    {
        return GetLastError();
    }
    if (!HttpQueryInfoW(request.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, NULL))
        return GetLastError();
    if (status != 200)
        return status == 404 ? ERROR_FILE_NOT_FOUND : ERROR_BAD_NET_RESP;
    size = sizeof(length) - sizeof(WCHAR);
    if (HttpQueryInfoW(request.Get(), HTTP_QUERY_CONTENT_LENGTH, length, &size, NULL))
        total = _wcstoui64(length, NULL, 10);

    for (;;)
    {
        DWORD read = 0;

        if (Cancelled())
            return ERROR_CANCELLED;
        if (!InternetReadFile(request.Get(), buffer.data(), (DWORD)buffer.size(), &read))
            return GetLastError();
        if (!read)
            break;
        error = sink.Write(buffer.data(), read, total);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

DWORD Catalog::Send(LPCWSTR verb, const std::wstring &url, const std::string &body, Sink &sink)
{
    DWORD error = ERROR_SUCCESS;

    for (unsigned attempt = 0; attempt < MaximumAttempts; attempt++)
    {
        DWORD status = 0;

        if (Cancelled())
            return ERROR_CANCELLED;
        if (attempt)
        {
            for (DWORD waited = 0; waited < 1000 * attempt && !Cancelled(); waited += 100)
                Sleep(100);
            if (Cancelled())
                return ERROR_CANCELLED;
            error = sink.Reset();
            if (error)
                return error;
        }
        error = SendOnce(verb, url, body, sink, status);
        if (!TransientError(error, status, attempt))
            break;
    }
    return error;
}

DWORD Catalog::Fetch(LPCWSTR verb, const std::wstring &url, const std::string &body, std::string &text)
{
    class TextSink : public Sink
    {
    public:
        explicit TextSink(std::string &out) : m_Text(out) {}

        DWORD Write(const BYTE *data, DWORD size, ULONGLONG)
        {
            if (m_Text.size() + size > MaximumPage)
                return ERROR_BUFFER_OVERFLOW;
            m_Text.append((const char *)data, size);
            return ERROR_SUCCESS;
        }

        DWORD Reset()
        {
            m_Text.clear();
            return ERROR_SUCCESS;
        }

    private:
        std::string &m_Text;
    };
    TextSink sink(text);

    text.clear();
    return Send(verb, url, body, sink);
}

DWORD Catalog::SearchPage(const std::wstring &query, unsigned page, std::vector<CatalogUpdate> &updates, bool &last)
{
    std::wstring key = query + L"#" + NumberText(page);
    auto cached = m_Pages.find(key);
    std::wstring url = std::wstring(CatalogRoot) + L"Search.aspx?q=" + Wide(UrlEncode(Utf8(query))) +
                       L"&scol=DateComputed&sdir=desc" + (page ? L"&p=" + NumberText(page) : std::wstring());
    std::string html;
    size_t rows = 0;
    DWORD error;

    updates.clear();
    last = true;
    if (cached != m_Pages.end())
    {
        updates = cached->second.first;
        last = cached->second.second;
        return ERROR_SUCCESS;
    }
    for (;;)
    {
        bool locked, claimed = true;

        error = LockSearches(locked);
        if (error)
            return error;
        if (LoadSearch(key, query, updates, last))
        {
            if (locked)
                ReleaseMutex(m_Lock);
            m_Pages[key] = std::make_pair(updates, last);
            return ERROR_SUCCESS;
        }
        if (!m_Wait && m_Deferred)
        {
            if (locked)
                ReleaseMutex(m_Lock);
            return ERROR_LNTDRV_CATALOG_THROTTLED;
        }
        if (locked)
        {
            claimed = ClaimSearch(key);
            ReleaseMutex(m_Lock);
        }
        if (!claimed)
        {
            if (Cancelled())
                return ERROR_CANCELLED;
            Sleep(100);
            continue;
        }
        error = Fetch(L"GET", url, std::string(), html);
        if (!error)
            error = ParseResults(html, query, updates, rows);
        if (locked)
            LockSearches(locked);
        if (locked)
            ReleaseSearch(key);
        if (!error)
        {
            m_Searches++;
            m_Deferred = false;
            SaveWindowStart(ThrottleWindowStart(LoadWindowStart(), NowMilliseconds()));
            last = rows < RowsPerPage || LastPage(html);
            SaveSearch(key, updates, last);
        }
        if (locked)
            ReleaseMutex(m_Lock);
        if (error != ERROR_LNTDRV_CATALOG_THROTTLED)
            break;
        if (!m_Wait)
        {
            m_Deferred = true;
            return error;
        }
        error = WaitThrottle(ThrottleDelay(LoadWindowStart(), NowMilliseconds()) +
                             ThrottleJitter(GetTickCount64() ^ ((ULONGLONG)GetCurrentProcessId() << 32)));
        if (error)
            return error;
    }
    if (error)
        return error;
    m_Pages[key] = std::make_pair(updates, last);
    return ERROR_SUCCESS;
}

DWORD Catalog::QueryDetails(CatalogUpdate &update)
{
    auto cached = m_Details.find(update.Id);
    std::string html;
    std::wstring models;
    size_t pos, end;
    DWORD error;

    if (cached != m_Details.end())
    {
        CopyDetails(update, cached->second);
        return ERROR_SUCCESS;
    }
    if (LoadDetails(update))
    {
        m_Details[update.Id] = update;
        return ERROR_SUCCESS;
    }
    error = Fetch(L"GET", std::wstring(CatalogRoot) + L"ScopedViewInline.aspx?updateid=" + update.Id, std::string(),
                  html);
    if (error)
        return error;
    pos = html.find("id=\"archDiv\"");
    if (pos == std::string::npos)
        return ERROR_LNTDRV_CATALOG_FORMAT;
    pos = html.find("</span>", pos);
    end = pos == std::string::npos ? pos : html.find("</div>", pos);
    if (end == std::string::npos)
        return ERROR_LNTDRV_CATALOG_FORMAT;
    update.Architecture = HtmlText(html.substr(pos + 7, end - pos - 7));
    models = HtmlText(Inner(html, "id=\"ScopedViewHandler_driverModel\"", "</span>"));
    update.Model = models.substr(0, models.find(L','));
    while (!update.Model.empty() && update.Model[update.Model.size() - 1] == L' ')
        update.Model.pop_back();
    update.DriverVersion = HtmlText(Inner(html, "id=\"ScopedViewHandler_version\"", "</span>"));
    update.HardwareIds.clear();
    pos = html.find("id=\"driverhwIDs\"");
    if (pos != std::string::npos)
    {
        static const std::string Item = "<div style=\"padding-bottom: 0.3em;\">";
        size_t stop = html.find("ScopedViewHandler_labelSupportedUpdateIDs", pos);
        std::string block = html.substr(pos, stop == std::string::npos ? std::string::npos : stop - pos);

        for (size_t at = block.find(Item); at != std::string::npos; at = block.find(Item, at + Item.size()))
        {
            size_t close = block.find("</div>", at);
            std::wstring id;

            if (close == std::string::npos)
                break;
            id = Upper(HtmlText(block.substr(at + Item.size(), close - at - Item.size())));
            if (!id.empty())
                update.HardwareIds.push_back(id);
        }
    }
    update.Detailed = true;
    UseDriverVersion(update);
    m_Details[update.Id] = update;
    SaveDetails(update);
    return ERROR_SUCCESS;
}

DWORD Catalog::QueryAllDetails(std::vector<CatalogUpdate> &updates)
{
    std::vector<CatalogUpdate *> pending;
    std::vector<HANDLE> threads;
    DetailsWork work = { &pending, -1, m_Cancel, ERROR_SUCCESS };

    for (CatalogUpdate &update : updates)
    {
        auto cached = m_Details.find(update.Id);

        if (cached != m_Details.end())
        {
            CopyDetails(update, cached->second);
        }
        else if (LoadDetails(update))
        {
            m_Details[update.Id] = update;
        }
        else
        {
            pending.push_back(&update);
        }
    }
    for (size_t i = 0; i < pending.size() && i < m_DetailWorkers; i++)
    {
        HANDLE thread = CreateThread(NULL, 0, DetailsWorker, &work, 0, NULL);
        if (thread)
            threads.push_back(thread);
    }
    if (threads.empty())
        DetailsWorker(&work);
    else
        WaitForMultipleObjects((DWORD)threads.size(), threads.data(), TRUE, INFINITE);
    for (HANDLE thread : threads)
        CloseHandle(thread);
    for (CatalogUpdate *update : pending)
    {
        if (update->Detailed)
            m_Details[update->Id] = *update;
    }
    if (Cancelled())
        return ERROR_CANCELLED;
    return (DWORD)work.Error;
}

DWORD Catalog::Finish(SearchPlan &plan) const
{
    plan.Complete = true;
    if (!plan.Updates.empty())
    {
        std::stable_sort(plan.Updates.begin(), plan.Updates.end(),
                         [](const CatalogUpdate &left, const CatalogUpdate &right) {
                             int order;

                             if (left.Fit != right.Fit)
                                 return left.Fit > right.Fit;
                             order = CompareVersions(left.Version, right.Version);
                             if (order)
                                 return order > 0;
                             return DateKey(left.LastUpdated) > DateKey(right.LastUpdated);
                         });
        plan.Error = ERROR_SUCCESS;
        return plan.Error;
    }
    if (!plan.Rows)
        plan.Error = plan.Failure ? plan.Failure : ERROR_NOT_FOUND;
    else if (!plan.Fits)
        plan.Error = ERROR_LNTDRV_OTHER_WINDOWS;
    else if (!plan.Supported)
        plan.Error = ERROR_NOT_FOUND;
    else
        plan.Error = ERROR_LNTDRV_OTHER_ARCH;
    for (const std::wstring &other : plan.Others)
        plan.OtherArchitectures += (plan.OtherArchitectures.empty() ? L"" : L", ") + other;
    return plan.Error;
}

DWORD Catalog::NextTerm(SearchPlan &plan) const
{
    if (!plan.Updates.empty())
        return Finish(plan);
    plan.Term++;
    plan.Page = 0;
    plan.Detailed = 0;
    plan.Seen.clear();
    if (plan.Term >= plan.Terms.size())
        return Finish(plan);
    return ERROR_SUCCESS;
}

DWORD Catalog::Advance(SearchPlan &plan)
{
    SystemTarget system = CurrentSystem();
    std::vector<CatalogUpdate> found, fresh;
    bool last = true;
    DWORD error;

    if (plan.Complete)
        return plan.Error;
    if (plan.Term >= plan.Terms.size())
        return Finish(plan);
    error = SearchPage(plan.Terms[plan.Term], plan.Page, found, last);
    if (error == ERROR_CANCELLED || StopsCatalogScan(error))
        return error;
    if (error)
    {
        plan.Failure = error;
        return NextTerm(plan);
    }
    plan.Rows = plan.Rows || !found.empty();
    for (CatalogUpdate &row : found)
    {
        std::wstring key = Key(row);

        row.Fit = ProductFit(row.Products, system);
        if (row.Fit <= 0 || std::find(plan.Seen.begin(), plan.Seen.end(), key) != plan.Seen.end())
            continue;
        plan.Seen.push_back(key);
        fresh.push_back(row);
    }
    plan.Fits = plan.Fits || !fresh.empty();
    if (fresh.size() > MaximumDetails - plan.Detailed)
        fresh.resize(MaximumDetails - plan.Detailed);
    plan.Detailed += fresh.size();
    error = QueryAllDetails(fresh);
    if (error)
        return error;
    for (const CatalogUpdate &update : fresh)
    {
        if (!update.Detailed || !SupportsHardwareIds(update, plan.Ids))
            continue;
        plan.Supported = true;
        if (ArchitectureMatches(update.Architecture))
        {
            plan.Updates.push_back(update);
            continue;
        }
        if (plan.OtherModel.empty())
            plan.OtherModel = update.Model;
        if (std::find(plan.Others.begin(), plan.Others.end(), update.Architecture) == plan.Others.end())
            plan.Others.push_back(update.Architecture);
    }
    plan.Page++;
    if (plan.Updates.size() >= WantedMatches || last || plan.Page >= MaximumPages || plan.Detailed >= MaximumDetails)
        return NextTerm(plan);
    return ERROR_SUCCESS;
}

DWORD Catalog::FindUpdates(const std::vector<std::wstring> &queries, std::vector<CatalogUpdate> &updates)
{
    SearchPlan plan;

    updates.clear();
    m_OtherArchitectures.clear();
    m_OtherModel.clear();
    plan.Terms = CatalogSearchTerms(queries);
    plan.Ids = queries;
    while (!plan.Complete)
    {
        DWORD error = Advance(plan);

        if (error == ERROR_CANCELLED || (!plan.Complete && StopsCatalogScan(error)))
            return error;
    }
    updates = plan.Updates;
    m_OtherArchitectures = plan.OtherArchitectures;
    m_OtherModel = plan.OtherModel;
    return plan.Error;
}

unsigned Catalog::Searches() const
{
    return m_Searches;
}

const std::wstring &Catalog::OtherArchitectures() const
{
    return m_OtherArchitectures;
}

const std::wstring &Catalog::OtherModel() const
{
    return m_OtherModel;
}

DWORD Catalog::QueryFiles(const std::wstring &id, std::vector<CatalogFile> &files)
{
    static const std::string Marker = "downloadInformation[0].files[";
    std::string idA = Utf8(id), text, body;
    std::map<unsigned, CatalogFile> found;
    size_t pos = 0;
    DWORD error;

    files.clear();
    body = "updateIDs=" + UrlEncode("[{\"size\":0,\"languages\":\"\",\"uidInfo\":\"" + idA +
                                    "\",\"updateID\":\"" + idA + "\"}]");
    error = Fetch(L"POST", std::wstring(CatalogRoot) + L"DownloadDialog.aspx", body, text);
    if (error)
        return error;

    while ((pos = text.find(Marker, pos)) != std::string::npos)
    {
        size_t index = pos + Marker.size(), close = text.find("].", index), field, quote, end;
        unsigned number;
        std::string name, value;

        pos = index;
        if (close == std::string::npos || close == index || close - index > 3)
            continue;
        number = (unsigned)strtoul(text.substr(index, close - index).c_str(), NULL, 10);
        field = close + 2;
        quote = text.find('\'', field);
        if (quote == std::string::npos || text.find('\n', field) < quote)
            continue;
        end = text.find('\'', quote + 1);
        if (end == std::string::npos)
            continue;
        name = text.substr(field, text.find_first_of(" =", field) - field);
        value = text.substr(quote + 1, end - quote - 1);
        if (name == "url")
            found[number].Url = Wide(value);
        else if (name == "fileName")
            found[number].FileName = Wide(value);
        else if (name == "digest")
        {
            DWORD size = 0;
            if (CryptStringToBinaryA(value.c_str(), (DWORD)value.size(), CRYPT_STRING_BASE64, NULL, &size, NULL,
                                     NULL) && size == 20)
            {
                found[number].Sha1.resize(size);
                CryptStringToBinaryA(value.c_str(), (DWORD)value.size(), CRYPT_STRING_BASE64, found[number].Sha1.data(),
                                     &size, NULL, NULL);
            }
        }
        pos = end;
    }
    if (found.empty() && text.find("downloadInformation") == std::string::npos)
        return ERROR_LNTDRV_CATALOG_FORMAT;

    for (const auto &entry : found)
    {
        const CatalogFile &file = entry.second;
        URL_COMPONENTSW parts = { sizeof(parts) };

        parts.dwHostNameLength = 1;
        if (file.Url.empty() || file.Sha1.size() != 20 || !InternetCrackUrlW(file.Url.c_str(), 0, 0, &parts) ||
            !AllowedDownloadHost(std::wstring(parts.lpszHostName, parts.dwHostNameLength)))
        {
            return ERROR_INVALID_DATA;
        }
        files.push_back(file);
    }
    return files.empty() ? ERROR_NOT_FOUND : ERROR_SUCCESS;
}

DWORD Catalog::QuerySize(const std::wstring &url, ULONGLONG &size, bool &ranges)
{
    HINTERNET connectionHandle, requestHandle;
    WCHAR text[64] = L"";
    DWORD status = 0, length = sizeof(status), error;

    size = 0;
    ranges = false;
    error = OpenRequest(L"HEAD", url, connectionHandle, requestHandle);
    InternetHandle connection(connectionHandle), request(requestHandle);
    if (error)
        return error;
    if (!HttpSendRequestW(request.Get(), NULL, 0, NULL, 0))
        return GetLastError();
    if (!HttpQueryInfoW(request.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &length, NULL))
        return GetLastError();
    if (status != 200)
        return ERROR_BAD_NET_RESP;
    length = sizeof(text) - sizeof(WCHAR);
    if (HttpQueryInfoW(request.Get(), HTTP_QUERY_CONTENT_LENGTH, text, &length, NULL))
        size = _wcstoui64(text, NULL, 10);
    text[0] = L'\0';
    length = sizeof(text) - sizeof(WCHAR);
    if (HttpQueryInfoW(request.Get(), HTTP_QUERY_ACCEPT_RANGES, text, &length, NULL))
        ranges = _wcsicmp(text, L"bytes") == 0;
    return ERROR_SUCCESS;
}

DWORD Catalog::FetchRange(Segment &segment, HANDLE file, DWORD &status)
{
    ULONGLONG from = segment.From + segment.Done;
    std::wstring range = L"Range: bytes=" + NumberText(from) + L"-" + NumberText(segment.To) + L"\r\n";
    std::wstring expected = L"bytes " + NumberText(from) + L"-";
    HINTERNET connectionHandle, requestHandle;
    WCHAR answered[128] = L"";
    DWORD size = sizeof(status), error;
    LARGE_INTEGER offset;
    std::vector<BYTE> buffer(64 * 1024);

    status = 0;
    error = OpenRequest(L"GET", *segment.Url, connectionHandle, requestHandle);
    InternetHandle connection(connectionHandle), request(requestHandle);
    if (error)
        return error;
    if (!HttpSendRequestW(request.Get(), range.c_str(), (DWORD)-1L, NULL, 0))
        return GetLastError();
    if (!HttpQueryInfoW(request.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, NULL))
        return GetLastError();
    if (status != 206)
        return ERROR_BAD_NET_RESP;
    size = sizeof(answered) - sizeof(WCHAR);
    if (!HttpQueryInfoW(request.Get(), HTTP_QUERY_CONTENT_RANGE, answered, &size, NULL) ||
        _wcsnicmp(answered, expected.c_str(), expected.size()) != 0)
    {
        return ERROR_BAD_NET_RESP;
    }
    offset.QuadPart = (LONGLONG)from;
    if (!SetFilePointerEx(file, offset, NULL, FILE_BEGIN))
        return GetLastError();
    for (;;)
    {
        DWORD read = 0, written = 0;

        if (Cancelled())
            return ERROR_CANCELLED;
        if (!InternetReadFile(request.Get(), buffer.data(), (DWORD)buffer.size(), &read))
            return GetLastError();
        if (!read)
            break;
        if (segment.From + segment.Done + read > segment.To + 1)
            return ERROR_BAD_NET_RESP;
        if (!WriteFile(file, buffer.data(), read, &written, NULL) || written != read)
            return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        segment.Done += read;
        InterlockedExchangeAdd64(segment.Received, (LONG64)read);
    }
    return segment.From + segment.Done == segment.To + 1 ? ERROR_SUCCESS : ERROR_INTERNET_CONNECTION_RESET;
}

DWORD Catalog::FetchSegment(Segment &segment)
{
    HANDLE file = CreateFileW(segment.Path->c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned failures = 0;
    DWORD error = ERROR_SUCCESS;

    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    while (failures < MaximumAttempts)
    {
        ULONGLONG before = segment.Done;
        DWORD status = 0;

        for (DWORD waited = 0; waited < 1000 * failures && !Cancelled(); waited += 100)
            Sleep(100);
        if (Cancelled())
        {
            error = ERROR_CANCELLED;
            break;
        }
        error = FetchRange(segment, file, status);
        if (!error || !TransientError(error, status, failures))
            break;
        failures = segment.Done > before ? 1 : failures + 1;
    }
    CloseHandle(file);
    return error;
}

DWORD WINAPI Catalog::SegmentMain(LPVOID parameter)
{
    Segment *segment = (Segment *)parameter;

    segment->Error = segment->Owner->FetchSegment(*segment);
    return 0;
}

DWORD Catalog::DownloadSegments(const std::wstring &url, const std::wstring &path, ULONGLONG size,
                                ProgressCallback *progress)
{
    std::vector<std::pair<ULONGLONG, ULONGLONG>> ranges = DownloadRanges(size);
    std::vector<Segment> segments(ranges.size());
    std::vector<HANDLE> threads;
    volatile LONG64 received = 0;
    LARGE_INTEGER end;
    HANDLE file;
    DWORD error = ERROR_SUCCESS;

    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    end.QuadPart = (LONGLONG)size;
    if (!SetFilePointerEx(file, end, NULL, FILE_BEGIN) || !SetEndOfFile(file))
        error = GetLastError();
    CloseHandle(file);
    if (error)
        return error;
    for (size_t i = 0; i < ranges.size(); i++)
    {
        HANDLE thread;

        segments[i] = Segment{ this, &url, &path, ranges[i].first, ranges[i].second, 0, &received, ERROR_SUCCESS };
        thread = CreateThread(NULL, 0, SegmentMain, &segments[i], 0, NULL);
        if (thread)
            threads.push_back(thread);
        else
            segments[i].Error = FetchSegment(segments[i]);
    }
    while (!threads.empty())
    {
        DWORD wait = WaitForMultipleObjects((DWORD)threads.size(), threads.data(), TRUE, 100);

        if (progress)
            (*progress)((ULONGLONG)InterlockedCompareExchange64(&received, 0, 0), size);
        if (wait == WAIT_TIMEOUT)
            continue;
        if (wait >= WAIT_OBJECT_0 + threads.size())
        {
            for (HANDLE thread : threads)
                WaitForSingleObject(thread, INFINITE);
        }
        break;
    }
    for (HANDLE thread : threads)
        CloseHandle(thread);
    for (const Segment &segment : segments)
    {
        if (segment.Error)
            return segment.Error;
    }
    if (progress)
        (*progress)(size, size);
    return ERROR_SUCCESS;
}

DWORD Catalog::DownloadStream(const std::wstring &url, const std::wstring &path, ProgressCallback *progress)
{
    class FileSink : public Sink
    {
    public:
        FileSink(HANDLE handle, ProgressCallback *progress) : m_Handle(handle), m_Progress(progress), m_Received(0) {}

        DWORD Write(const BYTE *data, DWORD size, ULONGLONG total)
        {
            DWORD written = 0;

            if (!WriteFile(m_Handle, data, size, &written, NULL) || written != size)
                return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            m_Received += size;
            if (m_Progress)
                (*m_Progress)(m_Received, total);
            return ERROR_SUCCESS;
        }

        DWORD Reset()
        {
            m_Received = 0;
            if (SetFilePointer(m_Handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER || !SetEndOfFile(m_Handle))
                return GetLastError();
            return ERROR_SUCCESS;
        }

    private:
        HANDLE m_Handle;
        ProgressCallback *m_Progress;
        ULONGLONG m_Received;
    };
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD error;

    if (handle == INVALID_HANDLE_VALUE)
        return GetLastError();
    {
        FileSink sink(handle, progress);

        error = Send(L"GET", url, std::string(), sink);
    }
    CloseHandle(handle);
    return error;
}

DWORD Catalog::Download(const CatalogFile &file, const std::wstring &path, ProgressCallback *progress)
{
    std::vector<BYTE> digest;
    std::wstring partial = path + L".partial";
    ULONGLONG size = 0;
    bool ranges = false;
    DWORD error;

    if (HashFile(path, BCRYPT_SHA1_ALGORITHM, digest) == ERROR_SUCCESS && digest == file.Sha1)
    {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (progress && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            ULONGLONG length = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            (*progress)(length, length);
        }
        return ERROR_SUCCESS;
    }
    if (QuerySize(file.Url, size, ranges) == ERROR_SUCCESS && ranges && DownloadRanges(size).size() > 1)
        error = DownloadSegments(file.Url, partial, size, progress);
    else
        error = DownloadStream(file.Url, partial, progress);
    if (!error)
        error = HashFile(partial, BCRYPT_SHA1_ALGORITHM, digest);
    if (!error && (file.Sha1.empty() || digest != file.Sha1))
        error = ERROR_CRC;
    if (!error && !MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        error = GetLastError();
    if (error)
        DeleteFileW(partial.c_str());
    return error;
}

std::wstring NativeArchitecture()
{
    SYSTEM_INFO info;

    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return L"AMD64";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return L"ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return L"X86";
        default:
            return std::wstring();
    }
}

SystemTarget CurrentSystem()
{
    typedef LONG(WINAPI * GET_VERSION)(OSVERSIONINFOEXW *);
    GET_VERSION getVersion = (GET_VERSION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOEXW info = { sizeof(info) };
    SystemTarget system;

    if (getVersion && getVersion(&info) == 0)
    {
        system.Build = info.dwBuildNumber;
        system.Server = info.wProductType != VER_NT_WORKSTATION;
    }
    if (!system.Build)
        system.Build = MAXDWORD;
    return system;
}

bool ArchitectureMatches(const std::wstring &architecture)
{
    std::wstring native = NativeArchitecture(), upper = Upper(architecture), token;
    bool known = false;

    upper += L',';
    for (WCHAR c : upper)
    {
        if (c == L',' || c == L' ' || c == L'/' || c == L';')
        {
            if (token == L"X64")
                token = L"AMD64";
            if (token == L"AMD64" || token == L"ARM64" || token == L"X86")
            {
                known = true;
                if (token == native)
                    return true;
            }
            token.clear();
        }
        else
        {
            token += c;
        }
    }
    return !known;
}

int ProductFit(const std::wstring &products, const SystemTarget &system)
{
    static const NamedBuild Client11[] = {
        { L"25H2", 26200 }, { L"24H2", 26100 }, { L"GERMANIUM", 26100 }, { L"23H2", 22631 },
        { L"22H2", 22621 }, { L"NICKEL", 22621 }, { L"21H2", 22000 }, { L"COBALT", 22000 },
    };
    static const NamedBuild Client10[] = {
        { L"VIBRANIUM", 19041 }, { L"22H2", 19045 }, { L"21H2", 19044 }, { L"21H1", 19043 },
        { L"20H2", 19042 }, { L"2004", 19041 }, { L"1909", 18363 }, { L"1903", 18362 },
        { L"1809", 17763 }, { L"1803", 17134 }, { L"1709", 16299 }, { L"FALL CREATORS", 16299 },
        { L"1703", 15063 }, { L"CREATORS", 15063 }, { L"1607", 14393 }, { L"ANNIVERSARY", 14393 },
        { L"1511", 10586 },
    };
    static const NamedBuild Servers[] = {
        { L"2025", 26100 }, { L"23H2", 25398 }, { L"2022", 20348 }, { L"IRON", 20348 }, { L"2019", 17763 },
        { L"1809", 17763 }, { L"1803", 17134 }, { L"1709", 16299 }, { L"2016", 14393 },
    };
    std::wstring upper = Upper(products);
    size_t pos = upper.find(L"WINDOWS");
    bool any = false;
    int fit = 0;

    while (pos != std::wstring::npos)
    {
        size_t next = upper.find(L"WINDOWS", pos + 7);
        std::wstring entry = upper.substr(pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
        bool client = false, server = false;
        ULONG build = 0;

        any = true;
        pos = next;
        if (entry.find(L"SERVER") != std::wstring::npos)
        {
            server = true;
            build = FindBuild(entry, Servers, 14393);
        }
        else if (entry.compare(0, 10, L"WINDOWS 11") == 0 || entry.compare(0, 9, L"WINDOWS -") == 0)
        {
            client = true;
            build = FindBuild(entry, Client11, 22000);
        }
        else if (entry.compare(0, 10, L"WINDOWS 10") == 0)
        {
            client = true;
            server = entry.compare(0, 20, L"WINDOWS 10 AND LATER") == 0;
            build = FindBuild(entry, Client10, 10240);
        }
        else if (entry.compare(0, 21, L"WINDOWS 8.1 AND LATER") == 0)
        {
            client = server = true;
            build = 9600;
        }
        else
        {
            fit = std::max(fit, 1);
            continue;
        }
        if (((system.Server && server) || (!system.Server && client)) && build <= system.Build)
            fit = 2;
    }
    return any ? fit : 1;
}

int CompareVersions(const std::wstring &left, const std::wstring &right)
{
    const WCHAR *a = left.c_str(), *b = right.c_str();

    while (*a || *b)
    {
        ULONGLONG x = _wcstoui64(a, (WCHAR **)&a, 10), y = _wcstoui64(b, (WCHAR **)&b, 10);

        if (x != y)
            return x > y ? 1 : -1;
        while (*a && *a != L'.')
            a++;
        while (*b && *b != L'.')
            b++;
        if (*a == L'.')
            a++;
        if (*b == L'.')
            b++;
    }
    return 0;
}

ULONG DateKey(const std::wstring &date)
{
    unsigned month = 0, day = 0, year = 0;

    if (swscanf(date.c_str(), L"%u/%u/%u", &month, &day, &year) != 3)
        return 0;
    return year * 10000 + month * 100 + day;
}

}
