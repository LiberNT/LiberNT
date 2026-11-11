/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loopback HTTP proxy serving the test.winehq.org pages the http tests load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "httpserver.h"

struct http_request
{
    char method[16];
    char path[512];
    char content_type[128];
    char referer[512];
    const char *body;
    int body_size;
};

typedef char *(*build_page)(const struct http_request *request, int *size);

struct served_page
{
    const char *path;
    const char *status;
    const char *headers;
    const char *type;
    const char *data;
    int size;
    build_page build;
};

static const char hello_html[] =
    "<html><body>\n"
    "<h1>Hello world!</h1>\n"
    "This is a test page for the Wine regression test suite.\n"
    "</body></html>\n";

static const char snapshot_html[] =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<title>WineHQ - Run Windows applications on Linux, BSD, Solaris and Mac OS X</title>\n"
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
static const char cors_html[] = "<html><body>test</body></html>\n";
static const char xmltest_xml[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<a>TEST</a>\n";
static const char moved_html[] =
    "<html><head><title>301 Moved Permanently</title></head><body>\n"
    "<h1>Moved Permanently</h1>\n"
    "<p>The document has moved <a href=\"http://test.winehq.org/tests/hello.html\">here</a>.</p>\n"
    "</body></html>\n";

static const unsigned char logo_png[] =
{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00,
    0x00, 0x05, 0x00, 0x01, 0x7a, 0x5e, 0xab, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82
};

static const char static_headers[] =
    "Last-Modified: Tue, 14 Jun 2022 15:45:18 GMT\r\n"
    "Accept-Ranges: bytes\r\n";

static const char cors_headers[] =
    "Last-Modified: Tue, 14 Jun 2022 15:45:18 GMT\r\n"
    "Accept-Ranges: bytes\r\n"
    "Access-Control-Allow-Origin: *\r\n";

static const char script_headers[] =
    "Cache-Control: max-age=0, no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Expires: Wed, 11 Jan 1984 05:00:00 GMT\r\n";

static char *copy_page(const char *text, int *size)
{
    char *page;

    *size = (int)strlen(text);
    page = malloc(*size + 1);
    if (page)
        memcpy(page, text, *size + 1);
    return page;
}

static char *build_data_page(const struct http_request *request, int *size)
{
    static const char head[] =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
        "<html>\n"
        "<head>\n"
        "    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
        "    <title>Page for winetest tests purposes</title>\n"
        "</head>\n"
        "<body>\n";
    static const char tail[] = "</body>\n</html>\n";
    static const char word[] = "test ";
    char *page, *p;
    int i;

    *size = (int)(sizeof(head) - 1 + 10000 * (sizeof(word) - 1) + sizeof(tail) - 1);
    page = malloc(*size);
    if (!page)
        return NULL;
    p = page;
    memcpy(p, head, sizeof(head) - 1);
    p += sizeof(head) - 1;
    for (i = 0; i < 10000; i++, p += sizeof(word) - 1)
        memcpy(p, word, sizeof(word) - 1);
    memcpy(p, tail, sizeof(tail) - 1);
    return page;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *url_decode(const char *text, int size, char *out)
{
    int i;

    for (i = 0; i < size; i++)
    {
        if (text[i] == '+')
            *out++ = ' ';
        else if (text[i] == '%' && i + 2 < size && hex_digit(text[i + 1]) >= 0 && hex_digit(text[i + 2]) >= 0)
        {
            *out++ = (char)(hex_digit(text[i + 1]) * 16 + hex_digit(text[i + 2]));
            i += 2;
        }
        else
            *out++ = text[i];
    }
    return out;
}

static char *build_post_page(const struct http_request *request, int *size)
{
    static const char form_type[] = "application/x-www-form-urlencoded";
    const char *field = request->body, *end = request->body + request->body_size, *next, *equal;
    char *page, *p;

    if (_strnicmp(request->content_type, form_type, sizeof(form_type) - 1) || !request->body_size)
        return copy_page("FAILED", size);

    page = malloc(request->body_size * 2 + 16);
    if (!page)
        return NULL;
    p = page;
    while (field < end)
    {
        next = memchr(field, '&', end - field);
        if (!next)
            next = end;
        equal = memchr(field, '=', next - field);
        if (!equal)
            equal = next;
        p = url_decode(field, (int)(equal - field), p);
        memcpy(p, " => ", 4);
        p += 4;
        if (equal < next)
            p = url_decode(equal + 1, (int)(next - equal - 1), p);
        *p++ = '\n';
        field = next + 1;
    }
    *size = (int)(p - page);
    return page;
}

static char *build_referer_page(const struct http_request *request, int *size)
{
    return copy_page(request->referer[0] ? request->referer : "no referer set", size);
}

static const struct served_page served_pages[] =
{
    { "/tests/hello.html", "200 OK", static_headers, "text/html", hello_html, sizeof(hello_html) - 1 },
    { "/tests/winehq_snapshot/", "200 OK", static_headers, "text/html", snapshot_html, sizeof(snapshot_html) - 1 },
    { "/tests/winehq_snapshot/index_files/styles.css", "200 OK", static_headers, "text/css", styles_css, sizeof(styles_css) - 1 },
    { "/tests/winehq_snapshot/index_files/jquery.js", "200 OK", static_headers, "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/utils.js", "200 OK", static_headers, "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/brand", "200 OK", static_headers, "application/javascript", script_js, sizeof(script_js) - 1 },
    { "/tests/winehq_snapshot/index_files/winehq_logo_glass.png", "200 OK", static_headers, "image/png", (const char *)logo_png, sizeof(logo_png) },
    { "/tests/winehq_snapshot/index_files/winehq_logo_text.png", "200 OK", static_headers, "image/png", (const char *)logo_png, sizeof(logo_png) },
    { "/tests/file.winetest", "200 OK", static_headers, "application/winetest", winetest_file, sizeof(winetest_file) - 1 },
    { "/tests/cors.html", "200 OK", cors_headers, "text/plain", cors_html, sizeof(cors_html) - 1 },
    { "/tests/xmltest.xml", "200 OK", static_headers, "application/xml", xmltest_xml, sizeof(xmltest_xml) - 1 },
    { "/tests/empty.js", "200 OK", static_headers, "text/javascript", "", 0 },
    { "/tests/redirect", "301 Moved Permanently", "Location: http://test.winehq.org/tests/hello.html\r\n",
      "text/html; charset=iso-8859-1", moved_html, sizeof(moved_html) - 1 },
    { "/tests/data.php", "200 OK", script_headers, "text/html; charset=UTF-8", NULL, 0, build_data_page },
    { "/tests/post.php", "200 OK", script_headers, "text/plain;charset=UTF-8", NULL, 0, build_post_page },
    { "/tests/referer.php", "200 OK", script_headers, "text/html; charset=UTF-8", NULL, 0, build_referer_page },
};

static const char served_host[] = "http://test.winehq.org";

static SOCKET listen_socket = INVALID_SOCKET;
static HANDLE server_thread;

static const struct served_page *find_served_page(const char *path)
{
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(served_pages); i++)
    {
        if (!strcmp(path, served_pages[i].path))
            return &served_pages[i];
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

static void copy_header(const char *headers, const char *name, char *value, size_t size)
{
    size_t name_length = strlen(name), length;
    const char *line = headers;

    value[0] = 0;
    while (line && *line && strncmp(line, "\r\n", 2))
    {
        if (!_strnicmp(line, name, name_length) && line[name_length] == ':')
        {
            line += name_length + 1;
            while (*line == ' ')
                line++;
            length = strcspn(line, "\r\n");
            if (length >= size)
                length = size - 1;
            memcpy(value, line, length);
            value[length] = 0;
            return;
        }
        line = strstr(line, "\r\n");
        if (line)
            line += 2;
    }
}

static BOOL read_request(SOCKET s, char *buffer, int size, struct http_request *request)
{
    char length_text[16], *end, *uri;
    int got = 0, ret, length, header_size;
    size_t path_length;

    for (;;)
    {
        if (got >= size - 1)
            return FALSE;
        ret = recv(s, buffer + got, size - 1 - got, 0);
        if (ret <= 0)
            return FALSE;
        got += ret;
        buffer[got] = 0;
        end = strstr(buffer, "\r\n\r\n");
        if (end)
            break;
    }
    header_size = (int)(end + 4 - buffer);

    copy_header(strstr(buffer, "\r\n") + 2, "Content-Type", request->content_type, sizeof(request->content_type));
    copy_header(strstr(buffer, "\r\n") + 2, "Referer", request->referer, sizeof(request->referer));
    copy_header(strstr(buffer, "\r\n") + 2, "Content-Length", length_text, sizeof(length_text));
    length = atoi(length_text);
    if (length < 0 || length > size - 1 - header_size)
        return FALSE;
    while (got < header_size + length)
    {
        ret = recv(s, buffer + got, size - 1 - got, 0);
        if (ret <= 0)
            return FALSE;
        got += ret;
    }
    request->body = buffer + header_size;
    request->body_size = length;

    uri = strchr(buffer, ' ');
    if (!uri || uri - buffer >= (int)sizeof(request->method))
        return FALSE;
    memcpy(request->method, buffer, uri - buffer);
    request->method[uri - buffer] = 0;
    uri++;
    if (!_strnicmp(uri, served_host, sizeof(served_host) - 1))
        uri += sizeof(served_host) - 1;
    path_length = strcspn(uri, "?# \r\n");
    if (uri[0] != '/' || path_length >= sizeof(request->path))
        return FALSE;
    memcpy(request->path, uri, path_length);
    request->path[path_length] = 0;
    return TRUE;
}

static void serve_connection(SOCKET s)
{
    static const char *const day_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const month_names[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char not_found[] =
        "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    struct http_request request;
    const struct served_page *page = NULL;
    char buffer[8192], header[1024], *built = NULL;
    const char *data;
    SYSTEMTIME now;
    int size, length;

    memset(&request, 0, sizeof(request));
    if (!read_request(s, buffer, sizeof(buffer), &request))
        return;

    if (!strcmp(request.method, "GET") || !strcmp(request.method, "HEAD") || !strcmp(request.method, "POST"))
        page = find_served_page(request.path);
    if (!page)
    {
        send_all(s, not_found, sizeof(not_found) - 1);
        return;
    }

    if (page->build)
    {
        built = page->build(&request, &size);
        if (!built)
            return;
        data = built;
    }
    else
    {
        data = page->data;
        size = page->size;
    }

    GetSystemTime(&now);
    length = sprintf(header,
                     "HTTP/1.1 %s\r\nDate: %s, %02u %s %04u %02u:%02u:%02u GMT\r\n%sContent-Type: %s\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n\r\n",
                     page->status,
                     day_names[now.wDayOfWeek], now.wDay, month_names[now.wMonth - 1], now.wYear,
                     now.wHour, now.wMinute, now.wSecond,
                     page->headers,
                     page->type,
                     size);
    send_all(s, header, length);
    if (strcmp(request.method, "HEAD"))
        send_all(s, data, size);
    free(built);
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

BOOL start_local_http_server(const WCHAR *bypass)
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
    proxy_info.lpszProxyBypass = bypass ? bypass : L"";
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
