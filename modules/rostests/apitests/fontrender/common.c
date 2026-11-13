/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Font files, coverage statistics and image dumps shared by the font rendering tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "fontrender.h"
#include <stdio.h>

FR_FONT FrFonts[2] =
{
    { L"Selawik", L"SELAWIK" },
    { L"FreeSans", L"FREESANS" },
};

const INT FrSizes[6] = { 9, 11, 12, 14, 16, 24 };

BOOL
FrExtractFont(FR_FONT *Font)
{
    WCHAR Dir[MAX_PATH];
    HRSRC Resource;
    HGLOBAL Data;
    HANDLE File;
    DWORD Size, Written;
    BOOL Result;

    Resource = FindResourceW(NULL, Font->Resource, (LPCWSTR)RT_RCDATA);
    if (!Resource)
        return FALSE;
    Data = LoadResource(NULL, Resource);
    Size = SizeofResource(NULL, Resource);
    if (!Data || !Size)
        return FALSE;

    if (!GetTempPathW(ARRAYSIZE(Dir), Dir))
        return FALSE;
    swprintf(Font->Path, ARRAYSIZE(Font->Path), L"%sfontrender_%s.ttf", Dir, Font->Family);

    File = CreateFileW(Font->Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, LockResource(Data), Size, &Written, NULL) && Written == Size;
    CloseHandle(File);
    return Result;
}

VOID
FrDeleteFont(FR_FONT *Font)
{
    DeleteFileW(Font->Path);
}

BOOL
FrEmit(VOID)
{
    return GetEnvironmentVariableA("FONTRENDER_EMIT", NULL, 0) != 0;
}

static VOID
FrDump(const char *Name, const BYTE *Pixels, INT Width, INT Height, INT Stride, INT Channels, BOOL IsCoverage)
{
    char Dir[MAX_PATH], Path[MAX_PATH], Header[64];
    HANDLE File;
    DWORD Written;
    BYTE *Row;
    INT X, Y, C, Length;

    if (!GetEnvironmentVariableA("FONTRENDER_DUMP", Dir, sizeof(Dir)))
        return;
    sprintf(Path, "%s\\%s.ppm", Dir, Name);
    File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;

    Row = HeapAlloc(GetProcessHeap(), 0, Width * 3);
    if (Row)
    {
        Length = sprintf(Header, "P6\n%d %d\n255\n", Width, Height);
        WriteFile(File, Header, Length, &Written, NULL);
        for (Y = 0; Y < Height; Y++)
        {
            for (X = 0; X < Width; X++)
            {
                for (C = 0; C < 3; C++)
                {
                    BYTE Value = Pixels[Y * Stride + X * Channels + (Channels == 1 ? 0 : Channels == 4 ? 2 - C : C)];
                    Row[X * 3 + C] = IsCoverage ? 255 - Value : Value;
                }
            }
            WriteFile(File, Row, Width * 3, &Written, NULL);
        }
        HeapFree(GetProcessHeap(), 0, Row);
    }
    CloseHandle(File);
}

VOID
FrCoverage(const char *Name, const BYTE *Pixels, INT Width, INT Height, INT Stride, INT Channels, BOOL IsCoverage)
{
    ULONG Ink = 0, Fringe = 0, Covered = 0, Solid = 0;
    INT X, Y, C, Left = Width, Top = Height, Right = 0, Bottom = 0;

    for (Y = 0; Y < Height; Y++)
    {
        for (X = 0; X < Width; X++)
        {
            const BYTE *Pixel = &Pixels[Y * Stride + X * Channels];
            INT Value[3], Sum = 0;

            for (C = 0; C < 3; C++)
            {
                Value[C] = Pixel[Channels == 1 ? 0 : C];
                if (!IsCoverage)
                    Value[C] = 255 - Value[C];
                Sum += Value[C];
            }
            if (!Sum)
                continue;

            Ink += Sum;
            Fringe += abs(Value[0] - Value[2]);
            Covered++;
            if (Sum >= 3 * 0xF0)
                Solid++;
            Left = min(Left, X);
            Top = min(Top, Y);
            Right = max(Right, X + 1);
            Bottom = max(Bottom, Y + 1);
        }
    }

    if (!Covered)
        Left = Top = 0;
    printf("coverage %s ink=%.2f fringe=%.2f solid=%.3f box=%d,%d,%d,%d\n", Name, Ink / (3.0 * 255.0),
          Fringe / 255.0, Covered ? (double)Solid / Covered : 0.0, Left, Top, Right, Bottom);
    FrDump(Name, Pixels, Width, Height, Stride, Channels, IsCoverage);
}
