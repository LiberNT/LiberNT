/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loopback HTTP proxy serving the pages the mshtml http tests load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <string.h>

#include "httpserver.h"

struct served_file
{
    const char *path;
    const char *type;
    const char *data;
    int size;
};

static const char hello_html[] =
    "<html><body>\n"
    "<h1>Hello world!</h1>\n"
    "This page is served by the test itself.\n"
    "</body></html>\n";

static const char snapshot_html[] =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<title>Local test page</title>\n"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n"
    "<link rel=\"stylesheet\" href=\"index_files/styles.css\" type=\"text/css\" media=\"screen, print\">\n"
    "<script src=\"index_files/jquery.js\" type=\"text/javascript\"></script>\n"
    "<script src=\"index_files/utils.js\" type=\"text/javascript\"></script>\n"
    "</head>\n"
    "<body>\n"
    "<div id=\"logo\"><img src=\"index_files/winehq_logo_glass.png\" alt=\"\"></div>\n"
    "<a name=\"test\"></a>\n"
    "<p>This page is served by the test itself.</p>\n"
    "<script type=\"text/javascript\" src=\"index_files/brand\"></script>\n"
    "</body></html>\n";

static const char styles_css[] = "body { margin: 0; }\n";
static const char script_js[] = "var local_test_page = 1;\n";
static const char winetest_file[] = "A file of a type with no registered handler.\n";

static const unsigned char logo_png[] =
{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00,
    0x00, 0x05, 0x00, 0x01, 0x7a, 0x5e, 0xab, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82
};

static const struct served_file served_files[] =
{
    { "/tests/hello.html", "text/html", hello_html, sizeof(hello_html) - 1 },
    { "/tests/winehq_snapshot/", "text/html", snapshot_html, sizeof(snapshot_html) - 1 },
    { "/tests/winehq_snapshot/index_files/styles.css", "text/css", styles_css, sizeof(styles_css) - 1 },
    { "/tests/winehq_snapshot/index_files/jquery.js", "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/utils.js", "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/brand", "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/winehq_logo_glass.png", "image/png", (const char *)logo_png, sizeof(logo_png) },
    { "/tests/file.winetest", "application/winetest", winetest_file, sizeof(winetest_file) - 1 },
};

static const char served_host[] = "http://test.winehq.org";

static SOCKET listen_socket = INVALID_SOCKET;
static HANDLE server_thread;

static const struct served_file *find_served_file(const char *uri)
{
    char path[512];
    size_t len;
    unsigned int i;

    if (!_strnicmp(uri, served_host, sizeof(served_host) - 1))
        uri += sizeof(served_host) - 1;
    else if (uri[0] != '/')
        return NULL;

    len = strcspn(uri, "?# ");
    if (len >= sizeof(path))
        return NULL;
    memcpy(path, uri, len);
    path[len] = 0;

    for (i = 0; i < ARRAYSIZE(served_files); i++)
    {
        if (!strcmp(path, served_files[i].path))
            return &served_files[i];
    }
    return NULL;
}

static void send_all(SOCKET s, const char *data, int size)
{
    int sent;

    while (size > 0)
    {
        sent = send(s, data, size, 0);
        if (sent <= 0)
            return;
        data += sent;
        size -= sent;
    }
}

static void serve_connection(SOCKET s)
{
    const struct served_file *file;
    char request[4096], header[512], method[16];
    int got = 0, ret, len;
    char *uri, *end;

    while (got < (int)sizeof(request) - 1)
    {
        ret = recv(s, request + got, sizeof(request) - 1 - got, 0);
        if (ret <= 0)
            return;
        got += ret;
        request[got] = 0;
        if (strstr(request, "\r\n\r\n"))
            break;
    }

    end = strchr(request, ' ');
    if (!end || end - request >= (int)sizeof(method))
        return;
    memcpy(method, request, end - request);
    method[end - request] = 0;
    uri = end + 1;

    file = (!strcmp(method, "GET") || !strcmp(method, "HEAD")) ? find_served_file(uri) : NULL;
    if (!file)
    {
        static const char not_found[] =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(s, not_found, sizeof(not_found) - 1);
        return;
    }

    len = sprintf(header,
                  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                  file->type,
                  file->size);
    send_all(s, header, len);
    if (strcmp(method, "HEAD"))
        send_all(s, file->data, file->size);
}

static DWORD WINAPI server_proc(void *arg)
{
    SOCKET client;
    DWORD timeout = 5000;

    for (;;)
    {
        client = accept(listen_socket, NULL, NULL);
        if (client == INVALID_SOCKET)
            return 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        serve_connection(client);
        shutdown(client, SD_SEND);
        closesocket(client);
    }
}

BOOL start_local_http_server(void)
{
    INTERNET_PROXY_INFOW proxy_info;
    struct sockaddr_in addr;
    WCHAR proxy[64];
    WSADATA wsa;
    int len;

    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return FALSE;

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET)
        return FALSE;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    len = sizeof(addr);
    if (bind(listen_socket, (struct sockaddr *)&addr, sizeof(addr)) ||
        listen(listen_socket, SOMAXCONN) ||
        getsockname(listen_socket, (struct sockaddr *)&addr, &len))
    {
        closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
        return FALSE;
    }

    server_thread = CreateThread(NULL, 0, server_proc, NULL, 0, NULL);
    if (!server_thread)
    {
        closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
        return FALSE;
    }

    /* Every http request of this process goes through the local server, with its URL unchanged */
    swprintf(proxy, ARRAYSIZE(proxy), L"127.0.0.1:%u", ntohs(addr.sin_port));
    proxy_info.dwAccessType = INTERNET_OPEN_TYPE_PROXY;
    proxy_info.lpszProxy = proxy;
    proxy_info.lpszProxyBypass = L"";
    return InternetSetOptionW(NULL, INTERNET_OPTION_PROXY, &proxy_info, sizeof(proxy_info));
}

void stop_local_http_server(void)
{
    if (listen_socket == INVALID_SOCKET)
        return;

    closesocket(listen_socket);
    listen_socket = INVALID_SOCKET;
    WaitForSingleObject(server_thread, 5000);
    CloseHandle(server_thread);
    server_thread = NULL;
    WSACleanup();
}
