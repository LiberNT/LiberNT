/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Authenticode and MSIX block-map validation for the WinGet source
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <mssip.h>

#include "source_trust.hpp"
#include "hash.hpp"
#include "util.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace lntget
{

namespace
{

constexpr DWORD P7xMagic = 0x58434b50;
constexpr const char *SpcIndirectDataObjectId = "1.3.6.1.4.1.311.2.1.4";
constexpr char ExpectedPackageName[] = "Microsoft.Winget.Source";
constexpr char ExpectedPublisher[] = "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US";

class CertificateOwner
{
public:
    explicit CertificateOwner(const CERT_CONTEXT *certificate) : certificate_(certificate) {}
    ~CertificateOwner() { if (certificate_) CertFreeCertificateContext(certificate_); }
    CertificateOwner(const CertificateOwner &) = delete;
    CertificateOwner &operator=(const CertificateOwner &) = delete;
    const CERT_CONTEXT *Get() const { return certificate_; }

private:
    const CERT_CONTEXT *certificate_ = nullptr;
};

class ChainOwner
{
public:
    explicit ChainOwner(const CERT_CHAIN_CONTEXT *chain) : chain_(chain) {}
    ~ChainOwner() { if (chain_) CertFreeCertificateChain(chain_); }
    ChainOwner(const ChainOwner &) = delete;
    ChainOwner &operator=(const ChainOwner &) = delete;

private:
    const CERT_CHAIN_CONTEXT *chain_ = nullptr;
};

class StoreOwner
{
public:
    explicit StoreOwner(HCERTSTORE store) : store_(store) {}
    ~StoreOwner() { if (store_) CertCloseStore(store_, 0); }
    StoreOwner(const StoreOwner &) = delete;
    StoreOwner &operator=(const StoreOwner &) = delete;

private:
    HCERTSTORE store_ = nullptr;
};

class MessageOwner
{
public:
    explicit MessageOwner(HCRYPTMSG message) : message_(message) {}
    ~MessageOwner() { if (message_) CryptMsgClose(message_); }
    MessageOwner(const MessageOwner &) = delete;
    MessageOwner &operator=(const MessageOwner &) = delete;

private:
    HCRYPTMSG message_ = nullptr;
};

class LocalMemoryOwner
{
public:
    explicit LocalMemoryOwner(void *memory) : memory_(memory) {}
    ~LocalMemoryOwner() { if (memory_) LocalFree(memory_); }
    LocalMemoryOwner(const LocalMemoryOwner &) = delete;
    LocalMemoryOwner &operator=(const LocalMemoryOwner &) = delete;

private:
    void *memory_ = nullptr;
};

bool Attribute(const std::string &tag, const std::string &name, std::string &value)
{
    const std::string needle = name + "=";
    std::size_t offset = 0;
    while ((offset = tag.find(needle, offset)) != std::string::npos)
    {
        if (offset && ((tag[offset - 1] >= 'A' && tag[offset - 1] <= 'Z') ||
                       (tag[offset - 1] >= 'a' && tag[offset - 1] <= 'z') || tag[offset - 1] == '_'))
        {
            offset += needle.size();
            continue;
        }
        const std::size_t quote = offset + needle.size();
        if (quote >= tag.size() || (tag[quote] != '"' && tag[quote] != '\''))
            return false;
        const std::size_t end = tag.find(tag[quote], quote + 1);
        if (end == std::string::npos)
            return false;
        value = tag.substr(quote + 1, end - quote - 1);
        return true;
    }
    return false;
}

bool ParseSize(const std::string &text, std::size_t &result)
{
    errno = 0;
    char *end = nullptr;
    const unsigned long long value = strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end || value > static_cast<unsigned long long>(SIZE_MAX))
        return false;
    result = static_cast<std::size_t>(value);
    return true;
}

Status DecodeHash(const std::string &base64, uint8_t (&hash)[32])
{
    DWORD size = static_cast<DWORD>(sizeof(hash));
    if (!CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
                              hash, &size, nullptr, nullptr) || size != sizeof(hash))
        return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains an invalid SHA-256 hash");
    return Status::Ok();
}

Status VerifyBlockMapFile(const std::string &xml, const std::string &wantedName, const std::vector<uint8_t> &bytes)
{
    std::size_t fileOffset = 0;
    std::string body;
    for (;;)
    {
        fileOffset = xml.find("<File ", fileOffset);
        if (fileOffset == std::string::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map does not contain " + wantedName);
        const std::size_t tagEnd = xml.find('>', fileOffset);
        if (tagEnd == std::string::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains a truncated File element");
        const std::string tag = xml.substr(fileOffset, tagEnd - fileOffset + 1);
        std::string name;
        if (Attribute(tag, "Name", name) && name == wantedName)
        {
            const std::size_t fileEnd = xml.find("</File>", tagEnd + 1);
            if (fileEnd == std::string::npos)
                return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains an unterminated File element");
            body = xml.substr(tagEnd + 1, fileEnd - tagEnd - 1);
            std::string declaredSize;
            std::size_t size = 0;
            if (!Attribute(tag, "Size", declaredSize) || !ParseSize(declaredSize, size) || size != bytes.size())
                return Status::Fail(ERROR_CRC, "MSIX block map file size does not match " + wantedName);
            break;
        }
        fileOffset = tagEnd + 1;
    }

    std::size_t blockOffset = 0;
    std::size_t dataOffset = 0;
    while ((blockOffset = body.find("<Block ", blockOffset)) != std::string::npos)
    {
        const std::size_t tagEnd = body.find('>', blockOffset);
        if (tagEnd == std::string::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains a truncated Block element");
        const std::string tag = body.substr(blockOffset, tagEnd - blockOffset + 1);
        std::string encodedHash;
        if (!Attribute(tag, "Hash", encodedHash))
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map Block element is missing Hash");
        uint8_t expected[32]{};
        Status status = DecodeHash(encodedHash, expected);
        if (!status) return status;
        const std::size_t chunkSize = std::min<std::size_t>(64 * 1024, bytes.size() - dataOffset);
        if (!chunkSize)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map has too many blocks for " + wantedName);
        uint8_t actual[32]{};
        status = HashBytesSha256(bytes.data() + dataOffset, chunkSize, actual);
        if (!status) return status;
        if (std::memcmp(actual, expected, sizeof(actual)) != 0)
            return Status::Fail(ERROR_CRC, "MSIX signed block hash mismatch for " + wantedName);
        dataOffset += chunkSize;
        blockOffset = tagEnd + 1;
    }
    if (dataOffset != bytes.size())
        return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map has too few blocks for " + std::string(wantedName));
    return Status::Ok();
}

Status VerifyPublisher(const CERT_CONTEXT *certificate, const std::string &appxManifest)
{
    const std::size_t identityOffset = appxManifest.find("<Identity ");
    if (identityOffset == std::string::npos)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppxManifest.xml does not contain an Identity element");
    const std::size_t identityEnd = appxManifest.find('>', identityOffset);
    if (identityEnd == std::string::npos)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppxManifest.xml has a truncated Identity element");
    const std::string identity = appxManifest.substr(identityOffset, identityEnd - identityOffset + 1);
    std::string name;
    std::string publisher;
    const bool hasName = Attribute(identity, "Name", name);
    const bool hasPublisher = Attribute(identity, "Publisher", publisher);
    if (!hasName || name != ExpectedPackageName || !hasPublisher || publisher != ExpectedPublisher)
        return Status::Fail(ERROR_ACCESS_DENIED, "source package identity is not Microsoft.Winget.Source");

    CERT_NAME_INFO *rawSubject = nullptr;
    DWORD subjectSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_NAME, certificate->pCertInfo->Subject.pbData,
                             certificate->pCertInfo->Subject.cbData, CRYPT_DECODE_ALLOC_FLAG,
                             nullptr, &rawSubject, &subjectSize))
        return Status::Fail(GetLastError(), "cannot decode source signature certificate subject");
    LocalMemoryOwner subject(rawSubject);
    const auto attributeEquals = [rawSubject](const char *objectId, const wchar_t *expected) {
        const CERT_RDN_ATTR *attribute = CertFindRDNAttr(objectId, rawSubject);
        if (!attribute) return false;
        auto value = const_cast<PCERT_RDN_VALUE_BLOB>(&attribute->Value);
        const DWORD required = CertRDNValueToStrW(attribute->dwValueType, value, nullptr, 0);
        if (!required) return false;
        std::vector<wchar_t> text(required);
        return CertRDNValueToStrW(attribute->dwValueType, value, text.data(), required) == required &&
               _wcsicmp(text.data(), expected) == 0;
    };
    if (!attributeEquals(szOID_COMMON_NAME, L"Microsoft Corporation") ||
        !attributeEquals(szOID_ORGANIZATION_NAME, L"Microsoft Corporation") ||
        !attributeEquals(szOID_LOCALITY_NAME, L"Redmond") ||
        !attributeEquals(szOID_STATE_OR_PROVINCE_NAME, L"Washington") ||
        !attributeEquals(szOID_COUNTRY_NAME, L"US"))
        return Status::Fail(ERROR_ACCESS_DENIED, "source signature certificate does not match the MSIX publisher");
    return Status::Ok();
}

} // namespace

Status VerifySourcePackageTrust(const SourcePackageContents &contents)
{
    if (contents.signature.size() < 5 || contents.blockMap.empty() || contents.manifest.empty() || contents.index.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "source MSIX is missing signed package content");
    DWORD magic = 0;
    std::memcpy(&magic, contents.signature.data(), sizeof(magic));
    if (magic != P7xMagic)
        return Status::Fail(ERROR_BAD_FORMAT, "AppxSignature.p7x has an invalid PKCX header");

    CRYPT_DATA_BLOB blob{static_cast<DWORD>(contents.signature.size() - 4), const_cast<BYTE *>(contents.signature.data() + 4)};
    DWORD encoding = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    HCERTSTORE rawStore = nullptr;
    HCRYPTMSG rawMessage = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob,
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED | CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &contentType, &formatType,
                          &rawStore, &rawMessage, nullptr))
        return Status::Fail(GetLastError(), "cannot decode the source AppX signature");
    StoreOwner store(rawStore);
    MessageOwner message(rawMessage);

    DWORD signerSize = 0;
    if (!CryptMsgGetParam(rawMessage, CMSG_SIGNER_CERT_INFO_PARAM, 0, nullptr, &signerSize) || !signerSize)
        return Status::Fail(GetLastError(), "source AppX signature has no signer");
    std::vector<uint8_t> signerBytes(signerSize);
    if (!CryptMsgGetParam(rawMessage, CMSG_SIGNER_CERT_INFO_PARAM, 0, signerBytes.data(), &signerSize))
        return Status::Fail(GetLastError(), "cannot read source AppX signer information");
    const auto signer = reinterpret_cast<PCERT_INFO>(signerBytes.data());
    CertificateOwner certificate(
        CertFindCertificateInStore(rawStore, encoding, 0, CERT_FIND_SUBJECT_CERT, signer, nullptr));
    if (!certificate.Get())
        return Status::Fail(GetLastError(), "source AppX signer certificate is missing");

    CMSG_CTRL_VERIFY_SIGNATURE_EX_PARA signatureParameters{};
    signatureParameters.cbSize = sizeof(signatureParameters);
    signatureParameters.dwSignerType = CMSG_VERIFY_SIGNER_CERT;
    signatureParameters.pvSigner = const_cast<PCERT_CONTEXT>(certificate.Get());
    if (!CryptMsgControl(rawMessage, 0, CMSG_CTRL_VERIFY_SIGNATURE_EX, &signatureParameters))
        return Status::Fail(GetLastError(), "source AppX cryptographic signature is invalid");

    CERT_CHAIN_PARA chainParameters{};
    chainParameters.cbSize = sizeof(chainParameters);
    PCCERT_CHAIN_CONTEXT rawChain = nullptr;
    if (!CertGetCertificateChain(nullptr, certificate.Get(), nullptr, rawStore, &chainParameters,
                                 CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &rawChain))
        return Status::Fail(GetLastError(), "cannot build the source publisher certificate chain");
    ChainOwner chain(rawChain);
    CERT_CHAIN_POLICY_PARA policyParameters{};
    policyParameters.cbSize = sizeof(policyParameters);
    CERT_CHAIN_POLICY_STATUS policyStatus{};
    policyStatus.cbSize = sizeof(policyStatus);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE, rawChain, &policyParameters, &policyStatus))
        return Status::Fail(GetLastError(), "cannot validate the source Authenticode certificate policy");
    if (rawChain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR || policyStatus.dwError != ERROR_SUCCESS)
        return Status::Fail(policyStatus.dwError ? policyStatus.dwError : ERROR_ACCESS_DENIED,
                            "source publisher certificate chain is not trusted");

    Status status = VerifyPublisher(certificate.Get(), std::string(reinterpret_cast<const char *>(contents.manifest.data()), contents.manifest.size()));
    if (!status) return status;

    DWORD contentSize = 0;
    if (!CryptMsgGetParam(rawMessage, CMSG_CONTENT_PARAM, 0, nullptr, &contentSize) || !contentSize)
        return Status::Fail(GetLastError(), "source AppX signature has no indirect digest");
    std::vector<uint8_t> content(contentSize);
    if (!CryptMsgGetParam(rawMessage, CMSG_CONTENT_PARAM, 0, content.data(), &contentSize))
        return Status::Fail(GetLastError(), "cannot read source AppX indirect digest");
    SIP_INDIRECT_DATA *rawIndirect = nullptr;
    DWORD indirectSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, SpcIndirectDataObjectId, content.data(), contentSize,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &rawIndirect, &indirectSize))
        return Status::Fail(GetLastError(), "cannot decode source AppX indirect digest");
    LocalMemoryOwner indirect(rawIndirect);
    if (!rawIndirect->DigestAlgorithm.pszObjId || std::strcmp(rawIndirect->DigestAlgorithm.pszObjId, szOID_NIST_sha256) != 0 ||
        rawIndirect->Digest.cbData < 4 || std::memcmp(rawIndirect->Digest.pbData, "APPX", 4) != 0)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppX signature does not use the expected SHA-256 APPX digest");

    const uint8_t *blockMapDigest = nullptr;
    for (DWORD offset = 4; offset + 36 <= rawIndirect->Digest.cbData; offset += 36)
    {
        if (std::memcmp(rawIndirect->Digest.pbData + offset, "AXBM", 4) == 0)
            blockMapDigest = rawIndirect->Digest.pbData + offset + 4;
    }
    if (!blockMapDigest)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppX signature does not contain an AXBM digest");
    uint8_t actualBlockMapDigest[32]{};
    status = HashBytesSha256(contents.blockMap.data(), contents.blockMap.size(), actualBlockMapDigest);
    if (!status) return status;
    if (!std::equal(actualBlockMapDigest, actualBlockMapDigest + sizeof(actualBlockMapDigest), blockMapDigest))
        return Status::Fail(ERROR_CRC, "source AppxBlockMap.xml does not match the signed AXBM digest");

    const std::string blockMap(reinterpret_cast<const char *>(contents.blockMap.data()), contents.blockMap.size());
    if (blockMap.find("HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"") == std::string::npos)
        return Status::Fail(ERROR_NOT_SUPPORTED, "source MSIX block map does not use SHA-256");
    status = VerifyBlockMapFile(blockMap, "Public\\index.db", contents.index);
    if (!status) return status;
    return VerifyBlockMapFile(blockMap, "AppxManifest.xml", contents.manifest);
}

} // namespace lntget
