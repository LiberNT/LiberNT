/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Phonemes and letter-to-sound interface of the synthesizer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windef.h>
#include "synth.h"

typedef enum _PHONEME
{
    PH_PAUSE,
    PH_AA, PH_AE, PH_AH, PH_AO, PH_AW, PH_AY, PH_EH, PH_ER, PH_EY,
    PH_IH, PH_IY, PH_OW, PH_OY, PH_UH, PH_UW,
    PH_B, PH_CH, PH_D, PH_DH, PH_F, PH_G, PH_HH, PH_JH, PH_K, PH_L,
    PH_M, PH_N, PH_NG, PH_P, PH_R, PH_S, PH_SH, PH_T, PH_TH, PH_V,
    PH_W, PH_Y, PH_Z, PH_ZH,
    PH_COUNT
} PHONEME;

#define MAX_WORD_LETTERS 48
#define MAX_WORD_PHONEMES 96

ULONG
LtsWordToPhonemes(
    _In_z_ const char *Word,
    _Out_writes_(Capacity) UCHAR *Phonemes,
    _In_ ULONG Capacity);

ULONG
LtsSpellLetter(
    _In_ char Letter,
    _Out_writes_(Capacity) UCHAR *Phonemes,
    _In_ ULONG Capacity);

ULONG
LtsNumberToWords(
    _In_ ULONGLONG Value,
    _Out_writes_(Capacity) char *Words,
    _In_ ULONG Capacity);
