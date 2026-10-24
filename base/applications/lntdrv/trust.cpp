/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver package signature and file hash verification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "trust.hpp"
#include "devices.hpp"
#include "text.hpp"

#include <bcrypt.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

#include <algorithm>
#include <cstring>
#include <wctype.h>

namespace lntdrv
{

namespace
{

const char DriverUsage[] = "1.3.6.1.4.1.311.10.3.5";
const ULONGLONG MaximumCatalogSize = 64 * 1024 * 1024;

struct Range
{
    ULONGLONG Offset;
    ULONGLONG Length;
};

class Hasher
{
public:
    explicit Hasher(LPCWSTR algorithm)
    {
        if (BCryptOpenAlgorithmProvider(&m_Algorithm, algorithm, NULL, 0) != 0)
            m_Algorithm = NULL;
        else if (BCryptCreateHash(m_Algorithm, &m_Hash, NULL, 0, NULL, 0, 0) != 0)
            m_Hash = NULL;
    }
    ~Hasher()
    {
        if (m_Hash)
            BCryptDestroyHash(m_Hash);
        if (m_Algorithm)
            BCryptCloseAlgorithmProvider(m_Algorithm, 0);
    }
    Hasher(const Hasher &) = delete;
    Hasher &operator=(const Hasher &) = delete;

    bool Valid() const
    {
        return m_Hash != NULL;
    }

    bool Update(const BYTE *data, ULONG size)
    {
        return BCryptHashData(m_Hash, (PUCHAR)data, size, 0) == 0;
    }

    bool Finish(std::vector<BYTE> &digest)
    {
        DWORD length = 0, size = 0;

        if (BCryptGetProperty(m_Algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&length, sizeof(length), &size, 0) != 0)
            return false;
        digest.resize(length);
        return BCryptFinishHash(m_Hash, digest.data(), length, 0) == 0;
    }

private:
    BCRYPT_ALG_HANDLE m_Algorithm = NULL;
    BCRYPT_HASH_HANDLE m_Hash = NULL;
};

std::wstring Upper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::towupper);
    return text;
}

std::wstring BaseName(const std::wstring &path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool HasExtension(const std::wstring &path, LPCWSTR extension)
{
    size_t length = wcslen(extension);
    return path.size() > length && _wcsicmp(path.c_str() + path.size() - length, extension) == 0;
}

void ListFiles(const std::wstring &directory, std::vector<std::wstring> &files)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);

    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        std::wstring name = data.cFileName, path = directory + L"\\" + name;

        if (name == L"." || name == L"..")
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ListFiles(path, files);
        else
            files.push_back(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

void ImageRanges(HANDLE file, ULONGLONG size, std::vector<Range> &ranges)
{
    BYTE header[4096];
    DWORD read = 0, pe = 0, rvaCount = 0, certOffset = 0, certSize = 0, optional, directories, checksum, certEntry;
    WORD magic = 0;
    LARGE_INTEGER start = {};

    ranges.assign(1, Range{ 0, size });
    if (!SetFilePointerEx(file, start, NULL, FILE_BEGIN) || !ReadFile(file, header, sizeof(header), &read, NULL) ||
        read < 0x40 || header[0] != 'M' || header[1] != 'Z')
    {
        return;
    }
    memcpy(&pe, header + 0x3C, sizeof(pe));
    if (pe > read || read - pe < 24 + 112 + 5 * 8 || memcmp(header + pe, "PE\0\0", 4) != 0)
        return;
    optional = pe + 24;
    memcpy(&magic, header + optional, sizeof(magic));
    if (magic == 0x10B)
    {
        memcpy(&rvaCount, header + optional + 92, sizeof(rvaCount));
        directories = optional + 96;
    }
    else if (magic == 0x20B)
    {
        memcpy(&rvaCount, header + optional + 108, sizeof(rvaCount));
        directories = optional + 112;
    }
    else
    {
        return;
    }
    if (rvaCount < 5 || directories + 5 * 8 > read)
        return;
    checksum = optional + 64;
    certEntry = directories + 4 * 8;
    memcpy(&certOffset, header + certEntry, sizeof(certOffset));
    memcpy(&certSize, header + certEntry + 4, sizeof(certSize));
    ranges.clear();
    ranges.push_back(Range{ 0, checksum });
    ranges.push_back(Range{ checksum + 4, certEntry - checksum - 4 });
    if (certOffset && certSize && certOffset >= certEntry + 8 && (ULONGLONG)certOffset + certSize <= size)
    {
        ranges.push_back(Range{ certEntry + 8, certOffset - certEntry - 8 });
        ranges.push_back(Range{ (ULONGLONG)certOffset + certSize, size - certOffset - certSize });
    }
    else
    {
        ranges.push_back(Range{ certEntry + 8, size - certEntry - 8 });
    }
}

DWORD DigestFile(const std::wstring &path, bool image, const std::vector<LPCWSTR> &algorithms,
                 std::vector<std::vector<BYTE>> &digests)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    std::vector<Hasher *> hashers;
    std::vector<Range> ranges;
    std::vector<BYTE> buffer(1024 * 1024);
    LARGE_INTEGER size;
    DWORD error = ERROR_SUCCESS;

    digests.clear();
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!GetFileSizeEx(file, &size))
    {
        error = GetLastError();
        CloseHandle(file);
        return error;
    }
    if (image)
        ImageRanges(file, (ULONGLONG)size.QuadPart, ranges);
    else
        ranges.assign(1, Range{ 0, (ULONGLONG)size.QuadPart });
    for (LPCWSTR algorithm : algorithms)
        hashers.push_back(new Hasher(algorithm));
    for (Hasher *hasher : hashers)
    {
        if (!hasher->Valid())
            error = ERROR_NOT_SUPPORTED;
    }
    for (const Range &range : ranges)
    {
        LARGE_INTEGER offset;
        ULONGLONG remaining = range.Length;

        offset.QuadPart = (LONGLONG)range.Offset;
        if (error || !SetFilePointerEx(file, offset, NULL, FILE_BEGIN))
        {
            error = error ? error : GetLastError();
            break;
        }
        while (!error && remaining)
        {
            DWORD chunk = (DWORD)std::min<ULONGLONG>(remaining, buffer.size()), read = 0;

            if (!ReadFile(file, buffer.data(), chunk, &read, NULL))
                error = GetLastError();
            else if (read != chunk)
                error = ERROR_HANDLE_EOF;
            for (Hasher *hasher : hashers)
            {
                if (!error && !hasher->Update(buffer.data(), read))
                    error = ERROR_NOT_SUPPORTED;
            }
            remaining -= read;
        }
    }
    CloseHandle(file);
    for (Hasher *hasher : hashers)
    {
        std::vector<BYTE> digest;

        if (!error && !hasher->Finish(digest))
            error = ERROR_NOT_SUPPORTED;
        digests.push_back(digest);
        delete hasher;
    }
    return error;
}

bool HasDriverUsage(PCCERT_CONTEXT certificate)
{
    DWORD size = 0;
    std::vector<BYTE> buffer;
    CERT_ENHKEY_USAGE *usage;

    if (!CertGetEnhancedKeyUsage(certificate, 0, NULL, &size) || !size)
        return false;
    buffer.resize(size);
    usage = (CERT_ENHKEY_USAGE *)buffer.data();
    if (!CertGetEnhancedKeyUsage(certificate, 0, usage, &size))
        return false;
    for (DWORD i = 0; i < usage->cUsageIdentifier; i++)
    {
        if (!strncmp(usage->rgpszUsageIdentifier[i], DriverUsage, sizeof(DriverUsage) - 1))
            return true;
    }
    return false;
}

LONG VerifyCatalog(const std::wstring &path)
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO file = { sizeof(file) };
    WINTRUST_DATA data = { sizeof(data) };
    LONG status;

    file.pcwszFilePath = path.c_str();
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    if (status == ERROR_SUCCESS)
    {
        CRYPT_PROVIDER_DATA *provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR *signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : NULL;
        CERT_CHAIN_POLICY_PARA policy = { sizeof(policy) };
        CERT_CHAIN_POLICY_STATUS result = { sizeof(result) };

        if (!signer || !signer->pChainContext || !signer->pChainContext->cChain ||
            !signer->pChainContext->rgpChain[0]->cElement)
        {
            status = TRUST_E_NOSIGNATURE;
        }
        else if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, signer->pChainContext, &policy,
                                                   &result) || result.dwError)
        {
            status = CERT_E_UNTRUSTEDROOT;
        }
        else if (!HasDriverUsage(signer->pChainContext->rgpChain[0]->rgpElement[0]->pCertContext))
        {
            status = CERT_E_WRONG_USAGE;
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
    return status;
}

DWORD CatalogContent(const std::wstring &path, std::vector<BYTE> &content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    std::vector<BYTE> bytes;
    LARGE_INTEGER size;
    HCRYPTMSG message;
    DWORD read = 0, length = 0, error = ERROR_SUCCESS;

    content.clear();
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || (ULONGLONG)size.QuadPart > MaximumCatalogSize)
        error = ERROR_INVALID_DATA;
    else
    {
        bytes.resize((size_t)size.QuadPart);
        if (!ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, NULL) || read != bytes.size())
            error = ERROR_INVALID_DATA;
    }
    CloseHandle(file);
    if (error)
        return error;
    message = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    if (!message)
        return GetLastError();
    if (!CryptMsgUpdate(message, bytes.data(), (DWORD)bytes.size(), TRUE) ||
        !CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, NULL, &length) || !length)
    {
        error = ERROR_LNTDRV_NO_SIGNATURE;
    }
    else
    {
        content.resize(length);
        if (!CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, content.data(), &length))
            error = ERROR_LNTDRV_NO_SIGNATURE;
        content.resize(length);
    }
    CryptMsgClose(message);
    return error;
}

bool Contains(const std::vector<BYTE> &content, const std::vector<BYTE> &digest)
{
    return !digest.empty() &&
           std::search(content.begin(), content.end(), digest.begin(), digest.end()) != content.end();
}

}

DWORD HashFile(const std::wstring &path, LPCWSTR algorithm, std::vector<BYTE> &digest)
{
    std::vector<std::vector<BYTE>> digests;
    DWORD error = DigestFile(path, false, std::vector<LPCWSTR>(1, algorithm), digests);

    digest = error || digests.empty() ? std::vector<BYTE>() : digests.front();
    return error;
}

DWORD VerifyPackage(const std::wstring &directory, std::wstring &file)
{
    std::vector<std::wstring> files;
    std::vector<std::vector<BYTE>> contents;
    std::vector<LPCWSTR> algorithms;
    std::wstring infText;

    file.clear();
    algorithms.push_back(BCRYPT_SHA1_ALGORITHM);
    algorithms.push_back(BCRYPT_SHA256_ALGORITHM);
    ListFiles(directory, files);
    for (const std::wstring &path : files)
    {
        if (HasExtension(path, L".inf"))
            infText += ReadInfText(path);
    }
    for (const std::wstring &path : files)
    {
        std::vector<BYTE> content;
        LONG status;
        DWORD error;

        if (!HasExtension(path, L".cat"))
            continue;
        status = VerifyCatalog(path);
        if (status != ERROR_SUCCESS)
        {
            file = BaseName(path);
            return (DWORD)status;
        }
        error = CatalogContent(path, content);
        if (error)
        {
            file = BaseName(path);
            return error;
        }
        contents.push_back(content);
    }
    if (contents.empty())
        return ERROR_LNTDRV_NO_SIGNATURE;
    for (const std::wstring &path : files)
    {
        std::vector<std::vector<BYTE>> digests;
        std::wstring name = BaseName(path);
        bool covered = false;
        DWORD error;

        if (HasExtension(path, L".cat") ||
            (!HasExtension(path, L".inf") && infText.find(Upper(name)) == std::wstring::npos))
        {
            continue;
        }
        error = DigestFile(path, !HasExtension(path, L".inf"), algorithms, digests);
        if (error)
        {
            file = name;
            return error;
        }
        for (const std::vector<BYTE> &content : contents)
        {
            for (const std::vector<BYTE> &digest : digests)
                covered = covered || Contains(content, digest);
        }
        if (!covered)
        {
            file = name;
            return ERROR_LNTDRV_NOT_COVERED;
        }
    }
    return ERROR_SUCCESS;
}

}
