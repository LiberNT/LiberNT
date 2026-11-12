/*
 * PROJECT:     LiberNT WinRT Speech
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Speech synthesis through the installed SAPI voices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct voice_token
{
    WCHAR id[MAX_PATH];
    WCHAR name[128];
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    LCID lcid;
    BOOL female;
};

HRESULT voice_tokens_enumerate( struct voice_token **tokens, UINT32 *count );
HRESULT synthesis_request_create( HSTRING text, BOOL ssml, HSTRING voice_id, double volume,
                                  double speaking_rate, IUnknown **out );
HRESULT synthesis_request_run( IUnknown *request, BYTE **data, UINT32 *size );
