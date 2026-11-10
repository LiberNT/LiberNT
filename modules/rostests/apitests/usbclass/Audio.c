/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test playing through every wave output, the USB ones included
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <mmsystem.h>

#define AUDIO_RATE   48000
#define AUDIO_MS     200

START_TEST(Audio)
{
    WAVEFORMATEX Format;
    WAVEOUTCAPSW Caps;
    WAVEHDR Header;
    HWAVEOUT Wave;
    HANDLE Done;
    PSHORT Samples;
    UINT Count = waveOutGetNumDevs(), Device, Index;
    MMRESULT Result;
    BOOL Found = FALSE;

    for (Device = 0; Device < Count; ++Device)
    {
        if (waveOutGetDevCapsW(Device, &Caps, sizeof(Caps)) != MMSYSERR_NOERROR)
            continue;
        trace("Wave output %u: %S\n", Device, Caps.szPname);
        Found = TRUE;
        ZeroMemory(&Format, sizeof(Format));
        Format.wFormatTag = WAVE_FORMAT_PCM;
        Format.nChannels = 2;
        Format.nSamplesPerSec = AUDIO_RATE;
        Format.wBitsPerSample = 16;
        Format.nBlockAlign = 4;
        Format.nAvgBytesPerSec = AUDIO_RATE * 4;
        Done = CreateEventW(NULL, FALSE, FALSE, NULL);
        Result = waveOutOpen(&Wave, Device, &Format, (DWORD_PTR)Done, 0, CALLBACK_EVENT);
        ok(Result == MMSYSERR_NOERROR, "Opening %S failed: %u\n", Caps.szPname, Result);
        if (Result != MMSYSERR_NOERROR)
        {
            CloseHandle(Done);
            continue;
        }
        WaitForSingleObject(Done, 1000);
        Samples = HeapAlloc(GetProcessHeap(), 0, AUDIO_RATE * 4 * AUDIO_MS / 1000);
        for (Index = 0; Index < AUDIO_RATE * AUDIO_MS / 1000; ++Index)
            Samples[2 * Index] = Samples[2 * Index + 1] = (SHORT)((Index % 100) * 300 - 15000);
        ZeroMemory(&Header, sizeof(Header));
        Header.lpData = (LPSTR)Samples;
        Header.dwBufferLength = AUDIO_RATE * 4 * AUDIO_MS / 1000;
        ok_int(waveOutPrepareHeader(Wave, &Header, sizeof(Header)), MMSYSERR_NOERROR);
        ok_int(waveOutWrite(Wave, &Header, sizeof(Header)), MMSYSERR_NOERROR);
        for (Index = 0; Index < 30 && !(Header.dwFlags & WHDR_DONE); ++Index)
            WaitForSingleObject(Done, 100);
        ok(Header.dwFlags & WHDR_DONE, "%u ms of audio did not finish playing on %S\n", AUDIO_MS, Caps.szPname);
        waveOutReset(Wave);
        waveOutUnprepareHeader(Wave, &Header, sizeof(Header));
        ok_int(waveOutClose(Wave), MMSYSERR_NOERROR);
        HeapFree(GetProcessHeap(), 0, Samples);
        CloseHandle(Done);
    }
    if (!Found)
        skip("No wave output is present\n");
}
