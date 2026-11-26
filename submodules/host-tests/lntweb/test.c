/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Web view page scripts: QuickJS with the linkedom DOM and the browser bootstrap
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <quickjs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TIMERS 64

typedef struct
{
    int Id;
    int Delay;
    bool Armed;
} TIMER;

static int Failures;
static int Reports;
static TIMER Timers[MAX_TIMERS];
static char NavigateUrl[1024];
static char NavigateMethod[16];
static char NavigateBody[1024];
static char NavigateType[128];
static int Navigations;
static char RequestLine[2048];
static int Requests;

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

static char *
ReadFile(const char *Path, size_t *Length)
{
    FILE *File = fopen(Path, "rb");
    char *Data = NULL;
    long Size;

    if (!File)
        return NULL;
    if (fseek(File, 0, SEEK_END) == 0 && (Size = ftell(File)) >= 0 && fseek(File, 0, SEEK_SET) == 0)
    {
        Data = malloc((size_t)Size + 1);
        if (Data && fread(Data, 1, (size_t)Size, File) != (size_t)Size)
        {
            free(Data);
            Data = NULL;
        }
        if (Data)
        {
            Data[Size] = 0;
            *Length = (size_t)Size;
        }
    }
    fclose(File);
    return Data;
}

static void
CopyString(JSContext *Context, JSValueConst Value, char *Buffer, size_t Size)
{
    const char *Text = JS_IsNull(Value) || JS_IsUndefined(Value) ? NULL : JS_ToCString(Context, Value);

    snprintf(Buffer, Size, "%s", Text ? Text : "");
    if (Text)
        JS_FreeCString(Context, Text);
}

static JSValue
NativeReport(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    char Text[2048];

    (void)This;
    CopyString(Context, Count ? Arguments[0] : JS_UNDEFINED, Text, sizeof(Text));
    fprintf(stderr, "report: %s\n", Text);
    Reports++;
    return JS_UNDEFINED;
}

static JSValue
NativeSetTimer(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    int32_t Id = 0, Delay = 0;

    (void)This;
    if (Count < 2 || JS_ToInt32(Context, &Id, Arguments[0]) || JS_ToInt32(Context, &Delay, Arguments[1]))
        return JS_EXCEPTION;
    for (int Index = 0; Index < MAX_TIMERS; Index++)
    {
        if (!Timers[Index].Armed || Timers[Index].Id == Id)
        {
            Timers[Index].Id = Id;
            Timers[Index].Delay = Delay;
            Timers[Index].Armed = true;
            return JS_UNDEFINED;
        }
    }
    return JS_ThrowInternalError(Context, "too many timers");
}

static JSValue
NativeClearTimer(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    int32_t Id = 0;

    (void)This;
    if (Count < 1 || JS_ToInt32(Context, &Id, Arguments[0]))
        return JS_EXCEPTION;
    for (int Index = 0; Index < MAX_TIMERS; Index++)
    {
        if (Timers[Index].Armed && Timers[Index].Id == Id)
            Timers[Index].Armed = false;
    }
    return JS_UNDEFINED;
}

static JSValue
NativeNavigate(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    (void)This;
    if (Count < 4)
        return JS_UNDEFINED;
    CopyString(Context, Arguments[0], NavigateUrl, sizeof(NavigateUrl));
    CopyString(Context, Arguments[1], NavigateMethod, sizeof(NavigateMethod));
    CopyString(Context, Arguments[2], NavigateBody, sizeof(NavigateBody));
    CopyString(Context, Arguments[3], NavigateType, sizeof(NavigateType));
    Navigations++;
    return JS_UNDEFINED;
}

static JSValue
NativeRequest(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    char Method[16], Url[512], Headers[512], Body[512];
    JSValue Result;

    (void)This;
    if (Count < 4)
        return JS_NULL;
    CopyString(Context, Arguments[0], Method, sizeof(Method));
    CopyString(Context, Arguments[1], Url, sizeof(Url));
    CopyString(Context, Arguments[2], Headers, sizeof(Headers));
    CopyString(Context, Arguments[3], Body, sizeof(Body));
    snprintf(RequestLine, sizeof(RequestLine), "%s %s|%s|%s", Method, Url, Headers, Body);
    Requests++;
    if (strcmp(Url, "https://portal.example/api/register") != 0)
        return JS_NULL;
    Result = JS_NewObject(Context);
    JS_SetPropertyStr(Context, Result, "status", JS_NewInt32(Context, 201));
    JS_SetPropertyStr(Context, Result, "headers", JS_NewString(Context, "Content-Type: application/json\r\n"));
    JS_SetPropertyStr(Context, Result, "body",
                      JS_NewString(Context, "{\"redirect\":\"https://portal.example/welcome?id=7\"}"));
    return Result;
}

static JSValue
NativeFetch(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    char Url[512];

    (void)This;
    if (Count < 1)
        return JS_NULL;
    CopyString(Context, Arguments[0], Url, sizeof(Url));
    if (strcmp(Url, "https://portal.example/js/app.js") == 0)
        return JS_NewString(Context, "window.appLoaded = document.currentScript.getAttribute('data-app');");
    return JS_NULL;
}

static JSValue
NativeEvalScript(JSContext *Context, JSValueConst This, int Count, JSValueConst *Arguments)
{
    size_t Length = 0;
    const char *Code, *Name;
    JSValue Result;

    (void)This;
    if (Count < 2)
        return JS_UNDEFINED;
    Code = JS_ToCStringLen(Context, &Length, Arguments[0]);
    Name = JS_ToCString(Context, Arguments[1]);
    if (Code && Name)
    {
        Result = JS_Eval(Context, Code, Length, Name, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(Result))
        {
            JSValue Error = JS_GetException(Context);
            const char *Text = JS_ToCString(Context, Error);
            fprintf(stderr, "script error in %s: %s\n", Name, Text ? Text : "?");
            Reports++;
            if (Text)
                JS_FreeCString(Context, Text);
            JS_FreeValue(Context, Error);
        }
        JS_FreeValue(Context, Result);
    }
    if (Code)
        JS_FreeCString(Context, Code);
    if (Name)
        JS_FreeCString(Context, Name);
    return JS_UNDEFINED;
}

static bool
Eval(JSContext *Context, const char *Code, size_t Length, const char *Name)
{
    JSValue Result = JS_Eval(Context, Code, Length, Name, JS_EVAL_TYPE_GLOBAL);
    bool Evaluated = !JS_IsException(Result);

    if (!Evaluated)
    {
        JSValue Error = JS_GetException(Context);
        JSValue Stack = JS_GetPropertyStr(Context, Error, "stack");
        const char *Text = JS_ToCString(Context, Error), *Trace = JS_ToCString(Context, Stack);
        fprintf(stderr, "%s: %s\n%s\n", Name, Text ? Text : "?", Trace ? Trace : "");
        if (Text)
            JS_FreeCString(Context, Text);
        if (Trace)
            JS_FreeCString(Context, Trace);
        JS_FreeValue(Context, Stack);
        JS_FreeValue(Context, Error);
    }
    JS_FreeValue(Context, Result);
    return Evaluated;
}

static void
Drain(JSRuntime *Runtime)
{
    JSContext *Job;

    while (JS_ExecutePendingJob(Runtime, &Job) != 0)
        ;
}

static void
FireTimers(JSRuntime *Runtime, JSContext *Context)
{
    for (int Round = 0; Round < 16; Round++)
    {
        bool Fired = false;
        for (int Index = 0; Index < MAX_TIMERS; Index++)
        {
            char Code[64];
            if (!Timers[Index].Armed)
                continue;
            Timers[Index].Armed = false;
            snprintf(Code, sizeof(Code), "__lntFire(%d)", Timers[Index].Id);
            Eval(Context, Code, strlen(Code), "timer");
            Drain(Runtime);
            Fired = true;
        }
        if (!Fired)
            break;
    }
}

static char *
Query(JSContext *Context, const char *Expression)
{
    JSValue Result = JS_Eval(Context, Expression, strlen(Expression), "query", JS_EVAL_TYPE_GLOBAL);
    const char *Text;
    char *Copy = NULL;

    if (JS_IsException(Result))
    {
        Eval(Context, Expression, strlen(Expression), "query");
        return strdup("");
    }
    Text = JS_ToCString(Context, Result);
    Copy = strdup(Text ? Text : "");
    if (Text)
        JS_FreeCString(Context, Text);
    JS_FreeValue(Context, Result);
    return Copy;
}

static void
Run(JSContext *Context, const char *Code)
{
    Eval(Context, Code, strlen(Code), "test");
}

static JSContext *
Open(JSRuntime **Runtime, const char *Library, size_t LibraryLength, const char *Bootstrap, size_t BootstrapLength)
{
    JSContext *Context;
    JSValue Global, Native;

    memset(Timers, 0, sizeof(Timers));
    Navigations = Requests = Reports = 0;
    NavigateUrl[0] = NavigateMethod[0] = NavigateBody[0] = NavigateType[0] = RequestLine[0] = 0;
    *Runtime = JS_NewRuntime();
    JS_SetMaxStackSize(*Runtime, 6 * 1024 * 1024);
    Context = JS_NewContext(*Runtime);
    Global = JS_GetGlobalObject(Context);
    Native = JS_NewObject(Context);
    JS_SetPropertyStr(Context, Native, "report", JS_NewCFunction(Context, NativeReport, "report", 1));
    JS_SetPropertyStr(Context, Native, "setTimer", JS_NewCFunction(Context, NativeSetTimer, "setTimer", 2));
    JS_SetPropertyStr(Context, Native, "clearTimer", JS_NewCFunction(Context, NativeClearTimer, "clearTimer", 1));
    JS_SetPropertyStr(Context, Native, "navigate", JS_NewCFunction(Context, NativeNavigate, "navigate", 4));
    JS_SetPropertyStr(Context, Native, "request", JS_NewCFunction(Context, NativeRequest, "request", 4));
    JS_SetPropertyStr(Context, Native, "fetch", JS_NewCFunction(Context, NativeFetch, "fetch", 1));
    JS_SetPropertyStr(Context, Native, "evalScript", JS_NewCFunction(Context, NativeEvalScript, "evalScript", 2));
    JS_SetPropertyStr(Context, Native, "userAgent", JS_NewString(Context, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));
    JS_SetPropertyStr(Context, Native, "language", JS_NewString(Context, "en-US"));
    JS_SetPropertyStr(Context, Native, "screenWidth", JS_NewInt32(Context, 1280));
    JS_SetPropertyStr(Context, Native, "screenHeight", JS_NewInt32(Context, 800));
    JS_SetPropertyStr(Context, Native, "viewportWidth", JS_NewInt32(Context, 640));
    JS_SetPropertyStr(Context, Native, "viewportHeight", JS_NewInt32(Context, 480));
    JS_SetPropertyStr(Context, Global, "__lnt", Native);
    JS_FreeValue(Context, Global);
    CHECK(Eval(Context, Bootstrap, BootstrapLength, "bootstrap.js"), "bootstrap did not evaluate");
    CHECK(Eval(Context, Library, LibraryLength, "linkedom.js"), "linkedom bundle did not evaluate");
    return Context;
}

static void
Load(JSRuntime *Runtime, JSContext *Context, const char *Html, const char *Url)
{
    JSValue Global = JS_GetGlobalObject(Context);
    JSValue Function = JS_GetPropertyStr(Context, Global, "__lntLoad");
    JSValue Arguments[2] = { JS_NewString(Context, Html), JS_NewString(Context, Url) };
    JSValue Result = JS_Call(Context, Function, Global, 2, Arguments);

    CHECK(!JS_IsException(Result) && JS_ToBool(Context, Result) > 0, "__lntLoad failed for %s", Url);
    if (JS_IsException(Result))
        Eval(Context, "throw 0", 7, "load");
    JS_FreeValue(Context, Result);
    JS_FreeValue(Context, Arguments[0]);
    JS_FreeValue(Context, Arguments[1]);
    JS_FreeValue(Context, Function);
    JS_FreeValue(Context, Global);
    Run(Context, "__lntRunScripts()");
    Drain(Runtime);
    Run(Context, "__lntReady()");
    Drain(Runtime);
    FireTimers(Runtime, Context);
}

static void
Close(JSRuntime *Runtime, JSContext *Context)
{
    JS_FreeContext(Context);
    JS_FreeRuntime(Runtime);
}

static const char HotelPage[] =
    "<!DOCTYPE html><html><head><title>Hotel</title>"
    "<script src=\"/js/app.js\" data-app=\"hotel\"></script>"
    "<script>"
    "document.addEventListener('DOMContentLoaded', function () {"
    "  document.getElementById('token').value = 'f00d';"
    "  var accept = document.getElementById('accept'), go = document.getElementById('go');"
    "  accept.addEventListener('change', function () { go.disabled = !accept.checked; });"
    "  setTimeout(function () { document.getElementById('note').textContent = 'Ready'; }, 50);"
    "});"
    "</script></head><body>"
    "<p id=\"note\">Loading</p>"
    "<form action=\"/login\" method=\"post\">"
    "<input type=\"hidden\" id=\"token\" name=\"token\" value=\"\">"
    "<input id=\"room\" name=\"room\" required>"
    "<select id=\"plan\" name=\"plan\"><option value=\"day\">1 day</option><option value=\"week\">1 week</option></select>"
    "<label><input type=\"checkbox\" id=\"accept\" name=\"accept\" value=\"yes\"> I accept</label>"
    "<input type=\"submit\" id=\"go\" name=\"go\" value=\"Connect\" disabled>"
    "</form></body></html>";

static const char ApiPage[] =
    "<!DOCTYPE html><html><body>"
    "<form id=\"f\" novalidate><input id=\"email\" name=\"email\" type=\"email\">"
    "<button id=\"send\" type=\"submit\">Send</button></form>"
    "<a id=\"stay\" href=\"/elsewhere\" onclick=\"return false\">Stay</a>"
    "<script>"
    "document.getElementById('f').addEventListener('submit', function (event) {"
    "  event.preventDefault();"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/api/register');"
    "  xhr.setRequestHeader('Content-Type', 'application/json');"
    "  xhr.onload = function () {"
    "    if (xhr.status === 201) window.location.href = JSON.parse(xhr.responseText).redirect;"
    "  };"
    "  xhr.send(JSON.stringify({ email: document.getElementById('email').value }));"
    "});"
    "fetch('/api/register', { method: 'POST', body: 'probe' }).then(function (r) { return r.json(); })"
    "  .then(function (j) { document.body.setAttribute('data-fetched', j.redirect); });"
    "</script></body></html>";

static const char HashPage[] =
    "<!DOCTYPE html><html><body>"
    "<a id=\"open\" href=\"#terms\">Terms</a>"
    "<div id=\"terms\" class=\"modal\">Terms text</div>"
    "<input type=\"checkbox\" id=\"box\">"
    "<form action=\"/x\" method=\"post\"><button id=\"off\" disabled>Go</button></form>"
    "<script>"
    "window.onload = function () { document.body.setAttribute('data-onload', '1'); };"
    "window.addEventListener('hashchange', function (e) {"
    "  document.body.setAttribute('data-hash', location.hash + '|' + e.newURL);"
    "});"
    "document.getElementById('box').addEventListener('change', function () {"
    "  document.body.setAttribute('data-box', String(this.checked));"
    "});"
    "</script></body></html>";

static void
TestHash(const char *Library, size_t LibraryLength, const char *Bootstrap, size_t BootstrapLength)
{
    JSRuntime *Runtime;
    JSContext *Context = Open(&Runtime, Library, LibraryLength, Bootstrap, BootstrapLength);
    char *Html, *Open_, *Value;
    char Code[256];

    Load(Runtime, Context, HashPage, "https://portal.example/p");
    Value = Query(Context, "String(document.body.getAttribute('data-onload'))");
    CHECK(strcmp(Value, "1") == 0, "window.onload ran: %s", Value);
    free(Value);

    Html = Query(Context, "__lntSerialize()");
    CHECK(strstr(Html, "data-lnt-target") == NULL, "no target before a fragment is set");
    free(Html);
    Open_ = Query(Context, "document.getElementById('open').getAttribute('data-lntid')");
    snprintf(Code, sizeof(Code), "__lntClick('%s', [])", Open_);
    Run(Context, Code);
    FireTimers(Runtime, Context);
    CHECK(Navigations == 0, "a fragment link stays on the page");
    Value = Query(Context, "location.hash + ' ' + location.href + ' ' + document.body.getAttribute('data-hash')");
    CHECK(strcmp(Value, "#terms https://portal.example/p#terms #terms|https://portal.example/p#terms") == 0,
          "fragment navigation and hashchange: %s", Value);
    free(Value);
    Html = Query(Context, "__lntSerialize()");
    CHECK(strstr(Html, "id=\"terms\"") && strstr(Html, "data-lnt-target") &&
              strstr(Html, "data-lnt-target") > strstr(Html, "id=\"terms\"") - 200,
          "the target element is marked");
    free(Html);

    Run(Context, "history.back()");
    FireTimers(Runtime, Context);
    Value = Query(Context, "'[' + location.hash + '] ' + history.length + ' ' + document.body.getAttribute('data-hash')");
    CHECK(strcmp(Value, "[] 2 |https://portal.example/p") == 0, "history.back() returns to the page: %s", Value);
    free(Value);
    Html = Query(Context, "__lntSerialize()");
    CHECK(strstr(Html, "data-lnt-target") == NULL, "the target mark is cleared");
    free(Html);

    Run(Context, "document.getElementById('box').click()");
    Value = Query(Context, "document.getElementById('box').checked + ' ' + document.body.getAttribute('data-box')");
    CHECK(strcmp(Value, "true true") == 0, "click() ticks the box and fires change: %s", Value);
    free(Value);

    Value = Query(Context, "String(document.getElementsByTagName('*').length === document.querySelectorAll('*').length && "
                           "document.body.getElementsByTagName('*').length > 3)");
    CHECK(strcmp(Value, "true") == 0, "getElementsByTagName('*') returns every element: %s", Value);
    free(Value);
    Run(Context, "var clicks = 0; document.getElementById('off').addEventListener('click', function () { clicks++; });"
                 "document.getElementById('off').click()");
    Value = Query(Context, "String(clicks)");
    CHECK(strcmp(Value, "0") == 0 && Navigations == 0, "click() on a disabled button does nothing: %s clicks, %d navigations",
          Value, Navigations);
    free(Value);

    Run(Context, "location.href = 'https://portal.example/p#x'");
    FireTimers(Runtime, Context);
    CHECK(Navigations == 0, "a same-page fragment URL does not navigate");
    Run(Context, "location.href = '/other'");
    CHECK(Navigations == 1 && strcmp(NavigateUrl, "https://portal.example/other") == 0 && strcmp(NavigateMethod, "GET") == 0,
          "another page navigates: %s", NavigateUrl);
    CHECK(Reports == 0, "%d script errors on the fragment page", Reports);
    free(Open_);
    Close(Runtime, Context);
}

static void
TestHotel(const char *Library, size_t LibraryLength, const char *Bootstrap, size_t BootstrapLength)
{
    JSRuntime *Runtime;
    JSContext *Context = Open(&Runtime, Library, LibraryLength, Bootstrap, BootstrapLength);
    char *Html, *Room, *Accept, *Go, *Plan, *Value;
    char Code[512];

    Load(Runtime, Context, HotelPage, "https://portal.example/login");
    CHECK(Reports == 0, "%d script errors on the hotel page", Reports);
    Value = Query(Context, "String(window.appLoaded)");
    CHECK(strcmp(Value, "hotel") == 0, "external script ran with currentScript: got %s", Value);
    free(Value);

    Html = Query(Context, "__lntSerialize()");
    CHECK(strstr(Html, "<!DOCTYPE html>") == Html, "serialized page starts with a doctype");
    CHECK(strstr(Html, "data-lntid=") != NULL, "elements carry view ids");
    CHECK(strstr(Html, "value=\"f00d\"") != NULL, "the script's value is serialized as an attribute");
    CHECK(strstr(Html, ">Ready<") != NULL, "the timer changed the page");
    free(Html);

    Room = Query(Context, "document.getElementById('room').getAttribute('data-lntid')");
    Accept = Query(Context, "document.getElementById('accept').getAttribute('data-lntid')");
    Go = Query(Context, "document.getElementById('go').getAttribute('data-lntid')");
    Plan = Query(Context, "document.getElementById('plan').getAttribute('data-lntid')");

    snprintf(Code, sizeof(Code), "__lntImplicitSubmit('%s', [{id: '%s', value: ''}])", Room, Room);
    Run(Context, Code);
    FireTimers(Runtime, Context);
    CHECK(Navigations == 0, "a required field left empty blocks the submission");

    snprintf(Code, sizeof(Code), "__lntClick('%s', [{id: '%s', value: '204'}])", Accept, Room);
    Run(Context, Code);
    Value = Query(Context, "String(document.getElementById('go').disabled)");
    CHECK(strcmp(Value, "false") == 0, "the change listener enabled the submit button: got %s", Value);
    free(Value);
    Html = Query(Context, "__lntSerialize()");
    CHECK(strstr(Html, "checked") != NULL, "the ticked box is serialized as checked");
    free(Html);

    snprintf(Code, sizeof(Code), "__lntInput('%s', [{id: '%s', value: '204'}, {id: '%s', index: 1}], true)", Room, Room,
             Plan);
    Run(Context, Code);
    snprintf(Code, sizeof(Code), "__lntImplicitSubmit('%s', [{id: '%s', value: '204'}])", Room, Room);
    Run(Context, Code);
    FireTimers(Runtime, Context);
    CHECK(Navigations == 1, "Enter in the room field submits once: %d", Navigations);
    CHECK(strcmp(NavigateUrl, "https://portal.example/login") == 0, "form action resolved: %s", NavigateUrl);
    CHECK(strcmp(NavigateMethod, "POST") == 0, "form method: %s", NavigateMethod);
    CHECK(strcmp(NavigateBody, "token=f00d&room=204&plan=week&accept=yes&go=Connect") == 0, "form body: %s",
          NavigateBody);
    CHECK(strcmp(NavigateType, "application/x-www-form-urlencoded") == 0, "form type: %s", NavigateType);

    snprintf(Code, sizeof(Code), "__lntClick('%s', [{id: '%s', value: '205'}])", Go, Room);
    Run(Context, Code);
    CHECK(Navigations == 2 && strstr(NavigateBody, "room=205") != NULL, "a click on Connect submits: %s", NavigateBody);
    CHECK(Reports == 0, "%d script errors while signing in", Reports);
    free(Room);
    free(Accept);
    free(Go);
    free(Plan);
    Close(Runtime, Context);
}

static void
TestApi(const char *Library, size_t LibraryLength, const char *Bootstrap, size_t BootstrapLength)
{
    JSRuntime *Runtime;
    JSContext *Context = Open(&Runtime, Library, LibraryLength, Bootstrap, BootstrapLength);
    char *Email, *Send, *Stay, *Value;
    char Code[512];

    Load(Runtime, Context, ApiPage, "https://portal.example/start");
    CHECK(Reports == 0, "%d script errors on the API page", Reports);
    Value = Query(Context, "String(document.body.getAttribute('data-fetched'))");
    CHECK(strcmp(Value, "https://portal.example/welcome?id=7") == 0, "fetch resolved: %s", Value);
    free(Value);
    CHECK(strstr(RequestLine, "Content-Type: text/plain;charset=UTF-8|probe") != NULL,
          "fetch sends a default content type: %s", RequestLine);

    Email = Query(Context, "document.getElementById('email').getAttribute('data-lntid') || (__lntSerialize(), "
                           "document.getElementById('email').getAttribute('data-lntid'))");
    Send = Query(Context, "document.getElementById('send').getAttribute('data-lntid')");
    Stay = Query(Context, "document.getElementById('stay').getAttribute('data-lntid')");

    snprintf(Code, sizeof(Code), "__lntClick('%s', [])", Stay);
    Run(Context, Code);
    CHECK(Navigations == 0, "onclick returning false keeps the link from navigating");

    snprintf(Code, sizeof(Code), "__lntClick('%s', [{id: '%s', value: 'guest@example.com'}])", Send, Email);
    Run(Context, Code);
    FireTimers(Runtime, Context);
    CHECK(strcmp(RequestLine,
                 "POST https://portal.example/api/register|Content-Type: application/json|{\"email\":\"guest@example.com\"}") ==
              0,
          "XHR request: %s", RequestLine);
    CHECK(Navigations == 1 && strcmp(NavigateUrl, "https://portal.example/welcome?id=7") == 0 &&
              strcmp(NavigateMethod, "GET") == 0,
          "location.href after the API answered: %d %s %s", Navigations, NavigateMethod, NavigateUrl);
    CHECK(Reports == 0, "%d script errors in the API flow", Reports);
    free(Email);
    free(Send);
    free(Stay);
    Close(Runtime, Context);
}

int
main(int argc, char **argv)
{
    size_t LibraryLength = 0, BootstrapLength = 0;
    char *Library, *Bootstrap;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s linkedom.js bootstrap.js\n", argv[0]);
        return 2;
    }
    Library = ReadFile(argv[1], &LibraryLength);
    Bootstrap = ReadFile(argv[2], &BootstrapLength);
    if (!Library || !Bootstrap)
    {
        fprintf(stderr, "cannot read the scripts\n");
        return 2;
    }
    TestHotel(Library, LibraryLength, Bootstrap, BootstrapLength);
    TestApi(Library, LibraryLength, Bootstrap, BootstrapLength);
    TestHash(Library, LibraryLength, Bootstrap, BootstrapLength);
    free(Library);
    free(Bootstrap);
    printf("lntweb.script: %d failures\n", Failures);
    return Failures ? 1 : 0;
}
