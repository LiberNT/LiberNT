/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Plain page view used when the web view is not installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <libernt/lntweb.h>

#include "html.h"
#include "plainview.h"
#include "resource.h"

#define PV_CLASS_NAME L"LiberNTPlainView"
#define PV_URL_LENGTH 4096
#define PV_TEXT_LENGTH 256
#define PV_ID_BASE 1000
#define PV_TIMER_REFRESH 1
#define PV_MAX_REFRESH_SECONDS 3600
#define PV_MS_PER_SECOND 1000
#define PV_COMBO_DROP_LINES 8
#define PV_TEXTAREA_LINES 4
#define WM_PV_SUBMIT (WM_APP + 1)

typedef enum _PV_ROLE
{
    PvRoleText,
    PvRoleLink,
    PvRoleEdit,
    PvRoleCheck,
    PvRoleRadio,
    PvRoleCombo,
    PvRoleTextArea,
    PvRoleButton
} PV_ROLE;

typedef struct _PV_CONTROL
{
    HWND Hwnd;
    size_t Item;
    PV_ROLE Role;
    int Heading;
} PV_CONTROL;

typedef struct _PV_VIEW
{
    HWND Hwnd;
    LNTWEB_HOST Host;
    HTML_DOCUMENT Page;
    WCHAR PageUrl[PV_URL_LENGTH];
    WCHAR BaseUrl[PV_URL_LENGTH];
    char *RefreshUrl;
    PV_CONTROL *Controls;
    size_t ControlCount;
    size_t ControlCapacity;
    size_t Hover;
    int ScrollPos;
    int ContentHeight;
    int Dpi;
    HFONT TextFont;
    HFONT LinkFont;
    HFONT HeadingFont[3];
} PV_VIEW;

static int
Scale(const PV_VIEW *View, int Value)
{
    return MulDiv(Value, View->Dpi, 96);
}

static PWSTR
Utf8ToWide(const char *Text)
{
    int Length;
    PWSTR Wide;

    if (!Text)
        Text = "";
    Length = MultiByteToWideChar(CP_UTF8, 0, Text, -1, NULL, 0);
    if (Length <= 0)
        return NULL;
    Wide = malloc(Length * sizeof(WCHAR));
    if (Wide && !MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, Length))
    {
        free(Wide);
        return NULL;
    }
    return Wide;
}

static char *
WideToUtf8(PCWSTR Text)
{
    int Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, NULL, 0, NULL, NULL);
    char *Utf8;

    if (Length <= 0)
        return NULL;
    Utf8 = malloc(Length);
    if (Utf8 && !WideCharToMultiByte(CP_UTF8, 0, Text, -1, Utf8, Length, NULL, NULL))
    {
        free(Utf8);
        return NULL;
    }
    return Utf8;
}

static PV_VIEW *
GetView(HWND Hwnd)
{
    return (PV_VIEW *)GetWindowLongPtrW(Hwnd, GWLP_USERDATA);
}

static char *
ResolveUrl(PV_VIEW *View, const char *Relative)
{
    WCHAR Resolved[PV_URL_LENGTH];
    PWSTR Wide = Utf8ToWide(Relative);
    DWORD Size = ARRAYSIZE(Resolved);
    HRESULT Result;

    if (!Wide)
        return NULL;
    Result = UrlCombineW(View->BaseUrl[0] ? View->BaseUrl : View->PageUrl, Wide, Resolved, &Size,
                         URL_ESCAPE_SPACES_ONLY);
    free(Wide);
    if (FAILED(Result) || (StrCmpNIW(Resolved, L"http://", 7) != 0 && StrCmpNIW(Resolved, L"https://", 8) != 0))
        return NULL;
    return WideToUtf8(Resolved);
}

static BOOL
IsNavigableHref(const char *Href)
{
    if (!Href || !Href[0] || Href[0] == '#')
        return FALSE;
    return StrCmpNIA(Href, "javascript:", 11) != 0 && StrCmpNIA(Href, "mailto:", 7) != 0 &&
           StrCmpNIA(Href, "tel:", 4) != 0 && StrCmpNIA(Href, "data:", 5) != 0;
}

static BOOL
IsTextInput(const HTML_ITEM *Item)
{
    return Item->Kind == HtmlItemInput && Item->Type && strcmp(Item->Type, "checkbox") != 0 &&
           strcmp(Item->Type, "radio") != 0 && strcmp(Item->Type, "submit") != 0 && strcmp(Item->Type, "image") != 0 &&
           strcmp(Item->Type, "button") != 0;
}

static VOID
SetHover(PV_VIEW *View, size_t Index)
{
    char *Url = NULL;

    if (Index == View->Hover)
        return;
    View->Hover = Index;
    if (Index < View->ControlCount && View->Controls[Index].Item < View->Page.ItemCount)
        Url = ResolveUrl(View, View->Page.Items[View->Controls[Index].Item].Href);
    if (View->Host.Hover)
        View->Host.Hover(View->Host.Context, Url);
    free(Url);
}

static VOID
ClearPage(PV_VIEW *View)
{
    size_t Index;

    KillTimer(View->Hwnd, PV_TIMER_REFRESH);
    SetHover(View, (size_t)-1);
    for (Index = 0; Index < View->ControlCount; Index++)
        DestroyWindow(View->Controls[Index].Hwnd);
    View->ControlCount = 0;
    HtmlFree(&View->Page);
    free(View->RefreshUrl);
    View->RefreshUrl = NULL;
    View->BaseUrl[0] = 0;
    View->ScrollPos = 0;
    View->ContentHeight = 0;
}

static LRESULT CALLBACK
FieldProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR Id, DWORD_PTR Data)
{
    UNREFERENCED_PARAMETER(Id);

    if (Message == WM_KEYDOWN && WParam == VK_RETURN && !(GetWindowLongW(Hwnd, GWL_STYLE) & ES_MULTILINE))
    {
        PostMessageW((HWND)Data, WM_PV_SUBMIT, (WPARAM)GetDlgCtrlID(Hwnd), 0);
        return 0;
    }
    if (Message == WM_GETDLGCODE && !(GetWindowLongW(Hwnd, GWL_STYLE) & ES_MULTILINE))
        return DefSubclassProc(Hwnd, Message, WParam, LParam) | DLGC_WANTALLKEYS;
    return DefSubclassProc(Hwnd, Message, WParam, LParam);
}

static PV_CONTROL *
AddControl(PV_VIEW *View, PCWSTR Class, PCWSTR Text, DWORD Style, size_t Item, PV_ROLE Role, HFONT Font)
{
    PV_CONTROL *Control;
    HWND Hwnd;

    if (View->ControlCount == View->ControlCapacity)
    {
        size_t Capacity = View->ControlCapacity ? View->ControlCapacity * 2 : 32;
        PV_CONTROL *Grown = realloc(View->Controls, Capacity * sizeof(*Grown));

        if (!Grown)
            return NULL;
        View->Controls = Grown;
        View->ControlCapacity = Capacity;
    }
    Hwnd = CreateWindowExW((Role == PvRoleEdit || Role == PvRoleTextArea) ? WS_EX_CLIENTEDGE : 0, Class,
                           Text ? Text : L"", WS_CHILD | WS_VISIBLE | Style, 0, 0, 0, 0, View->Hwnd,
                           (HMENU)(UINT_PTR)(PV_ID_BASE + View->ControlCount), NULL, NULL);
    if (!Hwnd)
        return NULL;
    SendMessageW(Hwnd, WM_SETFONT, (WPARAM)Font, FALSE);
    if (Role == PvRoleEdit)
        SetWindowSubclass(Hwnd, FieldProc, 1, (DWORD_PTR)View->Hwnd);
    Control = &View->Controls[View->ControlCount++];
    Control->Hwnd = Hwnd;
    Control->Item = Item;
    Control->Role = Role;
    Control->Heading = 0;
    return Control;
}

static PWSTR
ItemCaption(const HTML_ITEM *Item)
{
    WCHAR Default[PV_TEXT_LENGTH];

    if (Item->Kind == HtmlItemInput && Item->Value && Item->Value[0] && !Item->Text)
        return Utf8ToWide(Item->Value);
    if (Item->Text && Item->Text[0])
        return Utf8ToWide(Item->Text);
    if (Item->Value && Item->Value[0])
        return Utf8ToWide(Item->Value);
    if (!LoadStringW(GetModuleHandleW(NULL), IDS_SUBMIT, Default, ARRAYSIZE(Default)))
        Default[0] = 0;
    return _wcsdup(Default);
}

static VOID
AddNotice(PV_VIEW *View, UINT Id)
{
    WCHAR Text[PV_TEXT_LENGTH * 2];

    if (LoadStringW(GetModuleHandleW(NULL), Id, Text, ARRAYSIZE(Text)))
        AddControl(View, WC_STATICW, Text, SS_LEFT | SS_NOPREFIX, (size_t)-1, PvRoleText, View->TextFont);
}

static VOID
BuildControls(PV_VIEW *View)
{
    HTML_DOCUMENT *Page = &View->Page;
    size_t Index, Option;

    for (Index = 0; Index < Page->ItemCount; Index++)
    {
        HTML_ITEM *Item = &Page->Items[Index];
        PV_CONTROL *Control = NULL;
        PWSTR Text;

        if (!HtmlIsVisibleItem(Item))
            continue;

        switch (Item->Kind)
        {
            case HtmlItemText:
                Text = Utf8ToWide(Item->Text);
                Control = AddControl(View, WC_STATICW, Text, SS_LEFT | SS_NOPREFIX, Index, PvRoleText,
                                     Item->Heading ? View->HeadingFont[min(Item->Heading, 3) - 1] : View->TextFont);
                if (Control)
                    Control->Heading = Item->Heading;
                free(Text);
                break;

            case HtmlItemLink:
                Text = Utf8ToWide(Item->Text);
                Control = AddControl(View, WC_BUTTONW, Text, WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, Index, PvRoleLink,
                                     View->LinkFont);
                if (Control && !IsNavigableHref(Item->Href))
                    EnableWindow(Control->Hwnd, FALSE);
                free(Text);
                break;

            case HtmlItemInput:
                if (strcmp(Item->Type, "checkbox") == 0 || strcmp(Item->Type, "radio") == 0)
                {
                    BOOL Radio = Item->Type[0] == 'r';

                    Text = Utf8ToWide(Item->Text);
                    Control = AddControl(View, WC_BUTTONW, Text,
                                         WS_TABSTOP | BS_MULTILINE | BS_NOTIFY | (Radio ? BS_RADIOBUTTON : BS_AUTOCHECKBOX),
                                         Index, Radio ? PvRoleRadio : PvRoleCheck, View->TextFont);
                    if (Control)
                        SendMessageW(Control->Hwnd, BM_SETCHECK, Item->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
                    free(Text);
                }
                else if (IsTextInput(Item))
                {
                    DWORD Style = WS_TABSTOP | ES_AUTOHSCROLL;

                    if (strcmp(Item->Type, "password") == 0)
                        Style |= ES_PASSWORD;
                    Text = Utf8ToWide(Item->Value);
                    Control = AddControl(View, WC_EDITW, Text, Style, Index, PvRoleEdit, View->TextFont);
                    free(Text);
                    if (Control && Item->Placeholder)
                    {
                        PWSTR Placeholder = Utf8ToWide(Item->Placeholder);

                        if (Placeholder)
                            SendMessageW(Control->Hwnd, EM_SETCUEBANNER, FALSE, (LPARAM)Placeholder);
                        free(Placeholder);
                    }
                }
                else
                {
                    Text = ItemCaption(Item);
                    Control = AddControl(View, WC_BUTTONW, Text, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, Index,
                                         PvRoleButton, View->TextFont);
                    if (Control && !HtmlIsSubmitItem(Item))
                        EnableWindow(Control->Hwnd, FALSE);
                    free(Text);
                }
                break;

            case HtmlItemButton:
                Text = ItemCaption(Item);
                Control = AddControl(View, WC_BUTTONW, Text, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, Index, PvRoleButton,
                                     View->TextFont);
                if (Control && !HtmlIsSubmitItem(Item))
                    EnableWindow(Control->Hwnd, FALSE);
                free(Text);
                break;

            case HtmlItemSelect:
                Control = AddControl(View, WC_COMBOBOXW, NULL, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, Index,
                                     PvRoleCombo, View->TextFont);
                if (Control)
                {
                    int Selected = 0;

                    for (Option = 0; Option < Item->OptionCount; Option++)
                    {
                        Text = Utf8ToWide(Item->Options[Option].Text);
                        SendMessageW(Control->Hwnd, CB_ADDSTRING, 0, (LPARAM)(Text ? Text : L""));
                        free(Text);
                        if (Item->Options[Option].Selected)
                            Selected = (int)Option;
                    }
                    SendMessageW(Control->Hwnd, CB_SETCURSEL, Selected, 0);
                }
                break;

            case HtmlItemTextArea:
                Text = Utf8ToWide(Item->Value);
                Control = AddControl(View, WC_EDITW, Text,
                                     WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, Index,
                                     PvRoleTextArea, View->TextFont);
                free(Text);
                break;
        }

        if (Control && Item->Disabled)
            EnableWindow(Control->Hwnd, FALSE);
    }
}

static int
TextHeight(HDC Hdc, HFONT Font, PCWSTR Text, int Width, int *TextWidth)
{
    RECT Rect = { 0, 0, max(Width, 1), 0 };
    HGDIOBJ Old = SelectObject(Hdc, Font);

    DrawTextW(Hdc, Text[0] ? Text : L" ", -1, &Rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(Hdc, Old);
    if (TextWidth)
        *TextWidth = Rect.right - Rect.left;
    return Rect.bottom - Rect.top;
}

static VOID
UpdateScrollBar(PV_VIEW *View)
{
    SCROLLINFO Info;
    RECT Client;

    GetClientRect(View->Hwnd, &Client);
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    Info.nMax = max(View->ContentHeight - 1, 0);
    Info.nPage = Client.bottom;
    Info.nPos = View->ScrollPos;
    SetScrollInfo(View->Hwnd, SB_VERT, &Info, TRUE);
}

static VOID
LayoutPage(PV_VIEW *View)
{
    RECT Client;
    HDC Hdc;
    TEXTMETRICW Metrics;
    int Margin = Scale(View, 16), Gap = Scale(View, 8), Width, Y, LineHeight, Check = GetSystemMetrics(SM_CXMENUCHECK);
    size_t Index;
    HDWP Positions;

    GetClientRect(View->Hwnd, &Client);
    Width = Client.right - 2 * Margin;
    if (!(GetWindowLongPtrW(View->Hwnd, GWL_STYLE) & WS_VSCROLL))
        Width -= GetSystemMetrics(SM_CXVSCROLL);
    Width = max(Width, Scale(View, 80));

    Hdc = GetDC(View->Hwnd);
    if (!Hdc)
        return;
    SelectObject(Hdc, View->TextFont);
    GetTextMetricsW(Hdc, &Metrics);
    LineHeight = Metrics.tmHeight;

    Y = Margin;
    Positions = BeginDeferWindowPos((int)View->ControlCount);
    for (Index = 0; Index < View->ControlCount; Index++)
    {
        PV_CONTROL *Control = &View->Controls[Index];
        int TextLength = GetWindowTextLengthW(Control->Hwnd);
        PWSTR Text = malloc((max(TextLength, 0) + 1) * sizeof(WCHAR));
        int ControlWidth = Width, Height, TextWidth = 0, Extra = 0;

        if (!Text)
            continue;
        GetWindowTextW(Control->Hwnd, Text, TextLength + 1);
        switch (Control->Role)
        {
            case PvRoleText:
                Height = TextHeight(Hdc, (HFONT)SendMessageW(Control->Hwnd, WM_GETFONT, 0, 0), Text, Width, NULL);
                if (Control->Heading)
                    Extra = Gap / 2;
                break;

            case PvRoleLink:
                Height = TextHeight(Hdc, View->LinkFont, Text, Width - Scale(View, 4), &TextWidth) + Scale(View, 4);
                ControlWidth = min(TextWidth + Scale(View, 6), Width);
                break;

            case PvRoleCheck:
            case PvRoleRadio:
                Height = max(TextHeight(Hdc, View->TextFont, Text, Width - Check - Scale(View, 8), NULL), Check) +
                         Scale(View, 2);
                break;

            case PvRoleButton:
                TextHeight(Hdc, View->TextFont, Text, Width, &TextWidth);
                ControlWidth = min(max(TextWidth + Scale(View, 32), Scale(View, 96)), Width);
                Height = LineHeight + Scale(View, 12);
                break;

            case PvRoleTextArea:
                Height = LineHeight * PV_TEXTAREA_LINES + Scale(View, 8);
                break;

            default:
                Height = LineHeight + Scale(View, 10);
                break;
        }
        free(Text);

        Y += Extra;
        if (Positions)
        {
            Positions = DeferWindowPos(Positions, Control->Hwnd, NULL, Margin, Y - View->ScrollPos, ControlWidth,
                                       Control->Role == PvRoleCombo ? Height + LineHeight * PV_COMBO_DROP_LINES : Height,
                                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Y += Height + Gap + Extra;
    }
    if (Positions)
        EndDeferWindowPos(Positions);
    ReleaseDC(View->Hwnd, Hdc);

    View->ContentHeight = Y + Margin;
    View->ScrollPos = max(0, min(View->ScrollPos, View->ContentHeight - Client.bottom));
    UpdateScrollBar(View);
}

static VOID
ScrollPageTo(PV_VIEW *View, int Position)
{
    RECT Client;
    int Old = View->ScrollPos;

    GetClientRect(View->Hwnd, &Client);
    Position = max(0, min(Position, View->ContentHeight - Client.bottom));
    if (Position == Old)
        return;
    View->ScrollPos = Position;
    ScrollWindowEx(View->Hwnd, 0, Old - Position, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateScrollBar(View);
}

static VOID
EnsureVisible(PV_VIEW *View, HWND Control)
{
    RECT Rect, Client;

    GetWindowRect(Control, &Rect);
    MapWindowPoints(NULL, View->Hwnd, (POINT *)&Rect, 2);
    GetClientRect(View->Hwnd, &Client);
    if (Rect.top < 0)
        ScrollPageTo(View, View->ScrollPos + Rect.top - Scale(View, 8));
    else if (Rect.bottom > Client.bottom)
        ScrollPageTo(View, View->ScrollPos + Rect.bottom - Client.bottom + Scale(View, 8));
}

static VOID
StoreControlValues(PV_VIEW *View)
{
    size_t Index;

    for (Index = 0; Index < View->ControlCount; Index++)
    {
        PV_CONTROL *Control = &View->Controls[Index];
        HTML_ITEM *Item;

        if (Control->Item >= View->Page.ItemCount)
            continue;
        Item = &View->Page.Items[Control->Item];
        switch (Control->Role)
        {
            case PvRoleEdit:
            case PvRoleTextArea:
            {
                int Length = GetWindowTextLengthW(Control->Hwnd);
                PWSTR Text = malloc((max(Length, 0) + 1) * sizeof(WCHAR));
                char *Utf8;

                if (!Text)
                    break;
                GetWindowTextW(Control->Hwnd, Text, Length + 1);
                Utf8 = WideToUtf8(Text);
                if (Utf8)
                    HtmlSetValue(Item, Utf8);
                free(Utf8);
                free(Text);
                break;
            }

            case PvRoleCheck:
            case PvRoleRadio:
                Item->Checked = SendMessageW(Control->Hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                break;

            case PvRoleCombo:
            {
                LRESULT Selected = SendMessageW(Control->Hwnd, CB_GETCURSEL, 0, 0);
                size_t Option;

                for (Option = 0; Option < Item->OptionCount; Option++)
                    Item->Options[Option].Selected = (LRESULT)Option == Selected;
                break;
            }

            default:
                break;
        }
    }
}

static VOID
Navigate(PV_VIEW *View, const char *Url, const char *Body, size_t Length, const char *ContentType)
{
    if (View->Host.Navigate)
        View->Host.Navigate(View->Host.Context, Url, Body, Length, ContentType);
}

static VOID
SubmitForm(PV_VIEW *View, size_t Form, size_t Submitter)
{
    char Boundary[64], ContentType[PV_TEXT_LENGTH], *Action, *Body, *Url;
    size_t Length = 0, Size;
    BOOL Multipart;

    if (Form >= View->Page.FormCount)
        return;
    StoreControlValues(View);
    Action = ResolveUrl(View, View->Page.Forms[Form].Action ? View->Page.Forms[Form].Action : "");
    if (!Action)
        return;

    Multipart = HtmlFormIsMultipart(&View->Page, Form);
    StringCchPrintfA(Boundary, ARRAYSIZE(Boundary), "----LiberNTFormBoundary%08lX%08lX", GetTickCount(),
                     GetCurrentProcessId());
    Body = HtmlEncodeForm(&View->Page, Form, Submitter, Multipart ? Boundary : NULL, &Length);
    if (!Body)
    {
        free(Action);
        return;
    }

    if (HtmlFormIsPost(&View->Page, Form))
    {
        if (Multipart)
            StringCchPrintfA(ContentType, ARRAYSIZE(ContentType), "multipart/form-data; boundary=%s", Boundary);
        else
            StringCchCopyA(ContentType, ARRAYSIZE(ContentType), "application/x-www-form-urlencoded");
        Navigate(View, Action, Body, Length, ContentType);
    }
    else
    {
        Action[strcspn(Action, "?#")] = 0;
        Size = strlen(Action) + Length + 2;
        Url = malloc(Size);
        if (Url && SUCCEEDED(StringCchPrintfA(Url, Size, "%s?%s", Action, Body)))
            Navigate(View, Url, NULL, 0, NULL);
        free(Url);
    }
    free(Body);
    free(Action);
}

static PV_CONTROL *
ControlFromId(PV_VIEW *View, UINT Id)
{
    if (Id < PV_ID_BASE || Id - PV_ID_BASE >= View->ControlCount)
        return NULL;
    return &View->Controls[Id - PV_ID_BASE];
}

static VOID
SubmitFieldForm(PV_VIEW *View, PV_CONTROL *Control)
{
    size_t Index;
    int Form;

    if (!Control || Control->Item >= View->Page.ItemCount)
        return;
    Form = View->Page.Items[Control->Item].Form;
    if (Form < 0)
        return;
    for (Index = 0; Index < View->Page.ItemCount; Index++)
    {
        const HTML_ITEM *Item = &View->Page.Items[Index];

        if (Item->Form == Form && HtmlIsSubmitItem(Item) && !Item->Disabled)
        {
            SubmitForm(View, (size_t)Form, Index);
            return;
        }
    }
    SubmitForm(View, (size_t)Form, (size_t)-1);
}

static VOID
OnControlCommand(PV_VIEW *View, PV_CONTROL *Control, UINT Code)
{
    HTML_ITEM *Item = Control->Item < View->Page.ItemCount ? &View->Page.Items[Control->Item] : NULL;
    size_t Index;

    if ((Control->Role == PvRoleCombo && Code == CBN_SETFOCUS) ||
        ((Control->Role == PvRoleEdit || Control->Role == PvRoleTextArea) && Code == EN_SETFOCUS) ||
        ((Control->Role == PvRoleLink || Control->Role == PvRoleCheck || Control->Role == PvRoleRadio ||
          Control->Role == PvRoleButton) && Code == BN_SETFOCUS))
    {
        EnsureVisible(View, Control->Hwnd);
        return;
    }
    if (!Item || Code != BN_CLICKED)
        return;

    switch (Control->Role)
    {
        case PvRoleLink:
        {
            char *Target = IsNavigableHref(Item->Href) ? ResolveUrl(View, Item->Href) : NULL;

            if (Target)
                Navigate(View, Target, NULL, 0, NULL);
            free(Target);
            break;
        }

        case PvRoleRadio:
            for (Index = 0; Index < View->ControlCount; Index++)
            {
                PV_CONTROL *Other = &View->Controls[Index];
                HTML_ITEM *OtherItem;

                if (Other->Role != PvRoleRadio || Other->Item >= View->Page.ItemCount)
                    continue;
                OtherItem = &View->Page.Items[Other->Item];
                if (Other == Control || (OtherItem->Form == Item->Form && Item->Name && OtherItem->Name &&
                                         strcmp(OtherItem->Name, Item->Name) == 0))
                {
                    SendMessageW(Other->Hwnd, BM_SETCHECK, Other == Control ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }
            break;

        case PvRoleButton:
            if (HtmlIsSubmitItem(Item) && Item->Form >= 0)
                SubmitForm(View, (size_t)Item->Form, Control->Item);
            break;

        default:
            break;
    }
}

static VOID
DrawLink(PV_VIEW *View, const DRAWITEMSTRUCT *Draw)
{
    WCHAR Text[1024];
    RECT Rect = Draw->rcItem;
    HGDIOBJ Old;
    BOOL Disabled = (Draw->itemState & ODS_DISABLED) != 0;

    FillRect(Draw->hDC, &Draw->rcItem, GetSysColorBrush(COLOR_WINDOW));
    GetWindowTextW(Draw->hwndItem, Text, ARRAYSIZE(Text));
    Old = SelectObject(Draw->hDC, View->LinkFont);
    SetBkMode(Draw->hDC, TRANSPARENT);
    SetTextColor(Draw->hDC, GetSysColor(Disabled ? COLOR_GRAYTEXT : COLOR_HOTLIGHT));
    InflateRect(&Rect, -Scale(View, 2), -Scale(View, 2));
    DrawTextW(Draw->hDC, Text, -1, &Rect, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(Draw->hDC, Old);
    if ((Draw->itemState & ODS_FOCUS) && !(Draw->itemState & ODS_NOFOCUSRECT))
        DrawFocusRect(Draw->hDC, &Draw->rcItem);
}

static VOID
CreateFonts(PV_VIEW *View)
{
    NONCLIENTMETRICSW Metrics;
    LOGFONTW Font;
    static const int HeadingPercent[3] = { 160, 130, 110 };
    int Index;

    ZeroMemory(&Metrics, sizeof(Metrics));
    Metrics.cbSize = sizeof(Metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0);
    Font = Metrics.lfMessageFont;
    View->TextFont = CreateFontIndirectW(&Font);
    Font.lfUnderline = TRUE;
    View->LinkFont = CreateFontIndirectW(&Font);
    for (Index = 0; Index < 3; Index++)
    {
        Font = Metrics.lfMessageFont;
        Font.lfHeight = MulDiv(Font.lfHeight, HeadingPercent[Index], 100);
        Font.lfWeight = FW_SEMIBOLD;
        View->HeadingFont[Index] = CreateFontIndirectW(&Font);
    }
}

static VOID
DestroyView(PV_VIEW *View)
{
    int Index;

    ClearPage(View);
    free(View->Controls);
    DeleteObject(View->TextFont);
    DeleteObject(View->LinkFont);
    for (Index = 0; Index < 3; Index++)
        DeleteObject(View->HeadingFont[Index]);
    free(View);
}

static LRESULT CALLBACK
ViewProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    PV_VIEW *View = GetView(Hwnd);

    if (!View)
        return DefWindowProcW(Hwnd, Message, WParam, LParam);

    switch (Message)
    {
        case WM_COMMAND:
        {
            PV_CONTROL *Control = ControlFromId(View, LOWORD(WParam));

            if (Control && (HWND)LParam == Control->Hwnd)
                OnControlCommand(View, Control, HIWORD(WParam));
            return 0;
        }

        case WM_PV_SUBMIT:
            SubmitFieldForm(View, ControlFromId(View, (UINT)WParam));
            return 0;

        case WM_DRAWITEM:
            DrawLink(View, (const DRAWITEMSTRUCT *)LParam);
            return TRUE;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor((HDC)WParam, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)WParam, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

        case WM_SETCURSOR:
        {
            PV_CONTROL *Control = (HWND)WParam != Hwnd ? ControlFromId(View, GetDlgCtrlID((HWND)WParam)) : NULL;

            if (Control && Control->Role == PvRoleLink && IsWindowEnabled(Control->Hwnd))
            {
                SetHover(View, (size_t)(Control - View->Controls));
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            SetHover(View, (size_t)-1);
            break;
        }

        case WM_TIMER:
            if (WParam == PV_TIMER_REFRESH)
            {
                KillTimer(Hwnd, PV_TIMER_REFRESH);
                if (View->RefreshUrl)
                    Navigate(View, View->RefreshUrl, NULL, 0, NULL);
            }
            return 0;

        case WM_VSCROLL:
        {
            SCROLLINFO Info;
            RECT Client;
            int Line = Scale(View, 24);

            GetClientRect(Hwnd, &Client);
            ZeroMemory(&Info, sizeof(Info));
            Info.cbSize = sizeof(Info);
            Info.fMask = SIF_ALL;
            GetScrollInfo(Hwnd, SB_VERT, &Info);
            switch (LOWORD(WParam))
            {
                case SB_LINEUP: ScrollPageTo(View, View->ScrollPos - Line); break;
                case SB_LINEDOWN: ScrollPageTo(View, View->ScrollPos + Line); break;
                case SB_PAGEUP: ScrollPageTo(View, View->ScrollPos - Client.bottom); break;
                case SB_PAGEDOWN: ScrollPageTo(View, View->ScrollPos + Client.bottom); break;
                case SB_TOP: ScrollPageTo(View, 0); break;
                case SB_BOTTOM: ScrollPageTo(View, View->ContentHeight); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: ScrollPageTo(View, Info.nTrackPos); break;
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            ScrollPageTo(View, View->ScrollPos - MulDiv(GET_WHEEL_DELTA_WPARAM(WParam), Scale(View, 48), WHEEL_DELTA));
            return 0;

        case WM_SIZE:
            LayoutPage(View);
            return 0;

        case WM_ERASEBKGND:
        {
            RECT Client;

            GetClientRect(Hwnd, &Client);
            FillRect((HDC)WParam, &Client, GetSysColorBrush(COLOR_WINDOW));
            return TRUE;
        }

        case WM_NCDESTROY:
            SetWindowLongPtrW(Hwnd, GWLP_USERDATA, 0);
            DestroyView(View);
            break;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

HWND WINAPI
PlainViewCreate(HWND Parent, INT Id, const LNTWEB_HOST *Host)
{
    WNDCLASSEXW Class;
    PV_VIEW *View;
    HDC Screen;
    HWND Hwnd;

    if (!Host || Host->Size < FIELD_OFFSET(LNTWEB_HOST, UserAgent))
        return NULL;
    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = ViewProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.lpszClassName = PV_CLASS_NAME;
    if (!RegisterClassExW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    View = calloc(1, sizeof(*View));
    if (!View)
        return NULL;
    CopyMemory(&View->Host, Host, min(Host->Size, sizeof(View->Host)));
    View->Host.Size = sizeof(View->Host);
    View->Hover = (size_t)-1;
    View->Dpi = 96;
    Screen = GetDC(NULL);
    if (Screen)
    {
        View->Dpi = GetDeviceCaps(Screen, LOGPIXELSY);
        ReleaseDC(NULL, Screen);
    }
    CreateFonts(View);

    Hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, PV_CLASS_NAME, NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
                           0, 0, 0, 0, Parent, (HMENU)(INT_PTR)Id, Class.hInstance, NULL);
    if (!Hwnd)
    {
        DestroyView(View);
        return NULL;
    }
    View->Hwnd = Hwnd;
    SetWindowLongPtrW(Hwnd, GWLP_USERDATA, (LONG_PTR)View);
    return Hwnd;
}

BOOL WINAPI
PlainViewLoad(HWND Hwnd, PCSTR Html, SIZE_T Length, PCSTR Url)
{
    PV_VIEW *View = Hwnd ? GetView(Hwnd) : NULL;
    PWSTR WideUrl, WideBase;
    DWORD Size;

    if (!View || !Html || !Url)
        return FALSE;
    SendMessageW(Hwnd, WM_SETREDRAW, FALSE, 0);
    ClearPage(View);
    WideUrl = Utf8ToWide(Url);
    StringCchCopyW(View->PageUrl, ARRAYSIZE(View->PageUrl), WideUrl ? WideUrl : L"");
    free(WideUrl);

    if (HtmlParse(Html, Length, &View->Page))
    {
        if (View->Page.BaseHref && (WideBase = Utf8ToWide(View->Page.BaseHref)) != NULL)
        {
            Size = ARRAYSIZE(View->BaseUrl);
            if (FAILED(UrlCombineW(View->PageUrl, WideBase, View->BaseUrl, &Size, URL_ESCAPE_SPACES_ONLY)))
                View->BaseUrl[0] = 0;
            free(WideBase);
        }
        if (View->Page.HasScript)
            AddNotice(View, IDS_NO_SCRIPTS);
        BuildControls(View);
        if (View->Page.HasRefresh)
        {
            View->RefreshUrl = ResolveUrl(View, View->Page.RefreshUrl ? View->Page.RefreshUrl : "");
            if (View->RefreshUrl)
            {
                SetTimer(Hwnd, PV_TIMER_REFRESH,
                         (UINT)min(max(View->Page.RefreshSeconds, 0), PV_MAX_REFRESH_SECONDS) * PV_MS_PER_SECOND,
                         NULL);
            }
        }
    }

    LayoutPage(View);
    SendMessageW(Hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(Hwnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    return TRUE;
}
