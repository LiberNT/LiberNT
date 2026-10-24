/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HTTPS client used by lntget
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

struct HttpResponse
{
    DWORD statusCode = 0;
    std::vector<uint8_t> body;
};

class DownloadProgressCallback
{
public:
    virtual void Progress(unsigned long long received, unsigned long long total) = 0;

protected:
    ~DownloadProgressCallback() {}
};

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    Status Get(const std::wstring &url, const std::wstring &headers, HttpResponse &response) const;
    Status ProbeContentLength(const std::wstring &url, unsigned long long &length,
                              const std::wstring &headers = std::wstring()) const;
    Status Download(const std::wstring &url, const std::wstring &destination, const std::wstring &headers = std::wstring(),
                    unsigned long long maximumBytes = ~0ull, DownloadProgressCallback *progress = nullptr) const;

private:
    void *session_ = nullptr;
};

} // namespace lntget
