/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sound page: output and input devices, volume and mute
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

static const PROPERTYKEY DeviceFriendlyNameKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

static IMMDeviceEnumerator *
AudioEnumerator()
{
    IMMDeviceEnumerator *enumerator = NULL;

    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARG(IMMDeviceEnumerator, &enumerator))))
        return NULL;
    return enumerator;
}

static std::wstring
AudioName(IMMDevice *device)
{
    IPropertyStore *store = NULL;
    PROPVARIANT value;
    std::wstring name;

    PropVariantInit(&value);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
        SUCCEEDED(store->GetValue(DeviceFriendlyNameKey, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal)
        name = value.pwszVal;
    PropVariantClear(&value);
    if (store)
        store->Release();
    return name;
}

static void
AudioDevices(EDataFlow flow, std::vector<std::wstring> &ids, std::vector<std::wstring> &names, int *selected)
{
    IMMDeviceEnumerator *enumerator = AudioEnumerator();
    IMMDeviceCollection *collection = NULL;
    IMMDevice *preferred = NULL;
    LPWSTR preferredId = NULL;
    UINT count = 0;

    *selected = -1;
    if (!enumerator)
        return;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &preferred)))
    {
        preferred->GetId(&preferredId);
        preferred->Release();
    }
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)))
    {
        collection->GetCount(&count);
        for (UINT i = 0; i < count; i++)
        {
            IMMDevice *device = NULL;
            LPWSTR id = NULL;
            if (SUCCEEDED(collection->Item(i, &device)) && SUCCEEDED(device->GetId(&id)))
            {
                if (preferredId && !wcscmp(id, preferredId))
                    *selected = (int)ids.size();
                ids.push_back(id);
                names.push_back(AudioName(device));
                CoTaskMemFree(id);
            }
            if (device)
                device->Release();
        }
        collection->Release();
    }
    CoTaskMemFree(preferredId);
    enumerator->Release();
}

static IAudioEndpointVolume *
AudioVolume(EDataFlow flow)
{
    IMMDeviceEnumerator *enumerator = AudioEnumerator();
    IMMDevice *device = NULL;
    IAudioEndpointVolume *volume = NULL;

    if (!enumerator)
        return NULL;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &device)))
    {
        device->Activate(IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL, (void **)&volume);
        device->Release();
    }
    enumerator->Release();
    return volume;
}

static void
SetPreferredAudio(EDataFlow flow, const std::wstring &id)
{
    WriteString(HKEY_CURRENT_USER, L"Software\\Wine\\Drivers\\wdmaud.drv",
                flow == eRender ? L"DefaultOutput" : L"DefaultInput", id);
}

static void
AddAudioGroup(PageBuilder &page, EDataFlow flow)
{
    std::vector<std::wstring> ids, names;
    IAudioEndpointVolume *volume;
    float level = 0.0f;
    BOOL muted = FALSE;
    int selected;
    bool output = flow == eRender;

    page.Group(Str(output ? IDS_OUTPUT : IDS_INPUT));
    if (page.Indexing)
    {
        page.Choice(Str(output ? IDS_OUTPUT_DEVICE : IDS_INPUT_DEVICE), std::wstring(),
                    output ? L"speaker_2_20_regular" : L"mic_20_regular", names, -1, nullptr);
        page.Slider(Str(IDS_VOLUME), std::wstring(), NULL, 0, 100, 0, nullptr);
        return;
    }
    AudioDevices(flow, ids, names, &selected);
    if (ids.empty())
    {
        page.Text(Str(output ? IDS_NO_OUTPUT : IDS_NO_INPUT));
        return;
    }
    page.Choice(Str(output ? IDS_OUTPUT_DEVICE : IDS_INPUT_DEVICE), Str(output ? IDS_OUTPUT_DESC : IDS_INPUT_DESC),
                output ? L"speaker_2_20_regular" : L"mic_20_regular", names, selected,
                [flow, ids](int index) { SetPreferredAudio(flow, ids[index]); });
    volume = AudioVolume(flow);
    if (volume)
    {
        volume->GetMasterVolumeLevelScalar(&level);
        volume->GetMute(&muted);
        volume->Release();
    }
    {
        SettingRow &row = page.Slider(Str(IDS_VOLUME), std::wstring(), output ? L"speaker_1_20_regular" : L"mic_20_regular",
                                      0, 100, (int)(level * 100.0f + 0.5f),
                                      [flow](int value)
                                      {
                                          IAudioEndpointVolume *volume = AudioVolume(flow);
                                          if (volume)
                                          {
                                              volume->SetMasterVolumeLevelScalar((float)value / 100.0f, NULL);
                                              volume->Release();
                                          }
                                      });
        row.Live = true;
        row.Enabled = volume != NULL;
        row.Label = [](int value) { return Printf(L"%d", value); };
    }
    page.Toggle(Str(output ? IDS_MUTE_OUTPUT : IDS_MUTE_INPUT), std::wstring(),
                output ? L"speaker_mute_20_regular" : L"mic_off_20_regular", muted != FALSE,
                [flow](int value)
                {
                    IAudioEndpointVolume *volume = AudioVolume(flow);
                    if (volume)
                    {
                        volume->SetMute(value != 0, NULL);
                        volume->Release();
                    }
                }).Enabled = volume != NULL;
}

struct AudioApp
{
    std::wstring Id;
    std::wstring Name;
    float Level;
    BOOL Muted;
};

static IAudioSessionEnumerator *
AudioSessions()
{
    IMMDeviceEnumerator *enumerator = AudioEnumerator();
    IMMDevice *device = NULL;
    IAudioSessionManager2 *manager = NULL;
    IAudioSessionEnumerator *sessions = NULL;

    if (!enumerator)
        return NULL;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) &&
        SUCCEEDED(device->Activate(IID_IAudioSessionManager2, CLSCTX_INPROC_SERVER, NULL, (void **)&manager)))
    {
        if (FAILED(manager->GetSessionEnumerator(&sessions)))
            sessions = NULL;
        manager->Release();
    }
    if (device)
        device->Release();
    enumerator->Release();
    return sessions;
}

static std::wstring
ProcessName(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    WCHAR path[MAX_PATH];
    DWORD length = _countof(path);
    std::wstring name;

    if (!process)
        return name;
    if (QueryFullProcessImageNameW(process, 0, path, &length))
    {
        name = PathFindFileNameW(path);
        if (name.find_last_of(L'.') != std::wstring::npos)
            name.erase(name.find_last_of(L'.'));
    }
    CloseHandle(process);
    return name;
}

static void
WithSession(const std::wstring &id, std::function<void(ISimpleAudioVolume *)> action)
{
    IAudioSessionEnumerator *sessions = AudioSessions();
    int count = 0;

    if (!sessions)
        return;
    sessions->GetCount(&count);
    for (int i = 0; i < count; i++)
    {
        IAudioSessionControl *control = NULL;
        IAudioSessionControl2 *control2 = NULL;
        ISimpleAudioVolume *volume = NULL;
        LPWSTR instance = NULL;

        if (SUCCEEDED(sessions->GetSession(i, &control)) &&
            SUCCEEDED(control->QueryInterface(IID_PPV_ARG(IAudioSessionControl2, &control2))) &&
            SUCCEEDED(control2->GetSessionInstanceIdentifier(&instance)) && instance && id == instance &&
            SUCCEEDED(control->QueryInterface(IID_PPV_ARG(ISimpleAudioVolume, &volume))))
        {
            action(volume);
        }
        CoTaskMemFree(instance);
        if (volume)
            volume->Release();
        if (control2)
            control2->Release();
        if (control)
            control->Release();
    }
    sessions->Release();
}

static std::vector<AudioApp>
AudioApps()
{
    std::vector<AudioApp> apps;
    IAudioSessionEnumerator *sessions = AudioSessions();
    int count = 0;

    if (!sessions)
        return apps;
    sessions->GetCount(&count);
    for (int i = 0; i < count; i++)
    {
        IAudioSessionControl *control = NULL;
        IAudioSessionControl2 *control2 = NULL;
        ISimpleAudioVolume *volume = NULL;
        LPWSTR instance = NULL, display = NULL;
        AudioSessionState state;
        DWORD pid = 0;
        AudioApp app;

        if (FAILED(sessions->GetSession(i, &control)))
            continue;
        if (SUCCEEDED(control->QueryInterface(IID_PPV_ARG(IAudioSessionControl2, &control2))) &&
            SUCCEEDED(control2->GetState(&state)) && state != AudioSessionStateExpired &&
            SUCCEEDED(control2->GetSessionInstanceIdentifier(&instance)) && instance &&
            SUCCEEDED(control->QueryInterface(IID_PPV_ARG(ISimpleAudioVolume, &volume))))
        {
            app.Id = instance;
            if (control2->IsSystemSoundsSession() == S_OK)
                app.Name = Str(IDS_SYSTEM_SOUNDS);
            else
            {
                if (SUCCEEDED(control2->GetDisplayName(&display)) && display && display[0] != L'@')
                    app.Name = display;
                control2->GetProcessId(&pid);
                if (app.Name.empty() && pid)
                    app.Name = ProcessName(pid);
            }
            if (app.Name.empty())
                app.Name = Str(IDS_UNKNOWN_APP);
            app.Level = 1.0f;
            app.Muted = FALSE;
            volume->GetMasterVolume(&app.Level);
            volume->GetMute(&app.Muted);
            apps.push_back(app);
        }
        CoTaskMemFree(display);
        CoTaskMemFree(instance);
        if (volume)
            volume->Release();
        if (control2)
            control2->Release();
        control->Release();
    }
    sessions->Release();
    return apps;
}

#define SCHEMES_KEY L"AppEvents\\Schemes"

static std::vector<std::pair<std::wstring, std::wstring>>
SoundSchemes()
{
    std::vector<std::pair<std::wstring, std::wstring>> schemes;
    HKEY names;
    WCHAR key[MAX_PATH];

    if (RegOpenKeyExW(HKEY_CURRENT_USER, SCHEMES_KEY L"\\Names", 0, KEY_READ, &names) != ERROR_SUCCESS)
        return schemes;
    for (DWORD i = 0; ; i++)
    {
        DWORD length = _countof(key);
        HKEY scheme;
        WCHAR display[MAX_PATH] = L"";
        DWORD size = sizeof(display);

        if (RegEnumKeyExW(names, i, key, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(names, key, 0, KEY_READ, &scheme) == ERROR_SUCCESS)
        {
            if (RegLoadMUIStringW(scheme, L"", display, sizeof(display), NULL, 0, NULL) != ERROR_SUCCESS &&
                RegQueryValueExW(scheme, NULL, NULL, NULL, (BYTE *)display, &size) != ERROR_SUCCESS)
                display[0] = UNICODE_NULL;
            RegCloseKey(scheme);
        }
        schemes.push_back(std::make_pair(std::wstring(key), std::wstring(display[0] ? display : key)));
    }
    RegCloseKey(names);
    return schemes;
}

static void
ApplySoundScheme(HWND owner, const std::wstring &scheme)
{
    HKEY apps;
    WCHAR app[MAX_PATH], label[MAX_PATH];

    if (!WriteString(HKEY_CURRENT_USER, SCHEMES_KEY, NULL, scheme))
    {
        ShowError(owner, ERROR_ACCESS_DENIED);
        return;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SCHEMES_KEY L"\\Apps", 0, KEY_READ, &apps) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0; ; i++)
    {
        DWORD length = _countof(app);
        HKEY appKey;

        if (RegEnumKeyExW(apps, i, app, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(apps, app, 0, KEY_READ, &appKey) != ERROR_SUCCESS)
            continue;
        for (DWORD j = 0; ; j++)
        {
            DWORD labelLength = _countof(label);
            std::wstring base = std::wstring(SCHEMES_KEY L"\\Apps\\") + app + L"\\";
            std::wstring value;

            if (RegEnumKeyExW(appKey, j, label, &labelLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            if (scheme != L".None")
                value = ReadString(HKEY_CURRENT_USER, (base + label + L"\\" + scheme).c_str(), NULL);
            RegSetKeyValueW(HKEY_CURRENT_USER, (base + label + L"\\.Current").c_str(), NULL,
                            value.find(L'%') != std::wstring::npos ? REG_EXPAND_SZ : REG_SZ, value.c_str(),
                            (DWORD)((value.length() + 1) * sizeof(WCHAR)));
        }
        RegCloseKey(appKey);
    }
    RegCloseKey(apps);
}

void
BuildSound(PageBuilder &page)
{
    AddAudioGroup(page, eRender);
    AddAudioGroup(page, eCapture);

    page.Group(Str(IDS_APP_VOLUME));
    if (!page.Indexing)
    {
        std::vector<AudioApp> apps = AudioApps();
        if (apps.empty())
            page.Text(Str(IDS_NO_AUDIO_APPS));
        for (size_t i = 0; i < apps.size(); i++)
        {
            std::wstring id = apps[i].Id;
            SettingRow &row = page.Slider(apps[i].Name, std::wstring(), L"sound_wave_circle_20_regular", 0, 100,
                                          (int)(apps[i].Level * 100.0f + 0.5f),
                                          [id](int value)
                                          {
                                              WithSession(id, [value](ISimpleAudioVolume *volume)
                                                          { volume->SetMasterVolume((float)value / 100.0f, NULL); });
                                          });
            row.Live = true;
            row.Label = [](int value) { return Printf(L"%d", value); };
        }
    }
    else
    {
        page.Slider(Str(IDS_APP_VOLUME), Str(IDS_APP_VOLUME_DESC), L"sound_wave_circle_20_regular", 0, 100, 0, nullptr);
    }

    page.Group(Str(IDS_SYSTEM_SOUNDS));
    {
        SettingsHost *host = page.Host;
        std::vector<std::pair<std::wstring, std::wstring>> schemes = page.Indexing ?
            std::vector<std::pair<std::wstring, std::wstring>>() : SoundSchemes();
        std::wstring current = page.Indexing ? std::wstring() : ReadString(HKEY_CURRENT_USER, SCHEMES_KEY, NULL);
        std::vector<std::wstring> names;
        int selected = -1;

        for (size_t i = 0; i < schemes.size(); i++)
        {
            names.push_back(schemes[i].second);
            if (!lstrcmpiW(schemes[i].first.c_str(), current.c_str()))
                selected = (int)i;
        }
        page.Choice(Str(IDS_SOUND_SCHEME), Str(IDS_SOUND_SCHEME_DESC), L"speaker_2_20_regular", names, selected,
                    [host, schemes](int index) { ApplySoundScheme(host->Window(), schemes[index].first); })
            .Enabled = page.Indexing || !schemes.empty();
    }
}
