/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Captive portal sign-in page reader and form encoder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html.h"

static int Failures;

#define CHECK(Condition, ...) \
    do \
    { \
        if (!(Condition)) \
        { \
            printf("FAIL line %d: ", __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            Failures++; \
        } \
    } while (0)

static int
Same(const char *Left, const char *Right)
{
    if (!Left || !Right)
        return Left == Right;
    return strcmp(Left, Right) == 0;
}

static void
Parse(const char *Html, HTML_DOCUMENT *Document)
{
    int Ok = HtmlParse(Html, strlen(Html), Document);

    CHECK(Ok, "HtmlParse failed");
}

static const HTML_ITEM *
Item(const HTML_DOCUMENT *Document, size_t Index)
{
    static const HTML_ITEM Empty;

    if (Index >= Document->ItemCount)
    {
        CHECK(0, "item %zu missing, %zu items", Index, Document->ItemCount);
        return &Empty;
    }
    return &Document->Items[Index];
}

static size_t
Find(const HTML_DOCUMENT *Document, const char *Name, const char *Type)
{
    size_t Index;

    for (Index = 0; Index < Document->ItemCount; Index++)
    {
        const HTML_ITEM *Entry = &Document->Items[Index];

        if (Same(Entry->Name, Name) && (!Type || Same(Entry->Type, Type)))
            return Index;
    }
    CHECK(0, "no item named %s", Name);
    return (size_t)-1;
}

static void
CheckEncoding(const HTML_DOCUMENT *Document, size_t Form, size_t Submitter, const char *Boundary, const char *Expected)
{
    size_t Length = 0;
    char *Encoded = HtmlEncodeForm(Document, Form, Submitter, Boundary, &Length);

    CHECK(Encoded && Same(Encoded, Expected) && Length == strlen(Expected),
          "encoded \"%s\", expected \"%s\"", Encoded ? Encoded : "(null)", Expected);
    free(Encoded);
}

static void
TestClickThrough(void)
{
    static const char Html[] =
        "<!DOCTYPE html>\n<html><head><title> Hotel \n WiFi </title><meta charset=\"utf-8\">"
        "<style>p { color: red }</style></head>\n"
        "<body><!-- portal --><h1>Welcome &amp; enjoy</h1><p>Please accept the<br>terms of use.</p>\n"
        "<form action=\"/login\" method=\"POST\"><input type=\"hidden\" name=\"token\" value=\"a b&amp;c\">\n"
        "<label><input type=\"checkbox\" name=\"accept\" value=\"yes\"> I accept the <a href=\"/terms\">terms</a></label>\n"
        "<input type=\"submit\" name=\"go\" value=\"Connect\"></form></body></html>";
    HTML_DOCUMENT Document;
    size_t Accept, Go;

    Parse(Html, &Document);
    CHECK(Same(Document.Title, "Hotel WiFi"), "title \"%s\"", Document.Title);
    CHECK(Document.FormCount == 1, "%zu forms", Document.FormCount);
    CHECK(HtmlFormIsPost(&Document, 0) && !HtmlFormIsMultipart(&Document, 0), "form method");
    CHECK(Same(Document.Forms[0].Action, "/login"), "action \"%s\"", Document.Forms[0].Action);
    CHECK(!Document.HasScript && !Document.HasRefresh, "no script and no refresh expected");

    CHECK(Item(&Document, 0)->Kind == HtmlItemText && Item(&Document, 0)->Heading == 1 &&
          Same(Item(&Document, 0)->Text, "Welcome & enjoy"), "heading \"%s\"", Item(&Document, 0)->Text);
    CHECK(Same(Item(&Document, 1)->Text, "Please accept the"), "line 1 \"%s\"", Item(&Document, 1)->Text);
    CHECK(Same(Item(&Document, 2)->Text, "terms of use."), "line 2 \"%s\"", Item(&Document, 2)->Text);
    CHECK(Item(&Document, 3)->Kind == HtmlItemInput && Same(Item(&Document, 3)->Type, "hidden") &&
          Same(Item(&Document, 3)->Value, "a b&c") && !HtmlIsVisibleItem(Item(&Document, 3)), "hidden token");

    Accept = Find(&Document, "accept", "checkbox");
    Go = Find(&Document, "go", "submit");
    CHECK(Accept == 4 && Same(Document.Items[Accept].Text, "I accept the"),
          "checkbox caption \"%s\"", Accept < Document.ItemCount ? Document.Items[Accept].Text : "");
    CHECK(Item(&Document, 5)->Kind == HtmlItemLink && Same(Item(&Document, 5)->Href, "/terms") &&
          Same(Item(&Document, 5)->Text, "terms") && Item(&Document, 5)->Form == -1, "terms link");
    CHECK(Go == 6 && HtmlIsSubmitItem(&Document.Items[Go]) && Document.Items[Go].Form == 0, "submit button");
    CHECK(Document.ItemCount == 7, "%zu items", Document.ItemCount);

    CheckEncoding(&Document, 0, Go, NULL, "token=a+b%26c&go=Connect");
    Document.Items[Accept].Checked = 1;
    CheckEncoding(&Document, 0, Go, NULL, "token=a+b%26c&accept=yes&go=Connect");
    CheckEncoding(&Document, 0, (size_t)-1, NULL, "token=a+b%26c&accept=yes");
    HtmlFree(&Document);
}

static void
TestVoucher(void)
{
    static const char Html[] =
        "<form action=check.php><label for=code>Voucher</label><input id=code name=code placeholder=\"ABC-123\">"
        "<select name=plan><option value=1h>1 hour<option value=1d selected>1 day</select>"
        "<textarea name=note>\nline1\nline2</textarea><button>Go <b>now</b></button><button type=button>JS</button>"
        "<input type=reset><input type=radio name=r value=1 checked><input type=radio name=r value=2></form>";
    HTML_DOCUMENT Document;
    size_t Code, Plan, Note, Go, First, Second;

    Parse(Html, &Document);
    CHECK(!HtmlFormIsPost(&Document, 0), "GET is the default method");
    CHECK(Item(&Document, 0)->Kind == HtmlItemText && Same(Item(&Document, 0)->Text, "Voucher"), "label text");
    Code = Find(&Document, "code", "text");
    Plan = Find(&Document, "plan", NULL);
    Note = Find(&Document, "note", NULL);
    CHECK(Code == 1 && Same(Document.Items[Code].Placeholder, "ABC-123"), "code input");
    CHECK(Document.Items[Plan].Kind == HtmlItemSelect && Document.Items[Plan].OptionCount == 2 &&
          Same(Document.Items[Plan].Options[0].Text, "1 hour") && Same(Document.Items[Plan].Options[1].Value, "1d") &&
          Document.Items[Plan].Options[1].Selected && !Document.Items[Plan].Options[0].Selected, "select");
    CHECK(Document.Items[Note].Kind == HtmlItemTextArea && Same(Document.Items[Note].Value, "line1\nline2"),
          "textarea \"%s\"", Document.Items[Note].Value);

    Go = Note + 1;
    CHECK(Item(&Document, Go)->Kind == HtmlItemButton && Same(Item(&Document, Go)->Type, "submit") &&
          Same(Item(&Document, Go)->Text, "Go now") && HtmlIsSubmitItem(Item(&Document, Go)), "submit button");
    CHECK(Item(&Document, Go + 1)->Kind == HtmlItemButton && Same(Item(&Document, Go + 1)->Type, "button") &&
          !HtmlIsSubmitItem(Item(&Document, Go + 1)), "script button");
    CHECK(!HtmlIsVisibleItem(Item(&Document, Go + 2)), "reset is not shown");

    First = Go + 3;
    Second = Go + 4;
    CHECK(Same(Item(&Document, First)->Type, "radio") && Item(&Document, First)->Checked &&
          !Item(&Document, Second)->Checked, "radios");

    CHECK(HtmlSetValue(&Document.Items[Code], "X Y/1"), "set value");
    CheckEncoding(&Document, 0, Go, NULL, "code=X+Y%2F1&plan=1d&note=line1%0D%0Aline2&r=1");
    Document.Items[First].Checked = 0;
    Document.Items[Second].Checked = 1;
    Document.Items[Plan].Options[1].Selected = 0;
    CheckEncoding(&Document, 0, Go, NULL, "code=X+Y%2F1&plan=1h&note=line1%0D%0Aline2&r=2");
    HtmlFree(&Document);
}

static void
TestRefreshAndScript(void)
{
    HTML_DOCUMENT Document;

    Parse("<html><head><meta http-equiv=\"Refresh\" content=\"3; URL='http://10.0.0.1/login?x=1&amp;y=2'\">"
          "</head><body></body></html>", &Document);
    CHECK(Document.HasRefresh && Document.RefreshSeconds == 3 &&
          Same(Document.RefreshUrl, "http://10.0.0.1/login?x=1&y=2"), "refresh \"%s\"", Document.RefreshUrl);
    CHECK(Document.ItemCount == 0, "%zu items", Document.ItemCount);
    HtmlFree(&Document);

    Parse("<meta http-equiv=refresh content=0;url=/next>", &Document);
    CHECK(Document.HasRefresh && Document.RefreshSeconds == 0 && Same(Document.RefreshUrl, "/next"),
          "unquoted refresh \"%s\"", Document.RefreshUrl);
    HtmlFree(&Document);

    Parse("<html><body><script>if (a < b) { document.write('<p>x</p>'); }</script>"
          "<noscript>Enable JavaScript</noscript></body></html>", &Document);
    CHECK(Document.HasScript, "script seen");
    CHECK(Document.ItemCount == 1 && Same(Item(&Document, 0)->Text, "Enable JavaScript"), "noscript text");
    HtmlFree(&Document);
}

static void
TestText(void)
{
    HTML_DOCUMENT Document;

    Parse("<p>caf&#233; &#x20AC;5 &nbsp; a &lt; b &unknown; c < d</p><ul><li>one<li>two</ul>"
          "<p><a href=/x><img src=logo.png alt=\"Continue\"></a></p>", &Document);
    CHECK(Same(Item(&Document, 0)->Text, "caf\xC3\xA9 \xE2\x82\xAC" "5 a < b &unknown; c < d"),
          "text \"%s\"", Item(&Document, 0)->Text);
    CHECK(Same(Item(&Document, 1)->Text, "\xE2\x80\xA2 one") && Same(Item(&Document, 2)->Text, "\xE2\x80\xA2 two"),
          "list items");
    CHECK(Item(&Document, 3)->Kind == HtmlItemLink && Same(Item(&Document, 3)->Text, "Continue") &&
          Same(Item(&Document, 3)->Href, "/x"), "image link");
    HtmlFree(&Document);
}

static void
TestSubmitKinds(void)
{
    HTML_DOCUMENT Document;
    size_t Login;

    Parse("<form method=post enctype=\"multipart/form-data\" action=go><input name=a value=v>"
          "<textarea name=t>1\n2</textarea><input type=image name=login alt=\"Log in\"></form>"
          "<form><input type=image src=x.png></form>", &Document);
    CHECK(HtmlFormIsMultipart(&Document, 0), "multipart");
    Login = Find(&Document, "login", "image");
    CHECK(Same(Document.Items[Login].Text, "Log in") && HtmlIsSubmitItem(&Document.Items[Login]), "image submit");
    CheckEncoding(&Document, 0, Login, "B",
                  "--B\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\nv\r\n"
                  "--B\r\nContent-Disposition: form-data; name=\"t\"\r\n\r\n1\r\n2\r\n"
                  "--B\r\nContent-Disposition: form-data; name=\"login.x\"\r\n\r\n0\r\n"
                  "--B\r\nContent-Disposition: form-data; name=\"login.y\"\r\n\r\n0\r\n--B--\r\n");
    CheckEncoding(&Document, 0, Login, NULL, "a=v&t=1%0D%0A2&login.x=0&login.y=0");
    CheckEncoding(&Document, 1, Document.ItemCount - 1, NULL, "x=0&y=0");
    HtmlFree(&Document);

    Parse("<form><input name=on type=checkbox checked><input name=off disabled value=1>"
          "<input type=text value=nameless><input name=e></form>", &Document);
    CheckEncoding(&Document, 0, (size_t)-1, NULL, "on=on&e=");
    HtmlFree(&Document);
}

static void
TestCharset(void)
{
    static const char Meta[] = "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=ISO-8859-1\">";
    static const char Short[] = "<meta charset='Windows-1252'>";
    char *Charset;

    Charset = HtmlFindCharset(Meta, strlen(Meta));
    CHECK(Same(Charset, "iso-8859-1"), "charset \"%s\"", Charset);
    free(Charset);
    Charset = HtmlFindCharset(Short, strlen(Short));
    CHECK(Same(Charset, "windows-1252"), "charset \"%s\"", Charset);
    free(Charset);
    Charset = HtmlFindCharset("<p>none</p>", 11);
    CHECK(Charset == NULL, "no charset");
}

int
main(void)
{
    TestClickThrough();
    TestVoucher();
    TestRefreshAndScript();
    TestText();
    TestSubmitKinds();
    TestCharset();
    if (Failures)
    {
        printf("%d failure(s)\n", Failures);
        return 1;
    }
    printf("captiveportal html: all checks passed\n");
    return 0;
}
