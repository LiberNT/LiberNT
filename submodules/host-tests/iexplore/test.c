/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Web browser address, character set, title and generated page helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Failures;

#define CHECK(Condition, ...)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(Condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__);                                                            \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fprintf(stderr, "\n");                                                                                     \
            Failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void
Normalize(const char *Input, const char *Expected)
{
    char Url[256];
    int Ok = BrowserNormalizeUrl(Input, Url, sizeof(Url));

    if (Expected)
        CHECK(Ok && !strcmp(Url, Expected), "\"%s\" -> \"%s\" (%d), expected \"%s\"", Input, Url, Ok, Expected);
    else
        CHECK(!Ok, "\"%s\" should be refused, got \"%s\"", Input, Url);
}

static void
TestNormalize(void)
{
    char Small[12];

    Normalize("example.com", "http://example.com/");
    Normalize("  https://a.example/path?q=1  ", "https://a.example/path?q=1");
    Normalize("example.com?q=1", "http://example.com/?q=1");
    Normalize("http://example.com#top", "http://example.com/#top");
    Normalize("HTTP://X.ORG", "HTTP://X.ORG/");
    Normalize("localhost:8080", "http://localhost:8080/");
    Normalize("localhost:8080/admin", "http://localhost:8080/admin");
    Normalize("10.0.2.100/login", "http://10.0.2.100/login");
    Normalize("about:blank", "about:blank");
    Normalize("ABOUT:BLANK", "about:blank");
    Normalize("", NULL);
    Normalize("   ", NULL);
    Normalize("two words", NULL);
    Normalize("mailto:guest@example.com", NULL);
    Normalize("file:///C:/Windows/win.ini", NULL);
    Normalize("javascript:alert(1)", NULL);
    Normalize("C:\\Windows", NULL);
    Normalize("http://", NULL);
    Normalize("https:///path", NULL);
    Normalize("/relative", NULL);
    CHECK(!BrowserNormalizeUrl("example.com/long", Small, sizeof(Small)), "too long for the buffer");
}

static void
TestContent(void)
{
    CHECK(BrowserContentKind("text/html; charset=utf-8") == BrowserContentHtml, "html with parameters");
    CHECK(BrowserContentKind("  Application/XHTML+XML ") == BrowserContentHtml, "xhtml");
    CHECK(BrowserContentKind("") == BrowserContentHtml && BrowserContentKind(NULL) == BrowserContentHtml,
          "no type is shown as a page");
    CHECK(BrowserContentKind("TEXT/PLAIN") == BrowserContentText, "plain text");
    CHECK(BrowserContentKind("application/json") == BrowserContentText, "json");
    CHECK(BrowserContentKind("image/png") == BrowserContentImage, "png");
    CHECK(BrowserContentKind("image/svg+xml") == BrowserContentImage, "svg");
    CHECK(BrowserContentKind("image/webp") == BrowserContentDownload, "webp is not decoded, so it is saved");
    CHECK(BrowserContentKind("application/zip") == BrowserContentDownload, "zip");
    CHECK(BrowserContentKind("application/octet-stream") == BrowserContentDownload, "binary");
}

static void
TestCharset(void)
{
    static const char Meta[] = "<html><head><meta charset=\"windows-1252\"><title>x</title>";
    static const char HttpEquiv[] = "<head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=shift_jis\">";
    static const char Bom[] = "\xEF\xBB\xBF<html>";
    char Charset[32];

    CHECK(BrowserCharset("text/html; charset=ISO-8859-1", Meta, sizeof(Meta) - 1, Charset, sizeof(Charset)) &&
              !strcmp(Charset, "ISO-8859-1"),
          "the header wins: %s", Charset);
    CHECK(BrowserCharset("text/html; charset=\"utf-8\"", NULL, 0, Charset, sizeof(Charset)) && !strcmp(Charset, "utf-8"),
          "quoted header value: %s", Charset);
    CHECK(BrowserCharset("text/html", Meta, sizeof(Meta) - 1, Charset, sizeof(Charset)) &&
              !strcmp(Charset, "windows-1252"),
          "meta charset: %s", Charset);
    CHECK(BrowserCharset(NULL, HttpEquiv, sizeof(HttpEquiv) - 1, Charset, sizeof(Charset)) &&
              !strcmp(Charset, "shift_jis"),
          "meta http-equiv: %s", Charset);
    CHECK(BrowserCharset(NULL, Bom, sizeof(Bom) - 1, Charset, sizeof(Charset)) && !strcmp(Charset, "utf-8"),
          "byte order mark: %s", Charset);
    CHECK(!BrowserCharset("text/html", "<p>none</p>", 11, Charset, sizeof(Charset)) && !Charset[0], "no charset");

    CHECK(BrowserCodePage("ISO-8859-1") == 1252, "latin 1 is decoded as windows-1252, like browsers");
    CHECK(BrowserCodePage("us-ascii") == 1252, "ascii");
    CHECK(BrowserCodePage("windows-1251") == 1251, "cyrillic");
    CHECK(BrowserCodePage("iso-8859-2") == 28592, "latin 2");
    CHECK(BrowserCodePage("ISO-8859-15") == 28605, "latin 9");
    CHECK(BrowserCodePage("Shift_JIS") == 932, "shift jis");
    CHECK(BrowserCodePage("gb18030") == 54936, "gb18030");
    CHECK(BrowserCodePage("utf-8") == 0 && BrowserCodePage("UTF8") == 0, "utf-8 needs no conversion");
    CHECK(BrowserCodePage("x-unknown") == 0 && BrowserCodePage(NULL) == 0, "unknown names are read as utf-8");
    CHECK(BrowserCodePage("windows-1259") == 0 && BrowserCodePage("iso-8859-1x") == 0, "near misses");
}

static void
TestTitle(void)
{
    static const char Page[] = "<html><head><TITLE lang=\"en\">\n  Hotel &amp; Spa\t&lt;Guest&gt;  </title></head>";
    static const char Numeric[] = "<title>Caf&#233; &#x1F600;</title>";
    static const char Wide[] = "<title>\xC3\xA9\xC3\xA9\xC3\xA9</title>";
    char Title[64], Small[4];

    CHECK(BrowserTitle(Page, sizeof(Page) - 1, Title, sizeof(Title)) && !strcmp(Title, "Hotel & Spa <Guest>"),
          "entities and spaces: \"%s\"", Title);
    CHECK(BrowserTitle(Numeric, sizeof(Numeric) - 1, Title, sizeof(Title)) &&
              !strcmp(Title, "Caf\xC3\xA9 \xF0\x9F\x98\x80"),
          "numeric references: \"%s\"", Title);
    CHECK(BrowserTitle(Wide, sizeof(Wide) - 1, Small, sizeof(Small)) == 2 && !strcmp(Small, "\xC3\xA9"),
          "truncation keeps whole characters: %zu", strlen(Small));
    CHECK(!BrowserTitle("<p>no title</p>", 15, Title, sizeof(Title)) && !Title[0], "no title");
    CHECK(!BrowserTitle("<title>open", 11, Title, sizeof(Title)), "unterminated title");
}

static void
TestFileName(void)
{
    char Name[64];

    BrowserFileName("attachment; filename=\"report 1.pdf\"", "https://x.example/get?id=1", Name, sizeof(Name));
    CHECK(!strcmp(Name, "report 1.pdf"), "quoted name: %s", Name);
    BrowserFileName("attachment; filename*=UTF-8''r%C3%A9sum%C3%A9.pdf", NULL, Name, sizeof(Name));
    CHECK(!strcmp(Name, "r\xC3\xA9sum\xC3\xA9.pdf"), "extended name: %s", Name);
    BrowserFileName(NULL, "https://x.example/dl/setup%20v2.exe?token=1#part", Name, sizeof(Name));
    CHECK(!strcmp(Name, "setup v2.exe"), "name from the address: %s", Name);
    BrowserFileName("", "https://x.example/", Name, sizeof(Name));
    CHECK(!strcmp(Name, "download"), "no name: %s", Name);
    BrowserFileName("attachment; filename=\"../../evil:name.exe\"", NULL, Name, sizeof(Name));
    CHECK(!strcmp(Name, ".._.._evil_name.exe"), "path characters are replaced: %s", Name);
    BrowserFileName("attachment; filename=notes.txt. ", NULL, Name, sizeof(Name));
    CHECK(!strcmp(Name, "notes.txt"), "trailing dots: %s", Name);
}

static void
TestPages(void)
{
    size_t Length = 0;
    char *Page = BrowserTextPage("<b>&\"", 5, &Length);

    CHECK(Page && strstr(Page, "&lt;b&gt;&amp;&quot;") && Length == strlen(Page), "text is escaped");
    free(Page);
    Page = BrowserImagePage("https://x.example/a\"b.png", &Length);
    CHECK(Page && strstr(Page, "src=\"https://x.example/a&quot;b.png\"") && Length == strlen(Page), "image address");
    free(Page);
    Page = BrowserMessagePage("Could not open", "http://x/<script>", &Length);
    CHECK(Page && strstr(Page, ">Could not open<") && strstr(Page, "http://x/&lt;script&gt;") && Length == strlen(Page),
          "message page");
    free(Page);
}

int
main(void)
{
    TestNormalize();
    TestContent();
    TestCharset();
    TestTitle();
    TestFileName();
    TestPages();
    printf("iexplore.browser: %d failures\n", Failures);
    return Failures ? 1 : 0;
}
