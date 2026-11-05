/*
 * PROJECT:     LiberNT Audio Resampler DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the resampler object and its signal path
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "mmreg.h"
#include "ks.h"
#include "ksmedia.h"

#define RESAMPLER_DEFAULT_QUALITY 30
#define RESAMPLER_MAX_QUALITY 60

struct resampler_dsp
{
    BOOL in_float, out_float;
    UINT in_bits, out_bits;
    UINT in_channels, out_channels;
    UINT in_rate, out_rate;
    BOOL bypass;

    float *matrix;
    float *frame;
    double *acc;

    double cutoff;
    double half_length;
    UINT lookahead;
    UINT taps;
    UINT phases;
    double *table;

    float *history;
    SIZE_T history_capacity;
    UINT64 history_start;
    UINT64 received;
    UINT64 next_output;
};

HRESULT resampler_dsp_init(struct resampler_dsp *dsp, const WAVEFORMATEX *input, const WAVEFORMATEX *output, LONG quality);
void resampler_dsp_cleanup(struct resampler_dsp *dsp);
void resampler_dsp_reset(struct resampler_dsp *dsp);
HRESULT resampler_dsp_push(struct resampler_dsp *dsp, const BYTE *data, UINT frames);
UINT64 resampler_dsp_available(const struct resampler_dsp *dsp, BOOL drain);
void resampler_dsp_pull(struct resampler_dsp *dsp, BYTE *data, UINT frames);

BOOL resampler_wave_format_valid(const WAVEFORMATEX *format, SIZE_T size);
BOOL resampler_wave_format_is_float(const WAVEFORMATEX *format);

HRESULT resampler_create(IUnknown *outer, REFIID iid, void **out);
