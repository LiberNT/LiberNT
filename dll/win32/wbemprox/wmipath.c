/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object paths and the WQL select queries of the repository
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <wchar.h>
#include "wmirepo.h"

static void
WmiSkipSpace(const WCHAR **Text)
{
    while (**Text == L' ' || **Text == L'\t' || **Text == L'\r' || **Text == L'\n')
        (*Text)++;
}

static BOOL
WmiIsNameChar(WCHAR Char)
{
    return (Char >= L'a' && Char <= L'z') || (Char >= L'A' && Char <= L'Z') || (Char >= L'0' && Char <= L'9') ||
           Char == L'_' || Char >= 0x80;
}

static WCHAR *
WmiTakeName(const WCHAR **Text)
{
    const WCHAR *Start = *Text;
    WCHAR *Name;

    while (WmiIsNameChar(**Text))
        (*Text)++;
    if (*Text == Start || !(Name = malloc((*Text - Start + 1) * sizeof(WCHAR))))
        return NULL;
    memcpy(Name, Start, (*Text - Start) * sizeof(WCHAR));
    Name[*Text - Start] = 0;
    return Name;
}

/* A literal: a quoted string, a number or TRUE/FALSE/NULL */
static HRESULT
WmiTakeLiteral(const WCHAR **Text, VARIANT *Value)
{
    const WCHAR *Position = *Text;

    VariantInit(Value);
    if (*Position == L'"' || *Position == L'\'')
    {
        WCHAR Quote = *Position++, *Out;
        SIZE_T Length = 0;

        if (!(Out = malloc((wcslen(Position) + 1) * sizeof(WCHAR))))
            return WBEM_E_OUT_OF_MEMORY;
        while (*Position && *Position != Quote)
        {
            if (*Position == L'\\' && Position[1])
                Position++;
            Out[Length++] = *Position++;
        }
        if (*Position != Quote)
        {
            free(Out);
            return WBEM_E_INVALID_QUERY;
        }
        Out[Length] = 0;
        V_VT(Value) = VT_BSTR;
        V_BSTR(Value) = SysAllocString(Out);
        free(Out);
        *Text = Position + 1;
        return V_BSTR(Value) ? S_OK : WBEM_E_OUT_OF_MEMORY;
    }
    if (*Position == L'-' || *Position == L'+' || (*Position >= L'0' && *Position <= L'9'))
    {
        WCHAR *End;
        double Number = wcstod(Position, &End);

        if (End == Position)
            return WBEM_E_INVALID_QUERY;
        if (Number >= -9223372036854775808.0 && Number < 9223372036854775808.0 && Number == (double)(LONGLONG)Number &&
            !wcspbrk(Position, L".eE") )
        {
            V_VT(Value) = VT_I8;
            V_I8(Value) = _wcstoi64(Position, &End, 10);
        }
        else
        {
            V_VT(Value) = VT_R8;
            V_R8(Value) = Number;
        }
        *Text = End;
        return S_OK;
    }
    if (!_wcsnicmp(Position, L"TRUE", 4) && !WmiIsNameChar(Position[4]))
    {
        V_VT(Value) = VT_BOOL;
        V_BOOL(Value) = VARIANT_TRUE;
        *Text = Position + 4;
        return S_OK;
    }
    if (!_wcsnicmp(Position, L"FALSE", 5) && !WmiIsNameChar(Position[5]))
    {
        V_VT(Value) = VT_BOOL;
        V_BOOL(Value) = VARIANT_FALSE;
        *Text = Position + 5;
        return S_OK;
    }
    if (!_wcsnicmp(Position, L"NULL", 4) && !WmiIsNameChar(Position[4]))
    {
        V_VT(Value) = VT_NULL;
        *Text = Position + 4;
        return S_OK;
    }
    return WBEM_E_INVALID_QUERY;
}

void
WmiFreePath(WMI_PATH *Path)
{
    ULONG Index;

    for (Index = 0; Index < Path->KeyCount; Index++)
    {
        free(Path->KeyNames[Index]);
        VariantClear(&Path->KeyValues[Index]);
    }
    free(Path->KeyNames);
    free(Path->KeyValues);
    free(Path->Namespace);
    free(Path->Class);
    memset(Path, 0, sizeof(*Path));
}

HRESULT
WmiParsePath(const WCHAR *Text, WMI_PATH *Path)
{
    const WCHAR *Colon, *Position;
    HRESULT hr = S_OK;

    memset(Path, 0, sizeof(*Path));
    if (!Text)
        return WBEM_E_INVALID_OBJECT_PATH;
    Colon = wcschr(Text, L':');
    if (Colon && (Text[0] == L'\\' || Text[0] == L'/' || !_wcsnicmp(Text, L"root", 4)))
    {
        const WCHAR *Start = Text;

        if (Start[0] == L'\\' || Start[0] == L'/')
        {
            Start += 2;
            while (*Start && *Start != L'\\' && *Start != L'/')
                Start++;
            if (*Start)
                Start++;
        }
        if (!(Path->Namespace = malloc((Colon - Start + 1) * sizeof(WCHAR))))
            return WBEM_E_OUT_OF_MEMORY;
        memcpy(Path->Namespace, Start, (Colon - Start) * sizeof(WCHAR));
        Path->Namespace[Colon - Start] = 0;
        Text = Colon + 1;
    }
    Position = Text;
    if (!(Path->Class = WmiTakeName(&Position)))
    {
        WmiFreePath(Path);
        return WBEM_E_INVALID_OBJECT_PATH;
    }
    if (*Position == L'=' && Position[1] == L'@' && !Position[2])
    {
        Path->Singleton = TRUE;
        return S_OK;
    }
    if (*Position == L'=')
    {
        /* Class=Value names the only key of the class */
        Position++;
        if (!(Path->KeyNames = calloc(1, sizeof(WCHAR *))) || !(Path->KeyValues = calloc(1, sizeof(VARIANT))))
            hr = WBEM_E_OUT_OF_MEMORY;
        if (SUCCEEDED(hr))
        {
            Path->KeyCount = 1;
            hr = WmiTakeLiteral(&Position, &Path->KeyValues[0]);
        }
        if (SUCCEEDED(hr) && *Position)
            hr = WBEM_E_INVALID_OBJECT_PATH;
    }
    else if (*Position == L'.')
    {
        Position++;
        while (SUCCEEDED(hr))
        {
            WCHAR **Names = realloc(Path->KeyNames, (Path->KeyCount + 1) * sizeof(*Names));
            VARIANT *Values = Names ? realloc(Path->KeyValues, (Path->KeyCount + 1) * sizeof(*Values)) : NULL;

            if (Names)
                Path->KeyNames = Names;
            if (!Names || !Values)
            {
                hr = WBEM_E_OUT_OF_MEMORY;
                break;
            }
            Path->KeyValues = Values;
            VariantInit(&Values[Path->KeyCount]);
            Names[Path->KeyCount] = WmiTakeName(&Position);
            Path->KeyCount++;
            if (!Names[Path->KeyCount - 1] || *Position++ != L'=')
            {
                hr = WBEM_E_INVALID_OBJECT_PATH;
                break;
            }
            hr = WmiTakeLiteral(&Position, &Values[Path->KeyCount - 1]);
            if (FAILED(hr))
                break;
            if (!*Position)
                break;
            if (*Position++ != L',')
                hr = WBEM_E_INVALID_OBJECT_PATH;
        }
    }
    else if (*Position)
        hr = WBEM_E_INVALID_OBJECT_PATH;
    if (FAILED(hr))
    {
        WmiFreePath(Path);
        return hr == WBEM_E_INVALID_QUERY ? WBEM_E_INVALID_OBJECT_PATH : hr;
    }
    return S_OK;
}

static int
WmiCompareValues(const VARIANT *Left, const VARIANT *Right, BOOL *Comparable)
{
    VARIANT A, B;
    int Result = 0;

    *Comparable = FALSE;
    if (V_VT(Left) == VT_NULL || V_VT(Right) == VT_NULL || V_VT(Left) == VT_EMPTY || V_VT(Right) == VT_EMPTY)
        return 0;
    VariantInit(&A);
    VariantInit(&B);
    if (V_VT(Left) == VT_BSTR || V_VT(Right) == VT_BSTR)
    {
        if (SUCCEEDED(VariantChangeType(&A, (VARIANT *)Left, 0, VT_BSTR)) &&
            SUCCEEDED(VariantChangeType(&B, (VARIANT *)Right, 0, VT_BSTR)))
        {
            *Comparable = TRUE;
            Result = _wcsicmp(V_BSTR(&A), V_BSTR(&B));
        }
    }
    else if (SUCCEEDED(VariantChangeType(&A, (VARIANT *)Left, 0, VT_R8)) &&
             SUCCEEDED(VariantChangeType(&B, (VARIANT *)Right, 0, VT_R8)))
    {
        *Comparable = TRUE;
        Result = V_R8(&A) < V_R8(&B) ? -1 : V_R8(&A) > V_R8(&B) ? 1 : 0;
    }
    VariantClear(&A);
    VariantClear(&B);
    return Result;
}

BOOL
WmiPathMatchesInstance(const WMI_PATH *Path, IWbemClassObject *Instance)
{
    ULONG Index;

    if (Path->Singleton)
        return TRUE;
    for (Index = 0; Index < Path->KeyCount; Index++)
    {
        VARIANT Value;
        BOOL Comparable, Equal;

        VariantInit(&Value);
        if (Path->KeyNames[Index])
        {
            if (FAILED(IWbemClassObject_Get(Instance, Path->KeyNames[Index], 0, &Value, NULL, NULL)))
                return FALSE;
        }
        else
        {
            SAFEARRAY *Keys = NULL;
            LONG First = 0;
            BSTR Name = NULL;

            if (FAILED(IWbemClassObject_GetNames(Instance, NULL, WBEM_FLAG_KEYS_ONLY, NULL, &Keys)) ||
                FAILED(SafeArrayGetElement(Keys, &First, &Name)) ||
                FAILED(IWbemClassObject_Get(Instance, Name, 0, &Value, NULL, NULL)))
            {
                SysFreeString(Name);
                if (Keys)
                    SafeArrayDestroy(Keys);
                return FALSE;
            }
            SysFreeString(Name);
            SafeArrayDestroy(Keys);
        }
        Equal = WmiCompareValues(&Value, &Path->KeyValues[Index], &Comparable) == 0 && Comparable;
        VariantClear(&Value);
        if (!Equal)
            return FALSE;
    }
    return TRUE;
}

/* Queries */

typedef enum _WMI_NODE_KIND
{
    WmiNodeAnd,
    WmiNodeOr,
    WmiNodeNot,
    WmiNodeCompare,
    WmiNodeIsNull,
    WmiNodeIsNotNull
} WMI_NODE_KIND;

typedef enum _WMI_COMPARE
{
    WmiEqual,
    WmiNotEqual,
    WmiLess,
    WmiLessEqual,
    WmiGreater,
    WmiGreaterEqual
} WMI_COMPARE;

typedef struct _WMI_NODE
{
    WMI_NODE_KIND Kind;
    WMI_COMPARE Compare;
    struct _WMI_NODE *Left;
    struct _WMI_NODE *Right;
    WCHAR *Property;
    VARIANT Value;
    BOOL ValueIsProperty;
    WCHAR *OtherProperty;
} WMI_NODE;

struct _WMI_QUERY
{
    WCHAR *Class;
    WMI_NODE *Where;
};

static void
WmiFreeNode(WMI_NODE *Node)
{
    if (!Node)
        return;
    WmiFreeNode(Node->Left);
    WmiFreeNode(Node->Right);
    free(Node->Property);
    free(Node->OtherProperty);
    VariantClear(&Node->Value);
    free(Node);
}

static BOOL
WmiTakeKeyword(const WCHAR **Text, const WCHAR *Keyword)
{
    SIZE_T Length = wcslen(Keyword);

    WmiSkipSpace(Text);
    if (_wcsnicmp(*Text, Keyword, Length) || WmiIsNameChar((*Text)[Length]))
        return FALSE;
    *Text += Length;
    return TRUE;
}

static HRESULT WmiParseOr(const WCHAR **Text, WMI_NODE **Node);

static WMI_NODE *
WmiNewNode(WMI_NODE_KIND Kind)
{
    WMI_NODE *Node = calloc(1, sizeof(*Node));

    if (Node)
    {
        Node->Kind = Kind;
        VariantInit(&Node->Value);
    }
    return Node;
}

static HRESULT
WmiParseFactor(const WCHAR **Text, WMI_NODE **Result)
{
    WMI_NODE *Node;
    HRESULT hr;

    *Result = NULL;
    WmiSkipSpace(Text);
    if (WmiTakeKeyword(Text, L"NOT"))
    {
        if (!(Node = WmiNewNode(WmiNodeNot)))
            return WBEM_E_OUT_OF_MEMORY;
        hr = WmiParseFactor(Text, &Node->Left);
        if (FAILED(hr))
        {
            WmiFreeNode(Node);
            return hr;
        }
        *Result = Node;
        return S_OK;
    }
    if (**Text == L'(')
    {
        (*Text)++;
        hr = WmiParseOr(Text, Result);
        WmiSkipSpace(Text);
        if (SUCCEEDED(hr) && *(*Text)++ != L')')
            hr = WBEM_E_INVALID_QUERY;
        if (FAILED(hr))
        {
            WmiFreeNode(*Result);
            *Result = NULL;
        }
        return hr;
    }
    if (!(Node = WmiNewNode(WmiNodeCompare)))
        return WBEM_E_OUT_OF_MEMORY;
    if (!(Node->Property = WmiTakeName(Text)))
    {
        WmiFreeNode(Node);
        return WBEM_E_INVALID_QUERY;
    }
    WmiSkipSpace(Text);
    if (WmiTakeKeyword(Text, L"IS"))
    {
        Node->Kind = WmiTakeKeyword(Text, L"NOT") ? WmiNodeIsNotNull : WmiNodeIsNull;
        if (!WmiTakeKeyword(Text, L"NULL"))
        {
            WmiFreeNode(Node);
            return WBEM_E_INVALID_QUERY;
        }
        *Result = Node;
        return S_OK;
    }
    if (!wcsncmp(*Text, L"<>", 2) || !wcsncmp(*Text, L"!=", 2))
        Node->Compare = WmiNotEqual, *Text += 2;
    else if (!wcsncmp(*Text, L"<=", 2))
        Node->Compare = WmiLessEqual, *Text += 2;
    else if (!wcsncmp(*Text, L">=", 2))
        Node->Compare = WmiGreaterEqual, *Text += 2;
    else if (**Text == L'=')
        Node->Compare = WmiEqual, (*Text)++;
    else if (**Text == L'<')
        Node->Compare = WmiLess, (*Text)++;
    else if (**Text == L'>')
        Node->Compare = WmiGreater, (*Text)++;
    else
    {
        WmiFreeNode(Node);
        return WBEM_E_INVALID_QUERY;
    }
    WmiSkipSpace(Text);
    hr = WmiTakeLiteral(Text, &Node->Value);
    if (FAILED(hr))
    {
        if (!(Node->OtherProperty = WmiTakeName(Text)))
        {
            WmiFreeNode(Node);
            return WBEM_E_INVALID_QUERY;
        }
        Node->ValueIsProperty = TRUE;
    }
    *Result = Node;
    return S_OK;
}

static HRESULT
WmiParseAnd(const WCHAR **Text, WMI_NODE **Result)
{
    HRESULT hr = WmiParseFactor(Text, Result);

    while (SUCCEEDED(hr) && WmiTakeKeyword(Text, L"AND"))
    {
        WMI_NODE *Node = WmiNewNode(WmiNodeAnd);

        if (!Node)
            hr = WBEM_E_OUT_OF_MEMORY;
        else
        {
            Node->Left = *Result;
            *Result = Node;
            hr = WmiParseFactor(Text, &Node->Right);
        }
    }
    if (FAILED(hr))
    {
        WmiFreeNode(*Result);
        *Result = NULL;
    }
    return hr;
}

static HRESULT
WmiParseOr(const WCHAR **Text, WMI_NODE **Result)
{
    HRESULT hr = WmiParseAnd(Text, Result);

    while (SUCCEEDED(hr) && WmiTakeKeyword(Text, L"OR"))
    {
        WMI_NODE *Node = WmiNewNode(WmiNodeOr);

        if (!Node)
            hr = WBEM_E_OUT_OF_MEMORY;
        else
        {
            Node->Left = *Result;
            *Result = Node;
            hr = WmiParseAnd(Text, &Node->Right);
        }
    }
    if (FAILED(hr))
    {
        WmiFreeNode(*Result);
        *Result = NULL;
    }
    return hr;
}

HRESULT
WmiParseQuery(const WCHAR *Text, WMI_QUERY **Result)
{
    WMI_QUERY *Query;
    HRESULT hr = S_OK;

    *Result = NULL;
    if (!Text || !WmiTakeKeyword(&Text, L"SELECT"))
        return WBEM_E_INVALID_QUERY;
    /* TODO: the selected property list is not applied; every property of a match is returned */
    WmiSkipSpace(&Text);
    while (*Text && !WmiTakeKeyword(&Text, L"FROM"))
    {
        if (!WmiIsNameChar(*Text) && *Text != L'*' && *Text != L',')
            return WBEM_E_INVALID_QUERY;
        Text++;
        WmiSkipSpace(&Text);
    }
    if (!(Query = calloc(1, sizeof(*Query))))
        return WBEM_E_OUT_OF_MEMORY;
    WmiSkipSpace(&Text);
    if (!(Query->Class = WmiTakeName(&Text)))
        hr = WBEM_E_INVALID_QUERY;
    if (SUCCEEDED(hr) && WmiTakeKeyword(&Text, L"WHERE"))
        hr = WmiParseOr(&Text, &Query->Where);
    WmiSkipSpace(&Text);
    if (SUCCEEDED(hr) && *Text)
        hr = WBEM_E_INVALID_QUERY;
    if (FAILED(hr))
    {
        WmiFreeQuery(Query);
        return hr;
    }
    *Result = Query;
    return S_OK;
}

const WCHAR *
WmiQueryClass(const WMI_QUERY *Query)
{
    return Query->Class;
}

void
WmiFreeQuery(WMI_QUERY *Query)
{
    if (!Query)
        return;
    WmiFreeNode(Query->Where);
    free(Query->Class);
    free(Query);
}

static BOOL
WmiEvaluate(const WMI_NODE *Node, IWbemClassObject *Object)
{
    VARIANT Left, Right;
    BOOL Comparable, Result = FALSE;
    int Order;

    switch (Node->Kind)
    {
    case WmiNodeAnd:
        return WmiEvaluate(Node->Left, Object) && WmiEvaluate(Node->Right, Object);
    case WmiNodeOr:
        return WmiEvaluate(Node->Left, Object) || WmiEvaluate(Node->Right, Object);
    case WmiNodeNot:
        return !WmiEvaluate(Node->Left, Object);
    default:
        break;
    }
    VariantInit(&Left);
    VariantInit(&Right);
    if (FAILED(IWbemClassObject_Get(Object, Node->Property, 0, &Left, NULL, NULL)))
        return FALSE;
    if (Node->Kind == WmiNodeIsNull || Node->Kind == WmiNodeIsNotNull)
    {
        Result = (V_VT(&Left) == VT_NULL || V_VT(&Left) == VT_EMPTY) == (Node->Kind == WmiNodeIsNull);
        VariantClear(&Left);
        return Result;
    }
    if (Node->ValueIsProperty)
    {
        if (FAILED(IWbemClassObject_Get(Object, Node->OtherProperty, 0, &Right, NULL, NULL)))
        {
            VariantClear(&Left);
            return FALSE;
        }
    }
    else
        VariantCopy(&Right, (VARIANT *)&Node->Value);
    Order = WmiCompareValues(&Left, &Right, &Comparable);
    if (Comparable)
    {
        switch (Node->Compare)
        {
        case WmiEqual: Result = Order == 0; break;
        case WmiNotEqual: Result = Order != 0; break;
        case WmiLess: Result = Order < 0; break;
        case WmiLessEqual: Result = Order <= 0; break;
        case WmiGreater: Result = Order > 0; break;
        case WmiGreaterEqual: Result = Order >= 0; break;
        }
    }
    VariantClear(&Left);
    VariantClear(&Right);
    return Result;
}

BOOL
WmiObjectMatches(IWbemClassObject *Object, const WMI_QUERY *Query)
{
    return !Query->Where || WmiEvaluate(Query->Where, Object);
}
