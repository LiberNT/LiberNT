/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     View window: loading, painting, scrolling, links and forms
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntwebp.h"

#include <objbase.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <litehtml/render_item.h>
#include <cstring>

#define LNTWEB_CLASS L"LiberNTWebView"
#define LNTWEB_FORM_CSS \
    "[hidden] { display: none }\n" \
    "input, select, textarea { border: 1px solid #767676; border-radius: 2px; padding: 1px 2px; background-color: white; }\n" \
    "input[type=checkbox], input[type=radio] { border: none; padding: 0; margin: 3px 3px 3px 4px; background-color: transparent; }\n" \
    "input[type=submit], input[type=button], input[type=reset], input[type=image], button { border: 1px solid #767676; " \
    "border-radius: 3px; padding: 1px 6px; background-color: #efefef; color: black; }\n"
#define WM_LNTWEB_READY (WM_APP + 0x100)
#define WM_LNTWEB_SUBMIT (WM_APP + 0x101)
#define LNTWEB_CONTROL_ID 2000
#define LNTWEB_LINE 40
#define LNTWEB_COMBO_DROP 200

namespace lntweb
{

struct LoadResult
{
    LONG Sequence = 0;
    std::unique_ptr<Container> Owner;
    std::shared_ptr<litehtml::document> Document;
};

struct LoadJob
{
    HWND Hwnd;
    LONG Sequence;
    std::string Html;
    std::string Url;
    LNTWEB_HOST Host;
    int Width;
    int Height;
    int Dpi;
};

struct View
{
    HWND Hwnd = nullptr;
    LNTWEB_HOST Host = {};
    std::unique_ptr<Container> Owner;
    std::shared_ptr<litehtml::document> Document;
    std::vector<std::shared_ptr<Control>> Controls;
    std::vector<std::vector<std::string>> OptionValues;
    LONG Sequence = 0;
    int ScrollY = 0;
    int ContentHeight = 0;
    int Dpi = 96;
    bool Tracking = false;
};

static HINSTANCE Module;

static DWORD WINAPI
LoadThread(LPVOID Parameter)
{
    std::unique_ptr<LoadJob> Job((LoadJob *)Parameter);
    LoadResult *Result = new LoadResult;
    HRESULT Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Result->Sequence = Job->Sequence;
    try
    {
        Result->Owner.reset(new Container(Job->Host, Job->Url, Job->Width, Job->Height, Job->Dpi));
        Result->Document = litehtml::document::createFromString(litehtml::estring(Job->Html, litehtml::encoding::utf_8),
                                                                 Result->Owner.get(),
                                                                 std::string(litehtml::master_css) + LNTWEB_FORM_CSS);
    }
    catch (...)
    {
        Result->Document.reset();
    }
    if (SUCCEEDED(Com))
        CoUninitialize();
    if (!PostMessageW(Job->Hwnd, WM_LNTWEB_READY, 0, (LPARAM)Result))
        delete Result;
    return 0;
}

static void
CollectControls(const litehtml::element::ptr &Element, std::vector<std::shared_ptr<Control>> &Controls)
{
    for (const auto &Child : Element->children())
    {
        auto Field = std::dynamic_pointer_cast<Control>(Child);
        if (Field)
            Controls.push_back(Field);
        CollectControls(Child, Controls);
    }
}

static void
CollectOptions(const litehtml::element::ptr &Element, std::vector<std::pair<std::string, std::string>> &Options,
               int &Selected)
{
    for (const auto &Child : Element->children())
    {
        const char *Tag = Child->get_tagName();
        if (Tag && strcmp(Tag, "option") == 0)
        {
            std::string Text;
            Child->get_text(Text);
            const char *Value = Child->get_attr("value");
            if (Child->get_attr("selected"))
                Selected = (int)Options.size();
            Options.emplace_back(Text, Value ? Value : Text);
        }
        else if (Tag && strcmp(Tag, "optgroup") == 0)
        {
            CollectOptions(Child, Options, Selected);
        }
    }
}

static LRESULT CALLBACK
FieldProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR, DWORD_PTR Data)
{
    if (Message == WM_KEYDOWN && WParam == VK_RETURN && !(GetWindowLongW(Hwnd, GWL_STYLE) & ES_MULTILINE))
    {
        PostMessageW((HWND)Data, WM_LNTWEB_SUBMIT, (WPARAM)GetDlgCtrlID(Hwnd), 0);
        return 0;
    }
    if (Message == WM_GETDLGCODE && !(GetWindowLongW(Hwnd, GWL_STYLE) & ES_MULTILINE))
        return DefSubclassProc(Hwnd, Message, WParam, LParam) | DLGC_WANTALLKEYS;
    return DefSubclassProc(Hwnd, Message, WParam, LParam);
}

static void
CreateField(View *Self, size_t Index)
{
    Control *Field = Self->Controls[Index].get();
    std::string Kind = Field->Type();
    std::wstring Text;
    DWORD Style = WS_CHILD | WS_TABSTOP;
    PCWSTR Class;

    if (Kind == "checkbox" || Kind == "radio")
    {
        Class = WC_BUTTONW;
        Style |= (Kind == "radio") ? BS_RADIOBUTTON : BS_AUTOCHECKBOX;
    }
    else if (Kind == "select")
    {
        Class = WC_COMBOBOXW;
        Style |= CBS_DROPDOWNLIST | WS_VSCROLL;
    }
    else if (Kind == "textarea")
    {
        std::string Value;
        Field->get_text(Value);
        Text = Utf8ToWide(Value);
        Class = WC_EDITW;
        Style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    }
    else
    {
        Text = Utf8ToWide(Field->get_attr("value", ""));
        Class = WC_EDITW;
        Style |= ES_AUTOHSCROLL | (Kind == "password" ? ES_PASSWORD : 0);
    }

    Field->Window = CreateWindowExW(0, Class, Text.c_str(), Style, 0, 0, 0, 0, Self->Hwnd,
                                    (HMENU)(UINT_PTR)(LNTWEB_CONTROL_ID + Index), Module, nullptr);
    if (!Field->Window)
        return;
    SendMessageW(Field->Window, WM_SETFONT, (WPARAM)Field->css().get_font(), FALSE);
    SetWindowTheme(Field->Window, L"Explorer", nullptr);

    if (Kind == "checkbox" || Kind == "radio")
    {
        SendMessageW(Field->Window, BM_SETCHECK, Field->get_attr("checked") ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    else if (Kind == "select")
    {
        std::vector<std::pair<std::string, std::string>> Options;
        int Selected = 0;
        CollectOptions(Self->Controls[Index], Options, Selected);
        for (const auto &Option : Options)
        {
            SendMessageW(Field->Window, CB_ADDSTRING, 0, (LPARAM)Utf8ToWide(Option.first).c_str());
            Self->OptionValues[Index].push_back(Option.second);
        }
        SendMessageW(Field->Window, CB_SETCURSEL, Selected, 0);
    }
    else
    {
        const char *Placeholder = Field->get_attr("placeholder");
        if (Placeholder)
            SendMessageW(Field->Window, EM_SETCUEBANNER, FALSE, (LPARAM)Utf8ToWide(Placeholder).c_str());
        if (Field->get_attr("readonly"))
            SendMessageW(Field->Window, EM_SETREADONLY, TRUE, 0);
        SetWindowSubclass(Field->Window, FieldProc, 1, (DWORD_PTR)Self->Hwnd);
    }
    if (Field->get_attr("disabled"))
        EnableWindow(Field->Window, FALSE);
}

static void
PlaceFields(View *Self)
{
    HDWP Positions = BeginDeferWindowPos((int)Self->Controls.size());

    for (size_t Index = 0; Index < Self->Controls.size(); Index++)
    {
        Control *Field = Self->Controls[Index].get();
        if (!Field->IsField())
            continue;
        litehtml::position Box = Field->get_placement();
        bool Shown = Box.width > 0 && Box.height > 0 && Field->css().get_visibility() != litehtml::visibility_hidden;

        if (Shown && !Field->Window)
            CreateField(Self, Index);
        if (!Field->Window)
            continue;
        if (!Shown)
        {
            ShowWindow(Field->Window, SW_HIDE);
            continue;
        }
        int Height = (int)Box.height + (Field->Type() == "select" ? LNTWEB_COMBO_DROP : 0);
        if (Positions)
        {
            Positions = DeferWindowPos(Positions, Field->Window, nullptr, (int)Box.x, (int)Box.y - Self->ScrollY,
                                       (int)Box.width, Height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    if (Positions)
        EndDeferWindowPos(Positions);
}

static void
UpdateScroll(View *Self)
{
    RECT Client;
    SCROLLINFO Info = { sizeof(Info) };

    GetClientRect(Self->Hwnd, &Client);
    Self->ScrollY = std::max(0, std::min(Self->ScrollY, Self->ContentHeight - (int)Client.bottom));
    Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    Info.nMax = std::max(Self->ContentHeight - 1, 0);
    Info.nPage = Client.bottom;
    Info.nPos = Self->ScrollY;
    SetScrollInfo(Self->Hwnd, SB_VERT, &Info, TRUE);
}

static void
Layout(View *Self)
{
    RECT Client;

    if (!Self->Document)
        return;
    GetClientRect(Self->Hwnd, &Client);
    int Width = Client.right - ((GetWindowLongW(Self->Hwnd, GWL_STYLE) & WS_VSCROLL) ? 0 : GetSystemMetrics(SM_CXVSCROLL));
    Self->Owner->SetViewport(std::max(Width, 1), (int)Client.bottom);
    Self->Document->media_changed();
    Self->Document->render((litehtml::pixel_t)std::max(Width, 1));
    Self->ContentHeight = (int)std::ceil(Self->Document->height());
    UpdateScroll(Self);
    PlaceFields(Self);
}

static void
ScrollTo(View *Self, int Position)
{
    RECT Client;
    int Old = Self->ScrollY;

    GetClientRect(Self->Hwnd, &Client);
    Self->ScrollY = std::max(0, std::min(Position, Self->ContentHeight - (int)Client.bottom));
    if (Self->ScrollY == Old)
        return;
    ScrollWindowEx(Self->Hwnd, 0, Old - Self->ScrollY, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateScroll(Self);
}

static void
Paint(View *Self)
{
    PAINTSTRUCT Ps;
    HDC Dc = BeginPaint(Self->Hwnd, &Ps);
    RECT Client;
    BITMAPINFO Info = {};
    void *Bits = nullptr;

    GetClientRect(Self->Hwnd, &Client);
    int Width = std::max<int>(Client.right, 1), Height = std::max<int>(Client.bottom, 1);
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    HDC Memory = CreateCompatibleDC(Dc);
    HBITMAP Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, &Bits, nullptr, 0);

    if (Memory && Bitmap && Bits)
    {
        HGDIOBJ Old = SelectObject(Memory, Bitmap);
        memset(Bits, 0xFF, (size_t)Width * Height * 4);
        if (Self->Document)
        {
            plutovg_surface_t *Surface = plutovg_surface_create_for_data((unsigned char *)Bits, Width, Height, Width * 4);
            plutovg_canvas_t *Canvas = Surface ? plutovg_canvas_create(Surface) : nullptr;
            if (Canvas)
            {
                DrawContext Context = { Memory, Canvas };
                litehtml::position Clip((litehtml::pixel_t)Ps.rcPaint.left, (litehtml::pixel_t)Ps.rcPaint.top,
                                        (litehtml::pixel_t)(Ps.rcPaint.right - Ps.rcPaint.left),
                                        (litehtml::pixel_t)(Ps.rcPaint.bottom - Ps.rcPaint.top));
                Self->Owner->SetDrawContext(&Context);
                Self->Document->draw((litehtml::uint_ptr)&Context, 0, (litehtml::pixel_t)-Self->ScrollY, &Clip);
                Self->Owner->SetDrawContext(nullptr);
                GdiFlush();
                plutovg_canvas_destroy(Canvas);
            }
            if (Surface)
                plutovg_surface_destroy(Surface);
        }
        BitBlt(Dc, Ps.rcPaint.left, Ps.rcPaint.top, Ps.rcPaint.right - Ps.rcPaint.left, Ps.rcPaint.bottom - Ps.rcPaint.top,
               Memory, Ps.rcPaint.left, Ps.rcPaint.top, SRCCOPY);
        SelectObject(Memory, Old);
    }
    if (Bitmap)
        DeleteObject(Bitmap);
    if (Memory)
        DeleteDC(Memory);
    EndPaint(Self->Hwnd, &Ps);
}

static std::string
Escape(const std::string &Text)
{
    static const char Hex[] = "0123456789ABCDEF";
    std::string Result;

    for (unsigned char Character : Text)
    {
        if (isalnum(Character) || Character == '*' || Character == '-' || Character == '.' || Character == '_')
            Result += (char)Character;
        else if (Character == ' ')
            Result += '+';
        else
        {
            Result += '%';
            Result += Hex[Character >> 4];
            Result += Hex[Character & 15];
        }
    }
    return Result;
}

static std::string
WindowText(HWND Window)
{
    int Length = GetWindowTextLengthW(Window);
    std::wstring Text(Length > 0 ? Length + 1 : 1, L'\0');
    GetWindowTextW(Window, &Text[0], Length + 1);
    Text.resize(Length > 0 ? Length : 0);
    return WideToUtf8(Text);
}

static litehtml::element::ptr
Ancestor(litehtml::element::ptr Element, const char *Tag)
{
    for (; Element; Element = Element->parent())
    {
        if (Element->get_tagName() && strcmp(Element->get_tagName(), Tag) == 0)
            return Element;
    }
    return nullptr;
}

static void
AddPair(std::string &Body, const std::string &Name, const std::string &Value)
{
    if (!Body.empty())
        Body += '&';
    Body += Escape(Name) + '=' + Escape(Value);
}

static void
CollectForm(View *Self, const litehtml::element::ptr &Element, const litehtml::element::ptr &Submitter, std::string &Body)
{
    for (const auto &Child : Element->children())
    {
        const char *Name = Child->get_attr("name");
        const char *Tag = Child->get_tagName();
        auto Field = std::dynamic_pointer_cast<Control>(Child);

        if (Field && Name && *Name && !Field->get_attr("disabled"))
        {
            std::string Kind = Field->Type();
            size_t Index = (size_t)(std::find(Self->Controls.begin(), Self->Controls.end(), Field) - Self->Controls.begin());
            HWND Window = Field->Window;

            if (Kind == "checkbox" || Kind == "radio")
            {
                bool Checked = Window ? SendMessageW(Window, BM_GETCHECK, 0, 0) == BST_CHECKED : Field->get_attr("checked") != nullptr;
                if (Checked)
                    AddPair(Body, Name, Field->get_attr("value", "on"));
            }
            else if (Kind == "select")
            {
                LRESULT Selected = Window ? SendMessageW(Window, CB_GETCURSEL, 0, 0) : 0;
                if (Index < Self->OptionValues.size() && Selected >= 0 &&
                    (size_t)Selected < Self->OptionValues[Index].size())
                {
                    AddPair(Body, Name, Self->OptionValues[Index][Selected]);
                }
            }
            else if (Kind == "image")
            {
                if (Child == Submitter)
                {
                    AddPair(Body, std::string(Name) + ".x", "0");
                    AddPair(Body, std::string(Name) + ".y", "0");
                }
            }
            else if (Field->IsButton())
            {
                if (Child == Submitter && Kind == "submit")
                    AddPair(Body, Name, Field->get_attr("value", ""));
            }
            else if (Kind != "file")
            {
                std::string Value;
                if (Window)
                    Value = WindowText(Window);
                else if (Kind == "textarea")
                    Field->get_text(Value);
                else
                    Value = Field->get_attr("value", "");
                AddPair(Body, Name, Value);
            }
        }
        else if (Tag && strcmp(Tag, "button") == 0 && Child == Submitter && Name && *Name)
        {
            AddPair(Body, Name, Child->get_attr("value", ""));
        }
        if (!Field)
            CollectForm(Self, Child, Submitter, Body);
    }
}

static void
Submit(View *Self, const litehtml::element::ptr &Submitter)
{
    litehtml::element::ptr Form = Ancestor(Submitter, "form");
    std::string Body, Method, Action;

    if (!Form || !Self->Host.Navigate)
        return;
    CollectForm(Self, Form, Submitter, Body);
    Method = Form->get_attr("method", "get");
    for (auto &Character : Method)
        Character = (char)tolower((unsigned char)Character);
    Action = Self->Owner->Resolve(Form->get_attr("action", ""));
    if (Method == "post")
    {
        Self->Host.Navigate(Self->Host.Context, Action.c_str(), Body.c_str(), Body.size(),
                            "application/x-www-form-urlencoded");
    }
    else
    {
        Action = Action.substr(0, Action.find_first_of("?#")) + "?" + Body;
        Self->Host.Navigate(Self->Host.Context, Action.c_str(), nullptr, 0, nullptr);
    }
}

static void
ClickAt(View *Self, int X, int Y)
{
    litehtml::element::ptr Element = Self->Document->root_render()->get_element_by_point(
        (litehtml::pixel_t)X, (litehtml::pixel_t)(Y + Self->ScrollY), (litehtml::pixel_t)X, (litehtml::pixel_t)Y, nullptr);

    for (; Element; Element = Element->parent())
    {
        const char *Tag = Element->get_tagName();
        auto Field = std::dynamic_pointer_cast<Control>(Element);

        if (Field && Field->IsButton())
        {
            if (Field->Type() == "submit" || Field->Type() == "image")
                Submit(Self, Element);
            return;
        }
        if (Tag && strcmp(Tag, "button") == 0)
        {
            std::string Kind = Element->get_attr("type", "submit");
            if (Kind == "submit" && !Element->get_attr("disabled"))
                Submit(Self, Element);
            return;
        }
        if (Tag && strcmp(Tag, "label") == 0)
        {
            const char *For = Element->get_attr("for");
            for (const auto &Candidate : Self->Controls)
            {
                const char *Id = Candidate->get_attr("id");
                bool Inside = !For && Ancestor(Candidate, "label") == Element;
                if (Candidate->Window && ((For && Id && strcmp(For, Id) == 0) || Inside) &&
                    (Candidate->Type() == "checkbox" || Candidate->Type() == "radio"))
                {
                    SendMessageW(Candidate->Window, BM_CLICK, 0, 0);
                    return;
                }
            }
            return;
        }
    }
}

static void
OnAnchor(void *Context, const std::string &Url)
{
    View *Self = (View *)Context;
    std::string Target = Self->Owner->Resolve(Url);

    if (Self->Host.Navigate && (Target.compare(0, 7, "http://") == 0 || Target.compare(0, 8, "https://") == 0))
        Self->Host.Navigate(Self->Host.Context, Target.c_str(), nullptr, 0, nullptr);
}

static void
Adopt(View *Self, LoadResult *Result)
{
    for (const auto &Field : Self->Controls)
    {
        if (Field->Window)
            DestroyWindow(Field->Window);
    }
    Self->Controls.clear();
    Self->OptionValues.clear();
    Self->Document.reset();
    Self->Owner.reset();

    Self->Owner = std::move(Result->Owner);
    Self->Document = std::move(Result->Document);
    Self->ScrollY = 0;
    if (Self->Document)
    {
        Self->Owner->SetClickHandler(OnAnchor, Self);
        CollectControls(Self->Document->root(), Self->Controls);
        Self->OptionValues.resize(Self->Controls.size());
    }
    Layout(Self);
    InvalidateRect(Self->Hwnd, nullptr, TRUE);
}

static LRESULT CALLBACK
ViewProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    View *Self = (View *)GetWindowLongPtrW(Hwnd, GWLP_USERDATA);
    litehtml::position::vector Boxes;

    switch (Message)
    {
        case WM_NCCREATE:
        {
            CREATESTRUCTW *Create = (CREATESTRUCTW *)LParam;
            Self = new View;
            Self->Hwnd = Hwnd;
            Self->Host = *(const LNTWEB_HOST *)Create->lpCreateParams;
            HDC Screen = GetDC(nullptr);
            Self->Dpi = GetDeviceCaps(Screen, LOGPIXELSY);
            ReleaseDC(nullptr, Screen);
            SetWindowLongPtrW(Hwnd, GWLP_USERDATA, (LONG_PTR)Self);
            break;
        }

        case WM_NCDESTROY:
            if (Self)
            {
                Self->Document.reset();
                Self->Owner.reset();
                delete Self;
                SetWindowLongPtrW(Hwnd, GWLP_USERDATA, 0);
            }
            break;

        case WM_LNTWEB_READY:
        {
            std::unique_ptr<LoadResult> Result((LoadResult *)LParam);
            if (Self && Result->Sequence == Self->Sequence)
                Adopt(Self, Result.get());
            return 0;
        }

        case WM_LNTWEB_SUBMIT:
        {
            size_t Index = (size_t)WParam - LNTWEB_CONTROL_ID;
            if (Self && Index < Self->Controls.size())
            {
                std::shared_ptr<Control> Submitter;
                litehtml::element::ptr Form = Ancestor(Self->Controls[Index], "form");
                for (const auto &Candidate : Self->Controls)
                {
                    if (Candidate->IsButton() && Candidate->Type() == "submit" && Ancestor(Candidate, "form") == Form)
                    {
                        Submitter = Candidate;
                        break;
                    }
                }
                Submit(Self, Submitter ? (litehtml::element::ptr)Submitter : (litehtml::element::ptr)Self->Controls[Index]);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            if (Self)
            {
                Paint(Self);
                return 0;
            }
            break;

        case WM_SIZE:
            if (Self)
            {
                Layout(Self);
                InvalidateRect(Hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_VSCROLL:
            if (Self)
            {
                SCROLLINFO Info = { sizeof(Info), SIF_ALL };
                RECT Client;
                GetScrollInfo(Hwnd, SB_VERT, &Info);
                GetClientRect(Hwnd, &Client);
                switch (LOWORD(WParam))
                {
                    case SB_LINEUP: ScrollTo(Self, Self->ScrollY - LNTWEB_LINE); break;
                    case SB_LINEDOWN: ScrollTo(Self, Self->ScrollY + LNTWEB_LINE); break;
                    case SB_PAGEUP: ScrollTo(Self, Self->ScrollY - Client.bottom); break;
                    case SB_PAGEDOWN: ScrollTo(Self, Self->ScrollY + Client.bottom); break;
                    case SB_TOP: ScrollTo(Self, 0); break;
                    case SB_BOTTOM: ScrollTo(Self, Self->ContentHeight); break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: ScrollTo(Self, Info.nTrackPos); break;
                }
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (Self)
                ScrollTo(Self, Self->ScrollY - MulDiv(GET_WHEEL_DELTA_WPARAM(WParam), 3 * LNTWEB_LINE, WHEEL_DELTA));
            return 0;

        case WM_SETCURSOR:
            if (Self && Self->Owner && LOWORD(LParam) == HTCLIENT)
            {
                SetCursor(Self->Owner->Cursor());
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE:
            if (Self && Self->Document)
            {
                int X = GET_X_LPARAM(LParam), Y = GET_Y_LPARAM(LParam);
                if (!Self->Tracking)
                {
                    TRACKMOUSEEVENT Track = { sizeof(Track), TME_LEAVE, Hwnd, 0 };
                    Self->Tracking = TrackMouseEvent(&Track) != FALSE;
                }
                if (Self->Document->on_mouse_over((litehtml::pixel_t)X, (litehtml::pixel_t)(Y + Self->ScrollY),
                                                  (litehtml::pixel_t)X, (litehtml::pixel_t)Y, Boxes))
                {
                    InvalidateRect(Hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (Self)
            {
                Self->Tracking = false;
                if (Self->Document && Self->Document->on_mouse_leave(Boxes))
                    InvalidateRect(Hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN:
            SetFocus(Hwnd);
            if (Self && Self->Document)
            {
                int X = GET_X_LPARAM(LParam), Y = GET_Y_LPARAM(LParam);
                if (Self->Document->on_lbutton_down((litehtml::pixel_t)X, (litehtml::pixel_t)(Y + Self->ScrollY),
                                                    (litehtml::pixel_t)X, (litehtml::pixel_t)Y, Boxes))
                {
                    InvalidateRect(Hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (Self && Self->Document)
            {
                int X = GET_X_LPARAM(LParam), Y = GET_Y_LPARAM(LParam);
                std::shared_ptr<litehtml::document> Keep = Self->Document;
                if (Keep->on_lbutton_up((litehtml::pixel_t)X, (litehtml::pixel_t)(Y + Self->ScrollY), (litehtml::pixel_t)X,
                                        (litehtml::pixel_t)Y, Boxes))
                {
                    InvalidateRect(Hwnd, nullptr, FALSE);
                }
                if (Keep == Self->Document)
                    ClickAt(Self, X, Y);
            }
            return 0;

        case WM_COMMAND:
        {
            size_t Index = (size_t)LOWORD(WParam) - LNTWEB_CONTROL_ID;
            if (Self && HIWORD(WParam) == BN_CLICKED && Index < Self->Controls.size() &&
                Self->Controls[Index]->Type() == "radio")
            {
                const char *Name = Self->Controls[Index]->get_attr("name", "");
                for (const auto &Other : Self->Controls)
                {
                    if (Other->Window && Other->Type() == "radio" && strcmp(Other->get_attr("name", ""), Name) == 0 &&
                        Ancestor(Other, "form") == Ancestor(Self->Controls[Index], "form"))
                    {
                        SendMessageW(Other->Window, BM_SETCHECK, Other == Self->Controls[Index] ? BST_CHECKED : BST_UNCHECKED,
                                     0);
                    }
                }
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)WParam, RGB(0, 0, 0));
            SetBkColor((HDC)WParam, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

}

using namespace lntweb;

extern "C" BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        Module = Instance;
        DisableThreadLibraryCalls(Instance);
    }
    return TRUE;
}

extern "C" HWND WINAPI
LntWebCreate(HWND Parent, INT Id, const LNTWEB_HOST *Host)
{
    WNDCLASSEXW Class = { sizeof(Class) };

    if (!Host || Host->Size < sizeof(LNTWEB_HOST))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!GetClassInfoExW(Module, LNTWEB_CLASS, &Class))
    {
        Class.cbSize = sizeof(Class);
        Class.lpfnWndProc = ViewProc;
        Class.hInstance = Module;
        Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Class.lpszClassName = LNTWEB_CLASS;
        if (!RegisterClassExW(&Class))
            return nullptr;
    }
    return CreateWindowExW(WS_EX_CONTROLPARENT, LNTWEB_CLASS, nullptr,
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 0, 0, Parent,
                           (HMENU)(INT_PTR)Id, Module, (LPVOID)Host);
}

extern "C" BOOL WINAPI
LntWebLoad(HWND Window, PCSTR Html, SIZE_T Length, PCSTR Url)
{
    View *Self = (View *)GetWindowLongPtrW(Window, GWLP_USERDATA);
    RECT Client;

    if (!Self || !Html || !Url)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GetClientRect(Window, &Client);
    LoadJob *Job = new (std::nothrow) LoadJob;
    if (!Job)
        return FALSE;
    Job->Hwnd = Window;
    Job->Sequence = InterlockedIncrement(&Self->Sequence);
    Job->Html.assign(Html, Length);
    Job->Url = Url;
    Job->Host = Self->Host;
    Job->Width = std::max<int>(Client.right - GetSystemMetrics(SM_CXVSCROLL), 1);
    Job->Height = std::max<int>(Client.bottom, 1);
    Job->Dpi = Self->Dpi;

    HANDLE Thread = CreateThread(nullptr, 0, LoadThread, Job, 0, nullptr);
    if (!Thread)
    {
        delete Job;
        return FALSE;
    }
    CloseHandle(Thread);
    return TRUE;
}
