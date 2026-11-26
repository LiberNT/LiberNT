/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Page scripts: QuickJS with a linkedom DOM, on a thread of their own
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntwebp.h"
#include "resource.h"

#include <objbase.h>
#include <quickjs.h>
#include <algorithm>

#define LNTWEB_RENDER_INTERVAL 100
#define LNTWEB_SCRIPT_STACK (8 * 1024 * 1024)
#define LNTWEB_SCRIPT_MEMORY (512 * 1024 * 1024)
#define LNTWEB_SCRIPT_TIME_LIMIT 10000
#define LNTWEB_JOB_LIMIT 100000
#define LNTWEB_DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"

namespace lntweb
{

class Script
{
public:
    ~Script()
    {
        if (Wake)
            CloseHandle(Wake);
    }

    void Run();

    HWND View = nullptr;
    LONG Sequence = 0;
    LNTWEB_HOST Host = {};
    std::shared_ptr<Resources> Cache;
    std::string Html;
    std::string Url;
    int Dpi = 96;
    volatile LONG Width = 1;
    volatile LONG Height = 1;
    volatile LONG Stopping = 0;
    HANDLE Wake = nullptr;
    SRWLOCK Lock = SRWLOCK_INIT;
    std::deque<ScriptTask> Tasks;

    JSRuntime *Runtime = nullptr;
    JSContext *Context = nullptr;
    std::map<int32_t, ULONGLONG> Timers;
    ULONGLONG TaskStart = 0;
    ULONGLONG LastRender = 0;
    bool Dirty = false;
    std::string LastHtml;

private:
    bool Install();
    bool Eval(const std::string &Code, const char *Name);
    JSValue CallGlobal(const char *Name, int Count, JSValueConst *Arguments);
    void DrainJobs();
    void Dispatch(const ScriptTask &Task);
    void FireTimers();
    void Render(bool First);
    void Post(const std::string &Html, bool Scripted, bool Rerender);
};

static std::string
ToString(JSContext *Context, JSValueConst Value)
{
    size_t Length = 0;
    const char *Text = JS_ToCStringLen(Context, &Length, Value);
    std::string Result;

    if (Text)
    {
        Result.assign(Text, Length);
        JS_FreeCString(Context, Text);
    }
    return Result;
}

static Script *
Owner(JSContext *Context)
{
    return (Script *)JS_GetContextOpaque(Context);
}

static void
Report(const std::string &Text)
{
    OutputDebugStringA(("lntweb: " + Text + "\n").c_str());
}

static void
ReportException(JSContext *Context, const char *Where)
{
    JSValue Error = JS_GetException(Context);
    std::string Text = std::string("script error in ") + (Where ? Where : "page") + ": " + ToString(Context, Error);

    if (JS_IsObject(Error))
    {
        JSValue Stack = JS_GetPropertyStr(Context, Error, "stack");
        if (JS_IsString(Stack))
            Text += "\n" + ToString(Context, Stack);
        JS_FreeValue(Context, Stack);
    }
    JS_FreeValue(Context, Error);
    Report(Text);
}

static int
Interrupt(JSContext *, void *Opaque)
{
    Script *Self = (Script *)Opaque;

    if (Self->Stopping)
        return 1;
    return Self->TaskStart && GetTickCount64() - Self->TaskStart > LNTWEB_SCRIPT_TIME_LIMIT;
}

static bool
LoadText(int Id, std::string &Text)
{
    HRSRC Resource = FindResourceW(Module, MAKEINTRESOURCEW(Id), RT_RCDATA);
    HGLOBAL Handle = Resource ? LoadResource(Module, Resource) : nullptr;
    const char *Data = Handle ? (const char *)LockResource(Handle) : nullptr;

    if (!Data)
        return false;
    Text.assign(Data, SizeofResource(Module, Resource));
    return true;
}

static bool
IsWebUrl(const std::string &Url)
{
    return Url.compare(0, 7, "http://") == 0 || Url.compare(0, 8, "https://") == 0;
}

static void
Restart(Script *Self)
{
    if (Self->TaskStart)
        Self->TaskStart = GetTickCount64();
}

static JSValue
NativeReport(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    if (Count >= 1)
        Report(ToString(Context, Arguments[0]));
    return JS_UNDEFINED;
}

static JSValue
NativeSetTimer(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    Script *Self = Owner(Context);
    int32_t Id = 0, Delay = 0;

    if (Count < 2 || JS_ToInt32(Context, &Id, Arguments[0]) || JS_ToInt32(Context, &Delay, Arguments[1]))
        return JS_EXCEPTION;
    Self->Timers[Id] = GetTickCount64() + (ULONGLONG)std::max(Delay, 0);
    return JS_UNDEFINED;
}

static JSValue
NativeClearTimer(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    int32_t Id = 0;

    if (Count < 1 || JS_ToInt32(Context, &Id, Arguments[0]))
        return JS_EXCEPTION;
    Owner(Context)->Timers.erase(Id);
    return JS_UNDEFINED;
}

static JSValue
NativeNavigate(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    Script *Self = Owner(Context);
    NavigateRequest *Request;

    if (Count < 4)
        return JS_UNDEFINED;
    Request = new (std::nothrow) NavigateRequest;
    if (!Request)
        return JS_UNDEFINED;
    Request->Sequence = Self->Sequence;
    Request->Url = ToString(Context, Arguments[0]);
    Request->HasBody = ToString(Context, Arguments[1]) == "POST";
    if (Request->HasBody)
    {
        if (!JS_IsNull(Arguments[2]) && !JS_IsUndefined(Arguments[2]))
            Request->Body = ToString(Context, Arguments[2]);
        if (!JS_IsNull(Arguments[3]) && !JS_IsUndefined(Arguments[3]))
            Request->ContentType = ToString(Context, Arguments[3]);
    }
    if (!IsWebUrl(Request->Url) || !PostMessageW(Self->View, WM_LNTWEB_NAVIGATE, 0, (LPARAM)Request))
        delete Request;
    return JS_UNDEFINED;
}

static JSValue
NativeRequest(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    Script *Self = Owner(Context);
    ULONG Status = 0;
    PSTR Headers = nullptr, Data = nullptr;
    SIZE_T Length = 0;
    bool Done = false;

    if (Count < 4)
        return JS_NULL;
    std::string Method = ToString(Context, Arguments[0]), Url = ToString(Context, Arguments[1]);
    std::string RequestHeaders = ToString(Context, Arguments[2]);
    bool HasBody = !JS_IsNull(Arguments[3]) && !JS_IsUndefined(Arguments[3]);
    std::string Body = HasBody ? ToString(Context, Arguments[3]) : std::string();

    if (!IsWebUrl(Url))
        return JS_NULL;
    if (Self->Host.Request)
    {
        Done = Self->Host.Request(Self->Host.Context, Method.c_str(), Url.c_str(), RequestHeaders.c_str(),
                                  HasBody ? Body.c_str() : nullptr, Body.size(), &Status, &Headers, &Data, &Length) !=
               FALSE;
    }
    else if (Method == "GET" && Self->Host.Fetch)
    {
        Done = Self->Host.Fetch(Self->Host.Context, Url.c_str(), &Data, &Length) != FALSE;
        Status = 200;
    }
    Restart(Self);
    if (!Done)
        return JS_NULL;

    JSValue Result = JS_NewObject(Context);
    JS_SetPropertyStr(Context, Result, "status", JS_NewInt32(Context, (int32_t)Status));
    JS_SetPropertyStr(Context, Result, "headers", JS_NewString(Context, Headers ? Headers : ""));
    JS_SetPropertyStr(Context, Result, "body", JS_NewStringLen(Context, Data ? Data : "", Data ? Length : 0));
    if (Self->Host.Free)
    {
        if (Headers)
            Self->Host.Free(Self->Host.Context, Headers);
        if (Data)
            Self->Host.Free(Self->Host.Context, Data);
    }
    return Result;
}

static JSValue
NativeFetch(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    Script *Self = Owner(Context);
    std::string Data;
    bool Fetched;

    if (Count < 1)
        return JS_NULL;
    Fetched = Self->Cache->Fetch(ToString(Context, Arguments[0]), Data);
    Restart(Self);
    return Fetched ? JS_NewStringLen(Context, Data.data(), Data.size()) : JS_NULL;
}

static JSValue
NativeEvalScript(JSContext *Context, JSValueConst, int Count, JSValueConst *Arguments)
{
    if (Count < 2)
        return JS_UNDEFINED;
    std::string Code = ToString(Context, Arguments[0]), Name = ToString(Context, Arguments[1]);
    JSValue Result = JS_Eval(Context, Code.c_str(), Code.size(), Name.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(Result))
        ReportException(Context, Name.c_str());
    JS_FreeValue(Context, Result);
    return JS_UNDEFINED;
}

bool
Script::Install()
{
    static const struct
    {
        const char *Name;
        JSCFunction *Function;
        int Length;
    } Functions[] = {
        { "report", NativeReport, 1 },     { "setTimer", NativeSetTimer, 2 },  { "clearTimer", NativeClearTimer, 1 },
        { "navigate", NativeNavigate, 4 }, { "request", NativeRequest, 4 },    { "fetch", NativeFetch, 1 },
        { "evalScript", NativeEvalScript, 2 },
    };
    WCHAR Locale[LOCALE_NAME_MAX_LENGTH];
    std::string Language = "en-US";
    JSValue Global = JS_GetGlobalObject(Context);
    JSValue Native = JS_NewObject(Context);

    if (GetUserDefaultLocaleName(Locale, ARRAYSIZE(Locale)) > 1)
        Language = WideToUtf8(Locale);
    for (const auto &Entry : Functions)
        JS_SetPropertyStr(Context, Native, Entry.Name, JS_NewCFunction(Context, Entry.Function, Entry.Name, Entry.Length));
    JS_SetPropertyStr(Context, Native, "userAgent",
                      JS_NewString(Context, Host.UserAgent ? Host.UserAgent : LNTWEB_DEFAULT_USER_AGENT));
    JS_SetPropertyStr(Context, Native, "language", JS_NewString(Context, Language.c_str()));
    JS_SetPropertyStr(Context, Native, "screenWidth", JS_NewInt32(Context, GetSystemMetrics(SM_CXSCREEN)));
    JS_SetPropertyStr(Context, Native, "screenHeight", JS_NewInt32(Context, GetSystemMetrics(SM_CYSCREEN)));
    JS_SetPropertyStr(Context, Native, "viewportWidth", JS_NewInt32(Context, Width));
    JS_SetPropertyStr(Context, Native, "viewportHeight", JS_NewInt32(Context, Height));
    bool Installed = JS_SetPropertyStr(Context, Global, "__lnt", Native) >= 0;
    JS_FreeValue(Context, Global);
    return Installed;
}

bool
Script::Eval(const std::string &Code, const char *Name)
{
    JSValue Result = JS_Eval(Context, Code.c_str(), Code.size(), Name, JS_EVAL_TYPE_GLOBAL);
    bool Evaluated = !JS_IsException(Result);

    if (!Evaluated)
        ReportException(Context, Name);
    JS_FreeValue(Context, Result);
    return Evaluated;
}

JSValue
Script::CallGlobal(const char *Name, int Count, JSValueConst *Arguments)
{
    JSValue Global = JS_GetGlobalObject(Context);
    JSValue Function = JS_GetPropertyStr(Context, Global, Name);
    JSValue Result = JS_UNDEFINED;

    if (JS_IsFunction(Context, Function))
    {
        Result = JS_Call(Context, Function, Global, Count, Arguments);
        if (JS_IsException(Result))
        {
            ReportException(Context, Name);
            Result = JS_UNDEFINED;
        }
    }
    JS_FreeValue(Context, Function);
    JS_FreeValue(Context, Global);
    return Result;
}

void
Script::DrainJobs()
{
    JSContext *JobContext = nullptr;

    for (int Count = 0; Count < LNTWEB_JOB_LIMIT && !Stopping; Count++)
    {
        int Status = JS_ExecutePendingJob(Runtime, &JobContext);
        if (Status == 0)
            break;
        if (Status < 0 && JobContext)
            ReportException(JobContext, "promise job");
        Dirty = true;
    }
}

void
Script::Dispatch(const ScriptTask &Task)
{
    static const char *const Names[] = { "__lntClick", "__lntInput", "__lntInput", "__lntImplicitSubmit" };
    JSValue Fields = JS_NewArray(Context);
    JSValue Arguments[3];
    uint32_t Index = 0;

    for (const auto &Field : Task.Fields)
    {
        JSValue Item = JS_NewObject(Context);
        JS_SetPropertyStr(Context, Item, "id", JS_NewStringLen(Context, Field.Id.data(), Field.Id.size()));
        if (Field.IsSelect)
            JS_SetPropertyStr(Context, Item, "index", JS_NewInt32(Context, Field.Index));
        else
            JS_SetPropertyStr(Context, Item, "value", JS_NewStringLen(Context, Field.Value.data(), Field.Value.size()));
        JS_SetPropertyUint32(Context, Fields, Index++, Item);
    }
    Arguments[0] = JS_NewStringLen(Context, Task.Id.data(), Task.Id.size());
    Arguments[1] = Fields;
    Arguments[2] = JS_NewBool(Context, Task.Event == ScriptEvent::Change);
    JS_FreeValue(Context, CallGlobal(Names[(int)Task.Event], 3, Arguments));
    JS_FreeValue(Context, Arguments[0]);
    JS_FreeValue(Context, Arguments[1]);
    Dirty = true;
}

void
Script::FireTimers()
{
    ULONGLONG Now = GetTickCount64();
    std::vector<std::pair<ULONGLONG, int32_t>> Due;

    for (const auto &Timer : Timers)
    {
        if (Timer.second <= Now)
            Due.emplace_back(Timer.second, Timer.first);
    }
    std::sort(Due.begin(), Due.end());
    for (const auto &Entry : Due)
    {
        auto Timer = Timers.find(Entry.second);
        if (Stopping || Timer == Timers.end() || Timer->second != Entry.first)
            continue;
        Timers.erase(Timer);
        JSValue Id = JS_NewInt32(Context, Entry.second);
        JS_FreeValue(Context, CallGlobal("__lntFire", 1, &Id));
        DrainJobs();
        Dirty = true;
    }
}

void
Script::Post(const std::string &Text, bool Scripted, bool Rerender)
{
    LoadResult *Result = BuildDocument(Cache, Text, Url, Width, Height, Dpi);

    if (!Result)
        return;
    Result->Sequence = Sequence;
    Result->Scripted = Scripted;
    Result->Rerender = Rerender;
    if (Stopping || !PostMessageW(View, WM_LNTWEB_READY, 0, (LPARAM)Result))
        delete Result;
}

void
Script::Render(bool First)
{
    JSValue Value = CallGlobal("__lntSerialize", 0, nullptr);
    std::string Text = JS_IsString(Value) ? ToString(Context, Value) : std::string();

    JS_FreeValue(Context, Value);
    Dirty = false;
    LastRender = GetTickCount64();
    if (Text.empty())
    {
        if (First)
            Post(Html, false, false);
        return;
    }
    if (!First && Text == LastHtml)
        return;
    LastHtml = Text;
    Post(Text, true, !First);
}

void
Script::Run()
{
    std::string Library, Bootstrap;
    bool Loaded = false;

    Runtime = JS_NewRuntime();
    if (Runtime)
    {
        JS_SetMaxStackSize(Runtime, LNTWEB_SCRIPT_STACK - 512 * 1024);
        JS_SetMemoryLimit(Runtime, LNTWEB_SCRIPT_MEMORY);
        JS_SetInterruptHandler(Runtime, Interrupt, this);
        Context = JS_NewContext(Runtime);
    }
    if (Context)
    {
        JS_SetContextOpaque(Context, this);
        TaskStart = GetTickCount64();
        if (Install() && LoadText(IDR_LINKEDOM, Library) && LoadText(IDR_BOOTSTRAP, Bootstrap) &&
            Eval(Bootstrap, "bootstrap.js") && Eval(Library, "linkedom.js"))
        {
            JSValue Arguments[2] = { JS_NewStringLen(Context, Html.data(), Html.size()),
                                     JS_NewStringLen(Context, Url.data(), Url.size()) };
            JSValue Result = CallGlobal("__lntLoad", 2, Arguments);
            Loaded = JS_ToBool(Context, Result) > 0;
            JS_FreeValue(Context, Result);
            JS_FreeValue(Context, Arguments[0]);
            JS_FreeValue(Context, Arguments[1]);
        }
    }
    if (!Loaded)
    {
        Post(Html, false, false);
    }
    else
    {
        JS_FreeValue(Context, CallGlobal("__lntRunScripts", 0, nullptr));
        DrainJobs();
        JS_FreeValue(Context, CallGlobal("__lntReady", 0, nullptr));
        DrainJobs();
        TaskStart = 0;
        if (!Stopping)
            Render(true);
    }

    while (Loaded && !Stopping)
    {
        ULONGLONG Now = GetTickCount64(), Due = ~0ULL;
        DWORD Wait = INFINITE;
        std::deque<ScriptTask> Pending;

        for (const auto &Timer : Timers)
            Due = std::min(Due, Timer.second);
        if (Dirty)
            Due = std::min(Due, LastRender + LNTWEB_RENDER_INTERVAL);
        if (Due != ~0ULL)
            Wait = Due <= Now ? 0 : (DWORD)std::min<ULONGLONG>(Due - Now, 0x7FFFFFFF);
        WaitForSingleObject(Wake, Wait);
        if (Stopping)
            break;

        AcquireSRWLockExclusive(&Lock);
        Pending.swap(Tasks);
        ReleaseSRWLockExclusive(&Lock);
        TaskStart = GetTickCount64();
        for (const auto &Task : Pending)
        {
            if (Stopping)
                break;
            Dispatch(Task);
            DrainJobs();
        }
        FireTimers();
        DrainJobs();
        TaskStart = 0;
        if (!Stopping && Dirty && GetTickCount64() >= LastRender + LNTWEB_RENDER_INTERVAL)
            Render(false);
    }

    if (Context)
        JS_FreeContext(Context);
    if (Runtime)
        JS_FreeRuntime(Runtime);
    Context = nullptr;
    Runtime = nullptr;
}

static DWORD WINAPI
ScriptThread(LPVOID Parameter)
{
    {
        std::shared_ptr<Script> Engine = std::move(*(std::shared_ptr<Script> *)Parameter);
        delete (std::shared_ptr<Script> *)Parameter;
        HRESULT Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try
        {
            Engine->Run();
        }
        catch (...)
        {
        }
        Engine.reset();
        if (SUCCEEDED(Com))
            CoUninitialize();
    }
    FreeLibraryAndExitThread(Module, 0);
}

std::shared_ptr<Script>
StartScript(HWND View, LONG Sequence, const LNTWEB_HOST &Host, const std::shared_ptr<Resources> &Cache,
            const std::string &Html, const std::string &Url, int Width, int Height, int Dpi)
{
    std::shared_ptr<Script> Engine = std::make_shared<Script>();
    HMODULE Pinned;
    HANDLE Thread;

    Engine->View = View;
    Engine->Sequence = Sequence;
    Engine->Host = Host;
    Engine->Cache = Cache;
    Engine->Html = Html;
    Engine->Url = Url;
    Engine->Width = Width;
    Engine->Height = Height;
    Engine->Dpi = Dpi;
    Engine->Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!Engine->Wake ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void *)ScriptThread, &Pinned))
    {
        return nullptr;
    }
    auto *Holder = new std::shared_ptr<Script>(Engine);
    Thread = CreateThread(nullptr, LNTWEB_SCRIPT_STACK, ScriptThread, Holder, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!Thread)
    {
        delete Holder;
        FreeLibrary(Pinned);
        return nullptr;
    }
    CloseHandle(Thread);
    return Engine;
}

void
PostScriptTask(const std::shared_ptr<Script> &Engine, ScriptTask &&Task)
{
    if (!Engine)
        return;
    AcquireSRWLockExclusive(&Engine->Lock);
    Engine->Tasks.push_back(std::move(Task));
    ReleaseSRWLockExclusive(&Engine->Lock);
    SetEvent(Engine->Wake);
}

void
ResizeScript(const std::shared_ptr<Script> &Engine, int Width, int Height)
{
    if (!Engine)
        return;
    InterlockedExchange(&Engine->Width, std::max(Width, 1));
    InterlockedExchange(&Engine->Height, std::max(Height, 1));
}

void
StopScript(const std::shared_ptr<Script> &Engine)
{
    if (!Engine)
        return;
    InterlockedExchange(&Engine->Stopping, 1);
    SetEvent(Engine->Wake);
}

}
