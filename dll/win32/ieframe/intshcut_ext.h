/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Internet shortcut object state shared with the shell interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <shobjidl.h>
#include <propsys.h>

struct intshcut_prop;

typedef struct
{
    IUniformResourceLocatorA IUniformResourceLocatorA_iface;
    IUniformResourceLocatorW IUniformResourceLocatorW_iface;
    IPersistFile IPersistFile_iface;
    IPropertySetStorage IPropertySetStorage_iface;
    IPersistStream IPersistStream_iface;
    IShellLinkA IShellLinkA_iface;
    IShellLinkW IShellLinkW_iface;
    IExtractIconA IExtractIconA_iface;
    IExtractIconW IExtractIconW_iface;
    IQueryInfo IQueryInfo_iface;
    INewShortcutHookA INewShortcutHookA_iface;
    INewShortcutHookW INewShortcutHookW_iface;
    IShellExtInit IShellExtInit_iface;
    IContextMenu2 IContextMenu2_iface;
    IDataObject IDataObject_iface;
    IObjectWithSite IObjectWithSite_iface;
    IServiceProvider IServiceProvider_iface;
    IOleCommandTarget IOleCommandTarget_iface;
    IPropertyStore IPropertyStore_iface;
    IInitializeWithFile IInitializeWithFile_iface;
    IInitializeWithBindCtx IInitializeWithBindCtx_iface;
    IShellPropSheetExt IShellPropSheetExt_iface;

    LONG refCount;

    IPropertySetStorage *property_set_storage;
    WCHAR *url;
    BOOLEAN isDirty;
    LPOLESTR currentFile;

    WCHAR *description;
    WCHAR *folder;
    IUnknown *site;
    struct intshcut_prop *props;
    DWORD prop_count;
    BOOL loaded;
} InternetShortcut;

void InternetShortcutExt_Init(InternetShortcut *This);
void InternetShortcutExt_Free(InternetShortcut *This);
HRESULT InternetShortcutExt_QueryInterface(InternetShortcut *This, REFIID riid, void **ppv);
void InternetShortcutExt_LoadFile(InternetShortcut *This, LPCWSTR file);
void InternetShortcutExt_SaveFile(InternetShortcut *This, LPCWSTR file);
