/*
 * PROJECT:     LiberNT Drivers
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
#include <cwctype>

namespace lntdrv
{

namespace
{

const WCHAR CatalogRoot[] = L"https://www.catalog.update.microsoft.com/";
const size_t MaximumPage = 8 * 1024 * 1024;
const size_t MaximumArchitectureChecks = 16;
const unsigned MaximumPages = 4;
const unsigned MaximumAttempts = 3;
const size_t RowsPerPage = 25;

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
    return HtmlText(Inner(row, "id=\"" + id + "_C" + std::to_string(column) + "_R", "</td>"));
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

DWORD ParseResults(const std::string &html, const std::wstring &query, std::vector<CatalogUpdate> &updates,
                   size_t &rows)
{
    static const std::string Marker = "<tr id=\"";
    static const std::string Details = "goToDetails(\"";
    bool table = html.find("id=\"ctl00_catalogBody_updateMatches\"") != std::string::npos;
    size_t pos = 0, before = updates.size();

    rows = 0;
    if (!table)
        return html.find("id=\"ctl00_catalogBody_noResultText\"") != std::string::npos ? ERROR_SUCCESS
                                                                                         : ERROR_LNTDRV_CATALOG_FORMAT;
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

Catalog::Catalog() : m_Cancel(NULL)
{
    DWORD timeout = 30000;

    m_Session = InternetOpenW(L"LiberNT-Drivers/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (m_Session)
    {
        InternetSetOptionW(m_Session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(m_Session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(m_Session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    }
}

Catalog::~Catalog()
{
    if (m_Session)
        InternetCloseHandle(m_Session);
}

void Catalog::SetCancel(volatile LONG *cancel)
{
    m_Cancel = cancel;
}

bool Catalog::Cancelled() const
{
    return m_Cancel && InterlockedCompareExchange(m_Cancel, 0, 0) != 0;
}

DWORD Catalog::SendOnce(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink,
                        DWORD &status)
{
    static const WCHAR Form[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
    LPCWSTR accept[] = { L"*/*", NULL };
    URL_COMPONENTSW parts = { sizeof(parts) };
    WCHAR host[256], path[2048], extra[2048];
    DWORD flags, size = sizeof(status);
    ULONGLONG total = 0;
    WCHAR length[32] = L"";
    std::vector<BYTE> buffer(64 * 1024);

    status = 0;
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

    InternetHandle connection(InternetConnectW(m_Session, std::wstring(host, parts.dwHostNameLength).c_str(),
                                               parts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0));
    if (!connection.Get())
        return GetLastError();
    flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES |
            INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
            (parts.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0);
    InternetHandle request(HttpOpenRequestW(connection.Get(), verb,
                                            (std::wstring(path, parts.dwUrlPathLength) +
                                             std::wstring(extra, parts.dwExtraInfoLength)).c_str(),
                                            NULL, NULL, accept, flags, 0));
    if (!request.Get())
        return GetLastError();
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
        DWORD read = 0, error;

        if (Cancelled())
            return ERROR_CANCELLED;
        if (!InternetReadFile(request.Get(), buffer.data(), (DWORD)buffer.size(), &read))
            return GetLastError();
        if (!read)
            break;
        error = sink(buffer.data(), read, total);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

DWORD Catalog::Send(LPCWSTR verb, const std::wstring &url, const std::string &body, const Sink &sink,
                    const std::function<DWORD()> &reset)
{
    DWORD error = ERROR_SUCCESS;

    for (unsigned attempt = 0; attempt < MaximumAttempts; attempt++)
    {
        DWORD status = 0;
        bool transient;

        if (Cancelled())
            return ERROR_CANCELLED;
        if (attempt)
        {
            for (DWORD waited = 0; waited < 1000 * attempt && !Cancelled(); waited += 100)
                Sleep(100);
            if (Cancelled())
                return ERROR_CANCELLED;
            error = reset();
            if (error)
                return error;
        }
        error = SendOnce(verb, url, body, sink, status);
        transient = error == ERROR_INTERNET_TIMEOUT || error == ERROR_INTERNET_CONNECTION_RESET ||
                    error == ERROR_INTERNET_CONNECTION_ABORTED ||
                    (error == ERROR_INTERNET_CANNOT_CONNECT && attempt == 0) ||
                    (error == ERROR_BAD_NET_RESP && (status == 408 || status == 429 || status >= 500));
        if (!transient)
            break;
    }
    return error;
}

DWORD Catalog::Fetch(LPCWSTR verb, const std::wstring &url, const std::string &body, std::string &text)
{
    text.clear();
    return Send(
        verb, url, body,
        [&text](const BYTE *data, DWORD size, ULONGLONG) -> DWORD {
            if (text.size() + size > MaximumPage)
                return ERROR_BUFFER_OVERFLOW;
            text.append((const char *)data, size);
            return ERROR_SUCCESS;
        },
        [&text]() -> DWORD {
            text.clear();
            return ERROR_SUCCESS;
        });
}

DWORD Catalog::Search(const std::wstring &query, std::vector<CatalogUpdate> &updates)
{
    auto cached = m_Searches.find(query);
    std::wstring base = std::wstring(CatalogRoot) + L"Search.aspx?q=" + Wide(UrlEncode(Utf8(query))) +
                        L"&scol=DateComputed&sdir=desc";

    updates.clear();
    if (cached != m_Searches.end())
    {
        updates = cached->second;
        return ERROR_SUCCESS;
    }
    for (unsigned page = 0; page < MaximumPages; page++)
    {
        std::string html;
        size_t rows = 0;
        DWORD error = Fetch(L"GET", page ? base + L"&p=" + std::to_wstring(page) : base, std::string(), html);

        if (!error)
            error = ParseResults(html, query, updates, rows);
        if (error)
            return error;
        if (rows < RowsPerPage || LastPage(html))
            break;
    }
    m_Searches[query] = updates;
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
        update.Architecture = cached->second.first;
        update.Model = cached->second.second;
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
    while (!update.Model.empty() && update.Model.back() == L' ')
        update.Model.pop_back();
    m_Details[update.Id] = std::make_pair(update.Architecture, update.Model);
    return ERROR_SUCCESS;
}

DWORD Catalog::FindUpdates(const std::vector<std::wstring> &queries, std::vector<CatalogUpdate> &updates)
{
    SystemTarget system = CurrentSystem();

    updates.clear();
    for (const std::wstring &query : queries)
    {
        std::vector<CatalogUpdate> rows, unique;
        DWORD status = Search(query, rows);

        if (status)
            return status;
        for (CatalogUpdate &row : rows)
        {
            bool seen = std::any_of(unique.begin(), unique.end(), [&row](const CatalogUpdate &other) {
                return other.Title == row.Title && other.Version == row.Version && other.Bytes == row.Bytes;
            });
            row.Fit = ProductFit(row.Products, system);
            if (!seen && row.Fit > 0)
                unique.push_back(row);
        }
        std::stable_sort(unique.begin(), unique.end(), [](const CatalogUpdate &left, const CatalogUpdate &right) {
            int order;

            if (left.Fit != right.Fit)
                return left.Fit > right.Fit;
            order = CompareVersions(left.Version, right.Version);
            if (order)
                return order > 0;
            return DateKey(left.LastUpdated) > DateKey(right.LastUpdated);
        });
        if (unique.size() > MaximumArchitectureChecks)
            unique.resize(MaximumArchitectureChecks);
        for (CatalogUpdate &update : unique)
        {
            status = QueryDetails(update);
            if (status == ERROR_CANCELLED || IsOfflineError(status))
                return status;
            if (ArchitectureMatches(update.Architecture))
                updates.push_back(update);
        }
        if (!updates.empty())
            return ERROR_SUCCESS;
    }
    return ERROR_NOT_FOUND;
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

DWORD Catalog::Download(const CatalogFile &file, const std::wstring &path, const ProgressCallback &progress)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE digest[20];
    std::vector<BYTE> existing;
    std::wstring partial = path + L".partial";
    ULONGLONG received = 0;
    HANDLE handle;
    DWORD error = ERROR_SUCCESS;

    if (HashFile(path, BCRYPT_SHA1_ALGORITHM, existing) == ERROR_SUCCESS && existing == file.Sha1)
    {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (progress && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            ULONGLONG size = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            progress(size, size);
        }
        return ERROR_SUCCESS;
    }
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0)
        return ERROR_NOT_SUPPORTED;
    if (BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return ERROR_NOT_SUPPORTED;
    }
    handle = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        error = GetLastError();
    else
    {
        error = Send(
            L"GET", file.Url, std::string(),
            [&](const BYTE *data, DWORD size, ULONGLONG total) -> DWORD {
                DWORD written = 0;

                if (!WriteFile(handle, data, size, &written, NULL) || written != size)
                    return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
                if (BCryptHashData(hash, (PUCHAR)data, size, 0) != 0)
                    return ERROR_NOT_SUPPORTED;
                received += size;
                if (progress)
                    progress(received, total);
                return ERROR_SUCCESS;
            },
            [&]() -> DWORD {
                received = 0;
                BCryptDestroyHash(hash);
                hash = NULL;
                if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER || !SetEndOfFile(handle))
                    return GetLastError();
                return BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) == 0 ? ERROR_SUCCESS
                                                                                     : ERROR_NOT_SUPPORTED;
            });
        CloseHandle(handle);
    }
    if (!error && (!hash || BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0))
        error = ERROR_NOT_SUPPORTED;
    if (!error && (file.Sha1.size() != sizeof(digest) || memcmp(file.Sha1.data(), digest, sizeof(digest)) != 0))
        error = ERROR_CRC;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
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
