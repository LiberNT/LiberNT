/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Repository objects, store and decoupled provider transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <wbemcli.h>
#include <wbemprov.h>
#include <stdio.h>

typedef struct _WMI_QUERY WMI_QUERY;

typedef struct _WMI_BUFFER
{
    BYTE *Data;
    SIZE_T Size;
    SIZE_T Capacity;
    BOOL Failed;
} WMI_BUFFER;

typedef struct _WMI_READER
{
    const BYTE *Data;
    SIZE_T Size;
    SIZE_T Offset;
    BOOL Failed;
} WMI_READER;

void WmiBufferFree(WMI_BUFFER *Buffer);
void WmiBufferPut(WMI_BUFFER *Buffer, const void *Data, SIZE_T Size);
void WmiBufferPutUlong(WMI_BUFFER *Buffer, ULONG Value);
void WmiBufferPutString(WMI_BUFFER *Buffer, const WCHAR *Text);
BOOL WmiReaderGet(WMI_READER *Reader, void *Data, SIZE_T Size);
ULONG WmiReaderGetUlong(WMI_READER *Reader);
WCHAR *WmiReaderGetString(WMI_READER *Reader);

/* Objects */
HRESULT WmiObjectCreateClass(IWbemClassObject **Object);
BOOL WmiObjectIsOurs(IWbemClassObject *Object);
LONG WmiObjectGenus(IWbemClassObject *Object);
const WCHAR *WmiObjectClassName(IWbemClassObject *Object);
const WCHAR *WmiObjectSuperclass(IWbemClassObject *Object);
BOOL WmiObjectDerivesFrom(IWbemClassObject *Object, const WCHAR *Class);
void WmiObjectSetLocation(IWbemClassObject *Object, const WCHAR *Server, const WCHAR *Namespace);
HRESULT WmiObjectRelativePath(IWbemClassObject *Object, WCHAR **Path);
HRESULT WmiObjectGetClassQualifier(IWbemClassObject *Object, const WCHAR *Name, VARIANT *Value);
HRESULT WmiObjectSerialize(IWbemClassObject *Object, WMI_BUFFER *Buffer);
HRESULT WmiObjectDeserialize(WMI_READER *Reader, IWbemClassObject **Object);
BOOL WmiObjectMatches(IWbemClassObject *Object, const WMI_QUERY *Query);
HRESULT WmiInstanceFromClass(IWbemClassObject *Class, IWbemClassObject **Instance);

/* Paths */
typedef struct _WMI_PATH
{
    WCHAR *Namespace;
    WCHAR *Class;
    ULONG KeyCount;
    WCHAR **KeyNames;
    VARIANT *KeyValues;
    BOOL Singleton;
} WMI_PATH;

HRESULT WmiParsePath(const WCHAR *Text, WMI_PATH *Path);
void WmiFreePath(WMI_PATH *Path);
BOOL WmiPathMatchesInstance(const WMI_PATH *Path, IWbemClassObject *Instance);

/* Queries: SELECT * | properties FROM class [WHERE condition] */
HRESULT WmiParseQuery(const WCHAR *Text, WMI_QUERY **Query);
const WCHAR *WmiQueryClass(const WMI_QUERY *Query);
void WmiFreeQuery(WMI_QUERY *Query);

/* Store */
BOOL WmiRepoIsBuiltinNamespace(const WCHAR *Namespace);
BOOL WmiRepoNamespaceExists(const WCHAR *Namespace);
HRESULT WmiRepoCreateNamespace(const WCHAR *Parent, const WCHAR *Name);
HRESULT WmiRepoDeleteNamespace(const WCHAR *Parent, const WCHAR *Name);
HRESULT WmiRepoEnumNamespaces(const WCHAR *Parent, ULONG *Count, WCHAR ***Names);
HRESULT WmiRepoGetClass(const WCHAR *Namespace, const WCHAR *Class, IWbemClassObject **Object);
HRESULT WmiRepoPutClass(const WCHAR *Namespace, IWbemClassObject *Class, LONG Flags);
HRESULT WmiRepoDeleteClass(const WCHAR *Namespace, const WCHAR *Class);
HRESULT WmiRepoEnumClasses(const WCHAR *Namespace, const WCHAR *Superclass, BOOL Deep, ULONG *Count,
                           IWbemClassObject ***Classes);
HRESULT WmiRepoPutInstance(const WCHAR *Namespace, IWbemClassObject *Instance, LONG Flags, WCHAR **RelativePath);
HRESULT WmiRepoGetInstance(const WCHAR *Namespace, const WMI_PATH *Path, IWbemClassObject **Instance);
HRESULT WmiRepoDeleteInstance(const WCHAR *Namespace, const WMI_PATH *Path);
HRESULT WmiRepoEnumInstances(const WCHAR *Namespace, const WCHAR *Class, BOOL Deep, ULONG *Count,
                             IWbemClassObject ***Instances);
HRESULT WmiRepoSystemClass(const WCHAR *Namespace, const WCHAR *Class, IWbemClassObject **Object);
void WmiFreeObjects(ULONG Count, IWbemClassObject **Objects);
WCHAR *WmiFullNamespace(const WCHAR *Namespace);

/* Services */
HRESULT WmiRepoServicesCreate(const WCHAR *Namespace, IWbemServices **Services);
HRESULT WmiEnumCreate(ULONG Count, IWbemClassObject **Objects, IEnumWbemClassObject **Enum);
HRESULT WmiCallResultCreate(HRESULT Status, IWbemClassObject *Object, const WCHAR *Path, IWbemCallResult **Result);
HRESULT WmiBuiltinNamespaceEnum(const WCHAR *Namespace, const WCHAR *Operation, const WCHAR *Text, LONG Flags,
                                IEnumWbemClassObject **Enum, BOOL *Handled);
HRESULT WmiBuiltinNamespaceHook(const WCHAR *Namespace, const WCHAR *Operation, const WCHAR *Path,
                                IWbemClassObject *Object, LONG Flags, IWbemClassObject **Result, BOOL *Handled);

/* Decoupled providers */
HRESULT WmiDecoupledRegistrarCreate(void **Object, REFIID Iid);
HRESULT WmiDecoupledEventProviderCreate(void **Object, REFIID Iid);
HRESULT WmiProviderEnumerate(const WCHAR *Namespace, IWbemClassObject *Class, ULONG *Count,
                             IWbemClassObject ***Instances);
HRESULT WmiProviderGetObject(const WCHAR *Namespace, IWbemClassObject *Class, const WCHAR *Path,
                             IWbemClassObject **Instance);
HRESULT WmiProviderExecMethod(const WCHAR *Namespace, IWbemClassObject *Class, const WCHAR *Path,
                              const WCHAR *Method, IWbemClassObject *InParams, IWbemClassObject **OutParams);
BOOL WmiClassIsDynamic(IWbemClassObject *Class);
