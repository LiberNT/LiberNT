/*
 * PROJECT:     LiberNT Audio Resampler DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sample conversion, channel mapping and sample rate conversion
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "resampledmo_private.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(resampler);

#define MAX_TABLE_PHASES 4096
#define KAISER_BETA 10.0

enum speaker
{
    SP_L, SP_R, SP_C, SP_LFE, SP_BL, SP_BR, SP_SL, SP_SR, SP_COUNT
};

static const DWORD speaker_bits[SP_COUNT] =
{
    SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT, SPEAKER_FRONT_CENTER, SPEAKER_LOW_FREQUENCY,
    SPEAKER_BACK_LEFT, SPEAKER_BACK_RIGHT, SPEAKER_SIDE_LEFT, SPEAKER_SIDE_RIGHT,
};

struct downmix_table
{
    DWORD mask;
    float left[SP_COUNT];
    float right[SP_COUNT];
    float center[SP_COUNT];
};

/* Default downmix weights of the Audio Resampler DSP documentation, in speaker order. */
static const struct downmix_table downmix_tables[] =
{
    {KSAUDIO_SPEAKER_5POINT1,
            {0.314f, 0.0f, 0.222f, 0.031f, 0.268f, 0.164f, 0.0f, 0.0f},
            {0.0f, 0.314f, 0.222f, 0.031f, 0.164f, 0.268f, 0.0f, 0.0f},
            {0.192f, 0.192f, 0.192f, 0.038f, 0.192f, 0.192f, 0.0f, 0.0f}},
    {KSAUDIO_SPEAKER_5POINT1_SURROUND,
            {0.320f, 0.0f, 0.226f, 0.032f, 0.0f, 0.0f, 0.292f, 0.130f},
            {0.0f, 0.320f, 0.226f, 0.032f, 0.0f, 0.0f, 0.130f, 0.292f},
            {0.192f, 0.192f, 0.192f, 0.038f, 0.0f, 0.0f, 0.192f, 0.192f}},
    {KSAUDIO_SPEAKER_7POINT1_SURROUND,
            {0.222f, 0.0f, 0.157f, 0.022f, 0.189f, 0.116f, 0.203f, 0.090f},
            {0.0f, 0.222f, 0.157f, 0.022f, 0.116f, 0.189f, 0.090f, 0.203f},
            {0.139f, 0.139f, 0.139f, 0.028f, 0.139f, 0.139f, 0.139f, 0.139f}},
};

static const float downmix_8_to_6_back[6][8] =
{
    {0.518f, 0, 0, 0, 0, 0, 0.189f, 0},
    {0, 0.518f, 0, 0, 0, 0, 0, 0.189f},
    {0, 0, 0.518f, 0, 0, 0, 0, 0},
    {0, 0, 0, 0.518f, 0, 0, 0, 0},
    {0, 0, 0, 0, 0.518f, 0, 0.482f, 0},
    {0, 0, 0, 0, 0, 0.518f, 0, 0.482f},
};

static const float downmix_8_to_6_side[6][8] =
{
    {0.447f, 0, 0, 0, 0, 0, 0, 0},
    {0, 0.447f, 0, 0, 0, 0, 0, 0},
    {0, 0, 0.447f, 0, 0, 0, 0, 0},
    {0, 0, 0, 0.447f, 0, 0, 0, 0},
    {0, 0, 0, 0, 0.429f, 0.124f, 0.447f, 0},
    {0, 0, 0, 0, 0.124f, 0.429f, 0, 0.447f},
};

BOOL resampler_wave_format_is_float(const WAVEFORMATEX *format)
{
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)format;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    return format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
}

BOOL resampler_wave_format_valid(const WAVEFORMATEX *format, SIZE_T size)
{
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)format;
    BOOL is_float;

    if (size < sizeof(*format))
        return FALSE;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (size < sizeof(*ext) || format->cbSize < sizeof(*ext) - sizeof(*format))
            return FALSE;
        if (!IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)
                && !IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            return FALSE;
    }
    else if (format->wFormatTag != WAVE_FORMAT_PCM && format->wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
        return FALSE;

    if (!format->nChannels || !format->nSamplesPerSec)
        return FALSE;
    is_float = resampler_wave_format_is_float(format);
    if (is_float && format->wBitsPerSample != 32 && format->wBitsPerSample != 64)
        return FALSE;
    if (!is_float && format->wBitsPerSample != 8 && format->wBitsPerSample != 16
            && format->wBitsPerSample != 24 && format->wBitsPerSample != 32)
        return FALSE;
    return format->nBlockAlign == format->nChannels * format->wBitsPerSample / 8;
}

static DWORD wave_format_channel_mask(const WAVEFORMATEX *format)
{
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((const WAVEFORMATEXTENSIBLE *)format)->dwChannelMask)
        return ((const WAVEFORMATEXTENSIBLE *)format)->dwChannelMask;
    switch (format->nChannels)
    {
        case 1: return KSAUDIO_SPEAKER_MONO;
        case 2: return KSAUDIO_SPEAKER_STEREO;
        case 4: return KSAUDIO_SPEAKER_QUAD;
        case 6: return KSAUDIO_SPEAKER_5POINT1;
        case 8: return KSAUDIO_SPEAKER_7POINT1_SURROUND;
    }
    return 0;
}

static void channel_speakers(DWORD mask, UINT channels, int *speakers)
{
    UINT channel = 0, bit;
    int i;

    for (bit = 0; bit < 32 && channel < channels; bit++)
    {
        if (!(mask & (1u << bit)))
            continue;
        speakers[channel] = -1;
        for (i = 0; i < SP_COUNT; i++)
            if (speaker_bits[i] == (1u << bit))
                speakers[channel] = i;
        if (speakers[channel] < 0)
            speakers[channel] = SP_COUNT + bit;
        channel++;
    }
    for (; channel < channels; channel++)
        speakers[channel] = -1;
}

static void downmix_row(float *row, const int *in_speakers, UINT in_channels, const float *weights, BOOL normalize)
{
    float sum = 0.0f;
    UINT i;

    for (i = 0; i < in_channels; i++)
    {
        row[i] = (in_speakers[i] >= 0 && in_speakers[i] < SP_COUNT) ? weights[in_speakers[i]] : 0.0f;
        sum += row[i];
    }
    if (normalize && sum > 0.0f)
        for (i = 0; i < in_channels; i++)
            row[i] /= sum;
}

static const struct downmix_table *find_downmix_table(DWORD in_mask)
{
    UINT i;

    for (i = 0; i < ARRAY_SIZE(downmix_tables); i++)
        if (!(in_mask & ~downmix_tables[i].mask))
            return &downmix_tables[i];
    return &downmix_tables[ARRAY_SIZE(downmix_tables) - 1];
}

static void init_matrix(struct resampler_dsp *dsp, DWORD in_mask, DWORD out_mask)
{
    int in_speakers[32], out_speakers[32];
    UINT in_channels = dsp->in_channels, out_channels = dsp->out_channels, i, o;
    BOOL contained = TRUE;
    float *m = dsp->matrix;

    memset(m, 0, out_channels * in_channels * sizeof(*m));
    channel_speakers(in_mask, min(in_channels, 32), in_speakers);
    channel_speakers(out_mask, min(out_channels, 32), out_speakers);
    for (i = in_channels; i < 32; i++) in_speakers[i] = -1;
    for (o = out_channels; o < 32; o++) out_speakers[o] = -1;

    if (in_mask == 0x63f && (out_mask == KSAUDIO_SPEAKER_5POINT1 || out_mask == KSAUDIO_SPEAKER_5POINT1_SURROUND)
            && in_channels == 8 && out_channels == 6)
    {
        const float (*table)[8] = out_mask == KSAUDIO_SPEAKER_5POINT1 ? downmix_8_to_6_back : downmix_8_to_6_side;
        for (o = 0; o < 6; o++)
            for (i = 0; i < 8; i++)
                m[o * in_channels + i] = table[o][i];
        return;
    }

    for (i = 0; i < in_channels && i < 32; i++)
    {
        if (in_speakers[i] < 0)
            continue;
        for (o = 0; o < out_channels && o < 32; o++)
            if (out_speakers[o] == in_speakers[i])
                break;
        if (o == out_channels || o == 32)
            contained = FALSE;
    }

    if (!contained && (out_mask == KSAUDIO_SPEAKER_STEREO || out_mask == KSAUDIO_SPEAKER_MONO) && out_channels <= 2)
    {
        const struct downmix_table *table = find_downmix_table(in_mask);
        BOOL normalize = in_mask != table->mask;

        if (out_mask == KSAUDIO_SPEAKER_MONO)
            downmix_row(m, in_speakers, in_channels, table->center, normalize);
        else
        {
            downmix_row(m, in_speakers, in_channels, table->left, normalize);
            downmix_row(m + in_channels, in_speakers, in_channels, table->right, normalize);
        }
        return;
    }

    if (!contained)
        FIXME("No default matrix for channel mask %#lx to %#lx; dropping unmatched channels.\n", in_mask, out_mask);
    for (i = 0; i < in_channels; i++)
    {
        for (o = 0; o < out_channels; o++)
        {
            if (i < 32 && o < 32 && in_speakers[i] >= 0 && out_speakers[o] == in_speakers[i])
                break;
            if (i < 32 && o < 32 && in_speakers[i] < 0 && out_speakers[o] < 0 && i == o)
                break;
        }
        if (o < out_channels)
            m[o * in_channels + i] = 1.0f;
    }
}

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, k;

    for (k = 1.0; term > 1e-14 * sum; k += 1.0)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

static double kernel(const struct resampler_dsp *dsp, double d)
{
    double s, u;

    if (fabs(d) >= dsp->half_length)
        return 0.0;
    s = d == 0.0 ? dsp->cutoff : sin(M_PI * dsp->cutoff * d) / (M_PI * d);
    u = d / dsp->half_length;
    return s * bessel_i0(KAISER_BETA * sqrt(1.0 - u * u)) / bessel_i0(KAISER_BETA);
}

static void phase_weights(const struct resampler_dsp *dsp, double frac, double *weights)
{
    int start = -(int)(dsp->taps / 2);
    double sum = 0.0;
    UINT i;

    for (i = 0; i < dsp->taps; i++)
        sum += weights[i] = kernel(dsp, frac - (start + (int)i));
    if (sum != 0.0)
        for (i = 0; i < dsp->taps; i++)
            weights[i] /= sum;
}

static UINT gcd(UINT a, UINT b)
{
    while (b)
    {
        UINT t = a % b;
        a = b;
        b = t;
    }
    return a;
}

HRESULT resampler_dsp_init(struct resampler_dsp *dsp, const WAVEFORMATEX *input, const WAVEFORMATEX *output, LONG quality)
{
    UINT step, p;
    double ratio;

    memset(dsp, 0, sizeof(*dsp));
    dsp->in_float = resampler_wave_format_is_float(input);
    dsp->out_float = resampler_wave_format_is_float(output);
    dsp->in_bits = input->wBitsPerSample;
    dsp->out_bits = output->wBitsPerSample;
    dsp->in_channels = input->nChannels;
    dsp->out_channels = output->nChannels;
    dsp->in_rate = input->nSamplesPerSec;
    dsp->out_rate = output->nSamplesPerSec;
    dsp->bypass = dsp->in_rate == dsp->out_rate;

    if (!(dsp->matrix = calloc(dsp->in_channels * dsp->out_channels, sizeof(*dsp->matrix)))
            || !(dsp->frame = calloc(dsp->in_channels, sizeof(*dsp->frame)))
            || !(dsp->acc = calloc(dsp->out_channels, sizeof(*dsp->acc))))
    {
        resampler_dsp_cleanup(dsp);
        return E_OUTOFMEMORY;
    }
    init_matrix(dsp, wave_format_channel_mask(input), wave_format_channel_mask(output));

    if (dsp->bypass)
        return S_OK;

    ratio = (double)dsp->in_rate / dsp->out_rate;
    /* Kaiser window, cutoff and look-ahead measured on Windows 11 at qualities 30 and 60. */
    dsp->cutoff = (1.0 - 1.044 / (quality + 1)) * min(1.0, 1.0 / ratio);
    dsp->half_length = (quality + 1) * max(1.0, ratio);
    /* FIXME: Windows holds back one input frame less at quality 1. */
    dsp->lookahead = (UINT)floor(dsp->half_length) + 1;
    dsp->taps = 2 * (UINT)ceil(dsp->half_length) + 1;

    step = gcd(dsp->in_rate, dsp->out_rate);
    dsp->phases = dsp->out_rate / step;
    if (dsp->phases <= MAX_TABLE_PHASES)
    {
        if (!(dsp->table = malloc(dsp->phases * dsp->taps * sizeof(*dsp->table))))
        {
            resampler_dsp_cleanup(dsp);
            return E_OUTOFMEMORY;
        }
        for (p = 0; p < dsp->phases; p++)
            phase_weights(dsp, (double)p * step / dsp->out_rate, dsp->table + p * dsp->taps);
    }
    return S_OK;
}

void resampler_dsp_cleanup(struct resampler_dsp *dsp)
{
    free(dsp->matrix);
    free(dsp->frame);
    free(dsp->acc);
    free(dsp->table);
    free(dsp->history);
    memset(dsp, 0, sizeof(*dsp));
}

void resampler_dsp_reset(struct resampler_dsp *dsp)
{
    dsp->history_start = 0;
    dsp->received = 0;
    dsp->next_output = 0;
}

static float read_sample(const BYTE *data, BOOL is_float, UINT bits)
{
    INT32 v;

    if (is_float)
        return bits == 64 ? (float)*(const double *)data : *(const float *)data;
    switch (bits)
    {
        case 8: return (data[0] - 128) / 128.0f;
        case 16: return *(const INT16 *)data / 32768.0f;
        case 24:
            v = (INT32)((UINT32)data[0] << 8 | (UINT32)data[1] << 16 | (UINT32)data[2] << 24) >> 8;
            return v / 8388608.0f;
        default: return (float)(*(const INT32 *)data / 2147483648.0);
    }
}

static double round_away(double v)
{
    return v >= 0.0 ? floor(v + 0.5) : -floor(-v + 0.5);
}

static void write_sample(BYTE *data, BOOL is_float, UINT bits, double value)
{
    double v;
    float f;

    if (is_float)
    {
        if (bits == 64) *(double *)data = value;
        else *(float *)data = (float)value;
        return;
    }
    switch (bits)
    {
        case 8:
            v = min(127.0, max(-127.0, round_away(value * 128.0)));
            data[0] = (BYTE)(v + 128.0);
            break;
        case 16:
            *(INT16 *)data = (INT16)min(32767.0, max(-32767.0, round_away(value * 32768.0)));
            break;
        case 24:
            v = min(8388607.0, max(-8388607.0, round_away(value * 8388608.0)));
            data[0] = (BYTE)((INT32)v);
            data[1] = (BYTE)((INT32)v >> 8);
            data[2] = (BYTE)((INT32)v >> 16);
            break;
        default:
            f = (float)value * 2147483392.0f;
            *(INT32 *)data = (INT32)min(2147483392.0, max(-2147483392.0, round_away(f)));
            break;
    }
}

HRESULT resampler_dsp_push(struct resampler_dsp *dsp, const BYTE *data, UINT frames)
{
    UINT in_size = dsp->in_bits / 8, in_block = in_size * dsp->in_channels, f, i, o;
    SIZE_T held = (SIZE_T)(dsp->received - dsp->history_start), needed = held + frames;
    float *dst;

    if (needed > dsp->history_capacity)
    {
        SIZE_T capacity = max(needed, dsp->history_capacity * 2);
        float *history;

        if (!(history = realloc(dsp->history, capacity * dsp->out_channels * sizeof(*history))))
            return E_OUTOFMEMORY;
        dsp->history = history;
        dsp->history_capacity = capacity;
    }

    dst = dsp->history + held * dsp->out_channels;
    for (f = 0; f < frames; f++, data += in_block, dst += dsp->out_channels)
    {
        for (i = 0; i < dsp->in_channels; i++)
            dsp->frame[i] = read_sample(data + i * in_size, dsp->in_float, dsp->in_bits);
        for (o = 0; o < dsp->out_channels; o++)
        {
            const float *row = dsp->matrix + o * dsp->in_channels;
            float sum = 0.0f;

            for (i = 0; i < dsp->in_channels; i++)
                sum += row[i] * dsp->frame[i];
            dst[o] = sum;
        }
    }
    dsp->received += frames;
    return S_OK;
}

UINT64 resampler_dsp_available(const struct resampler_dsp *dsp, BOOL drain)
{
    UINT64 total, limit;

    if (dsp->bypass)
        return dsp->received - dsp->next_output;
    if (drain)
        total = dsp->received ? (dsp->received - 1) * dsp->out_rate / dsp->in_rate + 1 : 0;
    else
    {
        if (dsp->received <= dsp->lookahead)
            return 0;
        limit = dsp->received - dsp->lookahead;
        total = (limit * dsp->out_rate + dsp->in_rate - 1) / dsp->in_rate;
    }
    return total > dsp->next_output ? total - dsp->next_output : 0;
}

static void trim_history(struct resampler_dsp *dsp)
{
    UINT64 keep;

    if (dsp->bypass)
        keep = dsp->next_output;
    else
    {
        keep = dsp->next_output * dsp->in_rate / dsp->out_rate;
        keep = keep > dsp->taps / 2 ? keep - dsp->taps / 2 : 0;
    }
    if (keep <= dsp->history_start)
        return;
    if (keep > dsp->received)
        keep = dsp->received;
    memmove(dsp->history, dsp->history + (SIZE_T)(keep - dsp->history_start) * dsp->out_channels,
            (SIZE_T)(dsp->received - keep) * dsp->out_channels * sizeof(*dsp->history));
    dsp->history_start = keep;
}

void resampler_dsp_pull(struct resampler_dsp *dsp, BYTE *data, UINT frames)
{
    UINT out_size = dsp->out_bits / 8, channels = dsp->out_channels, f, c, t;
    int start = -(int)(dsp->taps / 2);
    double weights_buffer[256], *weights = weights_buffer, *acc = dsp->acc;
    UINT step = dsp->bypass ? 1 : dsp->out_rate / dsp->phases;

    if (!dsp->bypass && !dsp->table && dsp->taps > ARRAY_SIZE(weights_buffer)
            && !(weights = malloc(dsp->taps * sizeof(*weights))))
        return;

    for (f = 0; f < frames; f++, dsp->next_output++)
    {
        const double *w;
        UINT64 num;
        INT64 index;
        UINT rem;

        if (dsp->bypass)
        {
            const float *src = dsp->history + (SIZE_T)(dsp->next_output - dsp->history_start) * channels;

            for (c = 0; c < channels; c++, data += out_size)
                write_sample(data, dsp->out_float, dsp->out_bits, src[c]);
            continue;
        }

        num = dsp->next_output * dsp->in_rate;
        index = (INT64)(num / dsp->out_rate);
        rem = (UINT)(num % dsp->out_rate);
        if (dsp->table)
            w = dsp->table + (rem / step) * dsp->taps;
        else
        {
            phase_weights(dsp, (double)rem / dsp->out_rate, weights);
            w = weights;
        }

        for (c = 0; c < channels; c++)
            acc[c] = 0.0;
        for (t = 0; t < dsp->taps; t++)
        {
            INT64 j = index + start + (int)t;
            const float *src;

            if (j < (INT64)dsp->history_start || j >= (INT64)dsp->received || w[t] == 0.0)
                continue;
            src = dsp->history + (SIZE_T)(j - (INT64)dsp->history_start) * channels;
            for (c = 0; c < channels; c++)
                acc[c] += w[t] * src[c];
        }
        for (c = 0; c < channels; c++, data += out_size)
            write_sample(data, dsp->out_float, dsp->out_bits, acc[c]);
    }

    if (weights != weights_buffer)
        free(weights);
    trim_history(dsp);
}
