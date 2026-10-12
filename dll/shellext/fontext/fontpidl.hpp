/*
 * PROJECT:     ReactOS Font Shell Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     pidl handling
 * COPYRIGHT:   Copyright 2019 Mark Jansen <mark.jansen@reactos.org>
 *              Copyright 2026 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#pragma once

#pragma pack(push, 1)
struct FontPidlEntry
{
    WORD cb;
    WORD Magic;
    WORD ibName;
    WORD ibFileName;
    LPWSTR Name() { return (LPWSTR)((PBYTE)this + ibName); }
    LPWSTR FileName() { return (LPWSTR)((PBYTE)this + ibFileName); }
    LPCWSTR Name() const { return (LPCWSTR)((PBYTE)this + ibName); }
    LPCWSTR FileName() const { return (LPCWSTR)((PBYTE)this + ibFileName); }
};
#pragma pack(pop)

PITEMID_CHILD _ILCreate(LPCWSTR lpName, LPCWSTR lpFileName);
const FontPidlEntry* _FontFromIL(PCITEMID_CHILD pidl);
