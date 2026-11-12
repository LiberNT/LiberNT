/*
 * PROJECT:     LiberNT WinRT Speech
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Speech synthesis through the installed SAPI voices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <math.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winstring.h"
#include "objbase.h"
#include "initguid.h"
#include "sapi.h"

#include "sapi_synthesis.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(speech);

DEFINE_GUID(SPDFID_WaveFormatEx, 0xc31adbae, 0x527f, 0x4ff5, 0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c);

#define VOICE_TOKENS_KEY L"SOFTWARE\\Microsoft\\Speech_OneCore\\Voices\\Tokens"
#define VOICE_TOKENS_ID L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices\\Tokens\\"
#define SYNTHESIS_SAMPLE_RATE 16000
#define WAVE_HEADER_SIZE 46

struct synthesis_request
{
    IUnknown IUnknown_iface;
    LONG ref;
    HSTRING text;
    BOOL ssml;
    HSTRING voice_id;
    LONG rate;
    USHORT volume;
};

static inline struct synthesis_request *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct synthesis_request, IUnknown_iface );
}

static HRESULT WINAPI synthesis_request_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        IUnknown_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI synthesis_request_AddRef( IUnknown *iface )
{
    return InterlockedIncrement( &impl_from_IUnknown( iface )->ref );
}

static ULONG WINAPI synthesis_request_Release( IUnknown *iface )
{
    struct synthesis_request *impl = impl_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->text );
        WindowsDeleteString( impl->voice_id );
        free( impl );
    }
    return ref;
}

static const IUnknownVtbl synthesis_request_vtbl =
{
    synthesis_request_QueryInterface,
    synthesis_request_AddRef,
    synthesis_request_Release,
};

HRESULT synthesis_request_create( HSTRING text, BOOL ssml, HSTRING voice_id, double volume,
                                  double speaking_rate, IUnknown **out )
{
    struct synthesis_request *impl;
    double rate;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IUnknown_iface.lpVtbl = &synthesis_request_vtbl;
    impl->ref = 1;
    impl->ssml = ssml;
    rate = speaking_rate > 0.0 ? 10.0 * log( speaking_rate ) / log( 3.0 ) : 0.0;
    impl->rate = (LONG)floor( min( 10.0, max( -10.0, rate ) ) + 0.5 );
    impl->volume = (USHORT)floor( min( 1.0, max( 0.0, volume ) ) * 100.0 + 0.5 );

    hr = WindowsDuplicateString( text, &impl->text );
    if (SUCCEEDED(hr)) hr = WindowsDuplicateString( voice_id, &impl->voice_id );
    if (FAILED(hr))
    {
        synthesis_request_Release( &impl->IUnknown_iface );
        return hr;
    }

    *out = &impl->IUnknown_iface;
    return S_OK;
}

static HRESULT create_output( IStream **memory, ISpStream **stream )
{
    WAVEFORMATEX format;
    HRESULT hr;

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = SYNTHESIS_SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = SYNTHESIS_SAMPLE_RATE * 2;
    format.cbSize = 0;

    *stream = NULL;
    if (FAILED(hr = CreateStreamOnHGlobal( NULL, TRUE, memory ))) return hr;
    hr = CoCreateInstance( &CLSID_SpStream, NULL, CLSCTX_INPROC_SERVER, &IID_ISpStream, (void **)stream );
    if (SUCCEEDED(hr)) hr = ISpStream_SetBaseStream( *stream, *memory, &SPDFID_WaveFormatEx, &format );
    if (FAILED(hr))
    {
        if (*stream) ISpStream_Release( *stream );
        IStream_Release( *memory );
        *stream = NULL;
        *memory = NULL;
    }
    return hr;
}

static HRESULT select_voice( ISpVoice *voice, HSTRING id )
{
    ISpObjectToken *token;
    HRESULT hr;

    if (!id) return S_OK;
    if (FAILED(hr = CoCreateInstance( &CLSID_SpObjectToken, NULL, CLSCTX_INPROC_SERVER, &IID_ISpObjectToken,
                                      (void **)&token )))
        return hr;
    hr = ISpObjectToken_SetId( token, NULL, WindowsGetStringRawBuffer( id, NULL ), FALSE );
    if (SUCCEEDED(hr)) hr = ISpVoice_SetVoice( voice, token );
    ISpObjectToken_Release( token );
    return hr;
}

static void put_le16( BYTE *at, WORD value ) { at[0] = (BYTE)value; at[1] = (BYTE)(value >> 8); }
static void put_le32( BYTE *at, DWORD value ) { put_le16( at, LOWORD(value) ); put_le16( at + 2, HIWORD(value) ); }

static HRESULT read_wave( IStream *memory, BYTE **data, UINT32 *size )
{
    LARGE_INTEGER zero = {{0}};
    ULARGE_INTEGER end;
    ULONG pcm, read;
    BYTE *wave;
    HRESULT hr;

    if (FAILED(hr = IStream_Seek( memory, zero, STREAM_SEEK_END, &end ))) return hr;
    if (end.QuadPart > MAXDWORD - WAVE_HEADER_SIZE) return E_OUTOFMEMORY;
    pcm = end.u.LowPart;
    if (!(wave = malloc( WAVE_HEADER_SIZE + pcm ))) return E_OUTOFMEMORY;

    memcpy( wave, "RIFF", 4 );
    put_le32( wave + 4, WAVE_HEADER_SIZE - 8 + pcm );
    memcpy( wave + 8, "WAVEfmt ", 8 );
    put_le32( wave + 16, 18 );
    put_le16( wave + 20, WAVE_FORMAT_PCM );
    put_le16( wave + 22, 1 );
    put_le32( wave + 24, SYNTHESIS_SAMPLE_RATE );
    put_le32( wave + 28, SYNTHESIS_SAMPLE_RATE * 2 );
    put_le16( wave + 32, 2 );
    put_le16( wave + 34, 16 );
    put_le16( wave + 36, 0 );
    memcpy( wave + 38, "data", 4 );
    put_le32( wave + 42, pcm );

    hr = IStream_Seek( memory, zero, STREAM_SEEK_SET, NULL );
    if (SUCCEEDED(hr) && pcm) hr = IStream_Read( memory, wave + WAVE_HEADER_SIZE, pcm, &read );
    if (SUCCEEDED(hr) && pcm && read != pcm) hr = E_FAIL;
    if (FAILED(hr))
    {
        free( wave );
        return hr;
    }

    *data = wave;
    *size = WAVE_HEADER_SIZE + pcm;
    return S_OK;
}

HRESULT synthesis_request_run( IUnknown *request, BYTE **data, UINT32 *size )
{
    struct synthesis_request *impl = impl_from_IUnknown( request );
    HRESULT hr, init = CoInitializeEx( NULL, COINIT_MULTITHREADED );
    ISpStream *stream = NULL;
    IStream *memory = NULL;
    ISpVoice *voice = NULL;
    DWORD flags;

    *data = NULL;
    *size = 0;

    hr = CoCreateInstance( &CLSID_SpVoice, NULL, CLSCTX_INPROC_SERVER, &IID_ISpVoice, (void **)&voice );
    if (SUCCEEDED(hr)) hr = select_voice( voice, impl->voice_id );
    if (SUCCEEDED(hr)) hr = create_output( &memory, &stream );
    if (SUCCEEDED(hr)) hr = ISpVoice_SetOutput( voice, (IUnknown *)stream, TRUE );
    if (SUCCEEDED(hr)) hr = ISpVoice_SetRate( voice, impl->rate );
    if (SUCCEEDED(hr)) hr = ISpVoice_SetVolume( voice, impl->volume );
    if (SUCCEEDED(hr))
    {
        flags = impl->ssml ? SPF_IS_XML | SPF_PARSE_SSML : SPF_IS_NOT_XML;
        hr = ISpVoice_Speak( voice, WindowsGetStringRawBuffer( impl->text, NULL ), flags, NULL );
    }
    if (SUCCEEDED(hr)) hr = read_wave( memory, data, size );
    if (FAILED(hr)) WARN( "synthesis failed, hr %#lx.\n", hr );

    if (voice) ISpVoice_Release( voice );
    if (stream) ISpStream_Release( stream );
    if (memory) IStream_Release( memory );
    if (SUCCEEDED(init)) CoUninitialize();
    return hr;
}

static BOOL read_token_string( HKEY key, const WCHAR *subkey, const WCHAR *name, WCHAR *buffer, DWORD length )
{
    DWORD bytes = length * sizeof(WCHAR);

    return !RegGetValueW( key, subkey, name, RRF_RT_REG_SZ, NULL, buffer, &bytes );
}

static BOOL read_voice_token( HKEY tokens, const WCHAR *name, struct voice_token *token )
{
    WCHAR value[LOCALE_NAME_MAX_LENGTH], clsid[64];
    LCID lcid;

    if (!read_token_string( tokens, name, L"CLSID", clsid, ARRAY_SIZE(clsid) )) return FALSE;
    if (wcslen( VOICE_TOKENS_ID ) + wcslen( name ) >= ARRAY_SIZE(token->id)) return FALSE;
    wcscpy( token->id, VOICE_TOKENS_ID );
    wcscat( token->id, name );

    if (wcslen( name ) + 12 > ARRAY_SIZE(value)) return FALSE;
    wcscpy( value, name );
    wcscat( value, L"\\Attributes" );
    if (!read_token_string( tokens, value, L"Name", token->name, ARRAY_SIZE(token->name) ) &&
        !read_token_string( tokens, name, NULL, token->name, ARRAY_SIZE(token->name) ))
        return FALSE;

    token->female = read_token_string( tokens, value, L"Gender", clsid, ARRAY_SIZE(clsid) ) &&
                    !wcsicmp( clsid, L"Female" );

    if (!read_token_string( tokens, value, L"Language", clsid, ARRAY_SIZE(clsid) )) return FALSE;
    lcid = wcstoul( clsid, NULL, 16 );
    token->lcid = lcid;
    return LCIDToLocaleName( lcid, token->locale, ARRAY_SIZE(token->locale), 0 ) != 0;
}

HRESULT voice_tokens_enumerate( struct voice_token **tokens, UINT32 *count )
{
    struct voice_token *list = NULL, *grown, swap;
    WCHAR name[MAX_PATH];
    DWORD index, length;
    LANGID language = GetUserDefaultUILanguage();
    HKEY key;
    UINT32 found = 0, i;

    *tokens = NULL;
    *count = 0;
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, VOICE_TOKENS_KEY, 0, KEY_READ, &key )) return S_OK;

    for (index = 0; ; index++)
    {
        length = ARRAY_SIZE(name);
        if (RegEnumKeyExW( key, index, name, &length, NULL, NULL, NULL, NULL )) break;
        if (!(grown = realloc( list, (found + 1) * sizeof(*list) )))
        {
            RegCloseKey( key );
            free( list );
            return E_OUTOFMEMORY;
        }
        list = grown;
        if (read_voice_token( key, name, &list[found] )) found++;
    }
    RegCloseKey( key );

    for (i = 1; i < found; i++)
    {
        if (LANGIDFROMLCID( list[i].lcid ) == language && LANGIDFROMLCID( list[0].lcid ) != language)
        {
            swap = list[0];
            list[0] = list[i];
            list[i] = swap;
            break;
        }
    }

    *tokens = list;
    *count = found;
    return S_OK;
}
