/*
 * PROJECT:     LiberNT WMI MOF compiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared definitions of the MOF parser and the repository store phase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define COBJMACROS
#define CONST_VTABLE
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <wbemcli.h>

typedef enum _MOF_VALUE_KIND
{
    MofValueNone,
    MofValueNull,
    MofValueInteger,
    MofValueReal,
    MofValueString,
    MofValueBoolean,
    MofValueChar,
    MofValueAlias,
    MofValueArray
} MOF_VALUE_KIND;

typedef struct _MOF_VALUE
{
    MOF_VALUE_KIND Kind;
    BOOL Negative;
    ULONGLONG Integer;
    double Real;
    BOOL Boolean;
    WCHAR *String;
    struct _MOF_VALUE *Items;
    ULONG Count;
} MOF_VALUE;

typedef struct _MOF_QUALIFIER
{
    WCHAR *Name;
    MOF_VALUE Value;
    LONG Flavor;
} MOF_QUALIFIER;

typedef struct _MOF_QUALIFIER_LIST
{
    MOF_QUALIFIER *Items;
    ULONG Count;
} MOF_QUALIFIER_LIST;

typedef struct _MOF_PROPERTY
{
    MOF_QUALIFIER_LIST Qualifiers;
    CIMTYPE Type;
    WCHAR *ReferenceClass;
    WCHAR *ObjectClass;
    WCHAR *Name;
    MOF_VALUE Value;
} MOF_PROPERTY;

typedef struct _MOF_METHOD
{
    MOF_QUALIFIER_LIST Qualifiers;
    CIMTYPE ReturnType;
    WCHAR *Name;
    MOF_PROPERTY *Parameters;
    ULONG ParameterCount;
} MOF_METHOD;

typedef enum _MOF_OBJECT_KIND
{
    MofObjectPragma,
    MofObjectClass,
    MofObjectInstance,
    MofObjectQualifierDeclaration
} MOF_OBJECT_KIND;

typedef enum _MOF_PRAGMA_KIND
{
    MofPragmaNamespace,
    MofPragmaDeleteInstance,
    MofPragmaDeleteClass,
    MofPragmaClassFlags,
    MofPragmaInstanceFlags,
    MofPragmaAutoRecover,
    MofPragmaOther
} MOF_PRAGMA_KIND;

typedef struct _MOF_OBJECT
{
    MOF_OBJECT_KIND Kind;
    LONG Number;
    LONG FirstLine;
    LONG LastLine;
    MOF_QUALIFIER_LIST Qualifiers;
    WCHAR *Name;
    WCHAR *Super;
    WCHAR *Alias;
    MOF_PROPERTY *Properties;
    ULONG PropertyCount;
    MOF_METHOD *Methods;
    ULONG MethodCount;
    MOF_PRAGMA_KIND Pragma;
    WCHAR *PragmaArgument;
    BOOL PragmaFail;
    LONG PragmaFlags;
    struct _MOF_OBJECT *Next;
} MOF_OBJECT;

typedef struct _MOF_ARENA_BLOCK
{
    struct _MOF_ARENA_BLOCK *Next;
    SIZE_T Used;
    SIZE_T Size;
} MOF_ARENA_BLOCK;

typedef struct _MOF_UNIT
{
    MOF_ARENA_BLOCK *Arena;
    MOF_OBJECT *First;
    MOF_OBJECT *Last;
    LONG ObjectCount;
    BOOL HasAutoRecover;
} MOF_UNIT;

void *MofAlloc(MOF_UNIT *Unit, SIZE_T Size);
void *MofAllocArray(MOF_UNIT *Unit, SIZE_T Count, SIZE_T ElementSize);
WCHAR *MofStrDup(MOF_UNIT *Unit, const WCHAR *Text, SIZE_T Length);
void MofFreeUnit(MOF_UNIT *Unit);

HRESULT MofDecodeText(const BYTE *Data, SIZE_T Size, BOOL AcceptUnicode, WCHAR **Text);
HRESULT MofParse(MOF_UNIT *Unit, const WCHAR *Text, WBEM_COMPILE_STATUS_INFO *Info);
HRESULT MofParseFlags(const WCHAR *Text, BOOL Instance, LONG *Flags);
HRESULT MofStore(MOF_UNIT *Unit, const WCHAR *DefaultNamespace, LONG ClassFlags, LONG InstanceFlags,
                 WBEM_COMPILE_STATUS_INFO *Info);
