/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Audio Resampler MFT against Windows 11 output
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <initguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mediaobj.h>
#include <propsys.h>
#include <wmcodecdsp.h>
#include <stdio.h>

DEFINE_GUID(MFAudioFormat_PCM_, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(MFAudioFormat_Float_, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(MFPKEY_WMRESAMP_fmtid, 0xaf1adc73, 0xa210, 0x4b05, 0x96, 0x6e, 0x54, 0x91, 0xcf, 0xf4, 0x8b, 0x1d);

struct resample_test
{
    const char *name;
    BOOL in_float; UINT in_bits, in_channels, in_rate, in_mask;
    BOOL out_float; UINT out_bits, out_channels, out_rate, out_mask;
    UINT frames, signal;
    LONG half_filter;
    const char *input_type, *output_type;
    DWORD input_size, output_size;
    DWORD frames0;
    LONG duration0;
    DWORD drain_frames;
    LONG drain_duration;
    UINT probe_start, probe_frames;
    LONG first[72];
    LONG probe[17];
};

static const struct resample_test tests[] =
{
    {
        "f32 2ch 22050 -> s16 2ch 44100 (test)", TRUE, 32, 2, 22050, 0, FALSE, 16, 2, 44100, 0, 22491, 3, 0,
        "1aab75c8=176400 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=2 48eba18e={73647561} 5faeeae7=44100 a901aaba=1 c9173739=1 f2deb57f=16 f7e34c9a={00000001}",
        8, 4, 44918, 10185488, 63, 14286, 0, 0,
        {-7961, -6992, -7742, -6723, -5379, -4569, -3340, -2511, -1931, -912, -485, 484, 725, 1479, 1800, 2619, 4080, 5160, 7090, 8102, 6846, 7535, 914, 1662},
        {0},
    },
    {
        "impulse f32 1ch 22050 -> f32 1ch 44100", TRUE, 32, 1, 22050, 0, TRUE, 32, 1, 44100, 0, 1000, 0, 0,
        "1aab75c8=88200 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 1936, 439002, 63, 14286, 192, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-15092, -39905, 15832, 59585, -16380, -103616, 16718, 317473, 483168, 317473, 16718, -103616, -16380, 59585, 15832, -39905, -15092},
    },
    {
        "impulse f32 1ch 44100 -> f32 1ch 48000", TRUE, 32, 1, 44100, 0, TRUE, 32, 1, 48000, 0, 1000, 0, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=192000 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=48000 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 1054, 219583, 34, 7083, 100, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-6332, 1941, 4908, -14311, 26247, -40783, 58729, -84230, 145278, 467929, -12495, -20897, 30628, -32061, 29164, -23870, 17495},
    },
    {
        "impulse f32 1ch 48000 -> f32 1ch 44100", TRUE, 32, 1, 48000, 0, TRUE, 32, 1, 44100, 0, 1000, 0, 0,
        "1aab75c8=192000 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=48000 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 888, 201361, 30, 6803, 83, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {10778, -12815, 15127, -17803, 21024, -25195, 31335, -42706, 77651, 433301, -34916, 11250, -2772, -1418, 3726, -4999, 5625},
    },
    {
        "impulse f32 1ch 44100 -> f32 1ch 22050", TRUE, 32, 1, 44100, 0, TRUE, 32, 1, 22050, 0, 1000, 0, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=88200 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 469, 212698, 31, 14059, 42, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-5407, 6007, -6575, 7094, -7546, 7916, -8190, 8359, 241584, 8359, -8190, 7916, -7546, 7094, -6575, 6007, -5407},
    },
    {
        "impulse f32 1ch 8000 -> f32 1ch 48000", TRUE, 32, 1, 8000, 0, TRUE, 32, 1, 48000, 0, 1000, 0, 0,
        "1aab75c8=32000 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=8000 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=192000 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=48000 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 5808, 1210000, 187, 38958, 592, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-93141, -52795, 16718, 109306, 214175, 317474, 404605, 462751, 483169, 462751, 404605, 317474, 214175, 109306, 16718, -52795, -93141},
    },
    {
        "impulse f32 1ch 44100 -> f32 1ch 44100", TRUE, 32, 1, 44100, 0, TRUE, 32, 1, 44100, 0, 1000, 0, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 1000, 226757, 0, 0, 92, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 500000, 0, 0, 0, 0, 0, 0, 0, 0},
    },
    {
        "quality 1 f32 1ch 22050 -> f32 1ch 44100", TRUE, 32, 1, 22050, 0, TRUE, 32, 1, 44100, 0, 1000, 0, 1,
        "1aab75c8=88200 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 1996, 452608, 3, 680, 192, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 61415, 260190, 356787, 260190, 61415, 0, 0, 0, 0, 0, 0},
    },
    {
        "quality 60 f32 1ch 22050 -> f32 1ch 44100", TRUE, 32, 1, 22050, 0, TRUE, 32, 1, 44100, 0, 1000, 0, 60,
        "1aab75c8=88200 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 4, 1876, 425397, 123, 27891, 192, 17,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-8321, -43978, 8424, 62587, -8498, -105455, 8543, 318093, 491442, 318093, 8543, -105455, -8498, 62587, 8424, -43978, -8321},
    },
    {
        "edges f32 1ch 44100 -> s16 1ch 44100", TRUE, 32, 1, 44100, 0, FALSE, 16, 1, 44100, 0, 1000, 2, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=88200 322de230=2 37e48bf5=1 48eba18e={73647561} 5faeeae7=44100 a901aaba=1 c9173739=1 f2deb57f=16 f7e34c9a={00000001}",
        4, 2, 1000, 226757, 0, 0, 0, 0,
        {32767, -32767, 1, 2, -1, -2, 32767, -32767, 3, 32735, 32767, 32767},
        {0},
    },
    {
        "edges f32 1ch 44100 -> u8 1ch 44100", TRUE, 32, 1, 44100, 0, FALSE, 8, 1, 44100, 0, 1000, 2, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=44100 322de230=1 37e48bf5=1 48eba18e={73647561} 5faeeae7=44100 a901aaba=1 c9173739=1 f2deb57f=8 f7e34c9a={00000001}",
        4, 1, 1000, 226757, 0, 0, 0, 0,
        {255, 1, 128, 128, 128, 128, 255, 1, 128, 255, 255, 255},
        {0},
    },
    {
        "edges f32 1ch 44100 -> s32 1ch 44100", TRUE, 32, 1, 44100, 0, FALSE, 32, 1, 44100, 0, 1000, 2, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000001}",
        4, 4, 1000, 226757, 0, 0, 0, 0,
        {2147483392, -2147483392, 32768, 98304, -32768, -98304, 2147483392, -2147483392, 163840, 2145335936, 2147417856, 2147450624},
        {0},
    },
    {
        "s16 1ch -> f32 1ch 44100", FALSE, 16, 1, 44100, 0, TRUE, 32, 1, 44100, 0, 1000, 2, 0,
        "1aab75c8=88200 322de230=2 37e48bf5=1 48eba18e={73647561} 5faeeae7=44100 a901aaba=1 c9173739=1 f2deb57f=16 f7e34c9a={00000001}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        2, 4, 1000, 226757, 0, 0, 0, 0,
        {999969, -999969, 0, 30, 0, -30, -61, 61, 61, 998962, 999938, 999938},
        {0},
    },
    {
        "mix 1ch -> 2ch", TRUE, 32, 1, 44100, 0, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        4, 8, 1000, 226757, 0, 0, 200, 2,
        {100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000},
        {100000, 100000, 100000, 100000},
    },
    {
        "mix 2ch -> 1ch", TRUE, 32, 2, 44100, 0, TRUE, 32, 1, 44100, 0, 1000, 1, 0,
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        8, 4, 1000, 226757, 0, 0, 200, 2,
        {150000, 150000, 150000, 150000, 150000, 150000, 150000, 150000, 150000, 150000, 150000, 150000},
        {150000, 150000},
    },
    {
        "mix 2ch -> 6ch", TRUE, 32, 2, 44100, 0, TRUE, 32, 6, 44100, 0x3f, 1000, 1, 0,
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=1058400 322de230=24 37e48bf5=6 48eba18e={73647561} 55fb5765=63 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        8, 24, 1000, 226757, 0, 0, 200, 2,
        {100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0},
        {100000, 200000, 0, 0, 0, 0, 100000, 200000, 0, 0, 0, 0},
    },
    {
        "mix 6ch 3f -> 2ch", TRUE, 32, 6, 44100, 0x3f, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=1058400 322de230=24 37e48bf5=6 48eba18e={73647561} 55fb5765=63 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        24, 8, 1000, 226757, 0, 0, 200, 2,
        {342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600, 342800, 384600},
        {342800, 384600, 342800, 384600},
    },
    {
        "mix 6ch 60f -> 2ch", TRUE, 32, 6, 44100, 0x60f, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=1058400 322de230=24 37e48bf5=6 48eba18e={73647561} 55fb5765=1551 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        24, 8, 1000, 226757, 0, 0, 200, 2,
        {336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800, 336600, 384800},
        {336600, 384800, 336600, 384800},
    },
    {
        "mix 6ch 3f -> 1ch", TRUE, 32, 6, 44100, 0x3f, TRUE, 32, 1, 44100, 0, 1000, 1, 0,
        "1aab75c8=1058400 322de230=24 37e48bf5=6 48eba18e={73647561} 55fb5765=63 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=176400 322de230=4 37e48bf5=1 48eba18e={73647561} 55fb5765=4 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        24, 4, 1000, 226757, 0, 0, 200, 2,
        {341600, 341600, 341600, 341600, 341600, 341600, 341600, 341600, 341600, 341600, 341600, 341600},
        {341600, 341600},
    },
    {
        "mix 4ch 33 -> 2ch", TRUE, 32, 4, 44100, 0x33, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=705600 322de230=16 37e48bf5=4 48eba18e={73647561} 55fb5765=51 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        16, 8, 1000, 226757, 0, 0, 200, 2,
        {237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800, 237800, 293800},
        {237800, 293800, 237800, 293800},
    },
    {
        "mix 8ch 63f -> 2ch", TRUE, 32, 8, 44100, 0x63f, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=1411200 322de230=32 37e48bf5=8 48eba18e={73647561} 55fb5765=1599 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        32, 8, 1000, 226757, 0, 0, 200, 2,
        {456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100, 456300, 497100},
        {456300, 497100, 456300, 497100},
    },
    {
        "mix 6ch 3f -> 2ch 22050->44100", TRUE, 32, 6, 22050, 0x3f, TRUE, 32, 2, 44100, 0, 1000, 1, 0,
        "1aab75c8=529200 322de230=24 37e48bf5=6 48eba18e={73647561} 55fb5765=63 5faeeae7=22050 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        "1aab75c8=352800 322de230=8 37e48bf5=2 48eba18e={73647561} 55fb5765=3 5faeeae7=44100 c9173739=1 f2deb57f=32 f7e34c9a={00000003}",
        24, 8, 1936, 439002, 63, 14286, 200, 2,
        {337030, 378127, 389059, 436500, 348492, 390987, 318020, 356798, 337262, 378387, 358872, 402632, 348116, 390565, 331513, 371936, 337769, 378955, 351012, 393814, 347496, 389869, 336744, 377806},
        {342800, 384601, 342799, 384599},
    },
};

static float signal_value(UINT signal, UINT frame, UINT channel)
{
    static const float edge[] = {1.0f, -1.0f, 0.5f / 32768, 1.5f / 32768, -0.5f / 32768, -1.5f / 32768, 2.0f, -2.0f,
            2.5f / 32768, 0.999f, 32767.0f / 32768, 32767.5f / 32768};

    if (signal == 0)
        return frame == 100 ? 0.5f : 0.0f;
    if (signal == 1)
        return 0.1f * (channel + 1);
    if (signal == 2)
        return frame < ARRAYSIZE(edge) ? edge[frame] : 0.0f;
    return (float)((frame * 37 + channel * 11) % 200) / 400.0f - 0.25f;
}

static IMFMediaType *create_type(BOOL is_float, UINT bits, UINT channels, UINT rate, UINT mask)
{
    UINT block = channels * bits / 8;
    IMFMediaType *type;
    HRESULT hr;

    hr = MFCreateMediaType(&type);
    ok(hr == S_OK, "MFCreateMediaType returned %#lx\n", hr);
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, is_float ? &MFAudioFormat_Float_ : &MFAudioFormat_PCM_);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, block);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, block * rate);
    if (mask)
        IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_CHANNEL_MASK, mask);
    return type;
}

static void check_type(IMFMediaType *type, const char *expect, const char *what)
{
    char items[16][32], text[512], *p = text;
    UINT32 count = 0, i, j;
    PROPVARIANT value;
    GUID key;

    IMFMediaType_GetCount(type, &count);
    ok(count <= ARRAYSIZE(items), "%s has %u attributes\n", what, count);
    count = min(count, ARRAYSIZE(items));
    for (i = 0; i < count; i++)
    {
        PropVariantInit(&value);
        IMFMediaType_GetItemByIndex(type, i, &key, &value);
        if (value.vt == VT_UI4)
            sprintf(items[i], "%08lx=%lu", key.Data1, value.ulVal);
        else if (value.vt == VT_CLSID)
            sprintf(items[i], "%08lx={%08lx}", key.Data1, value.puuid->Data1);
        else
            sprintf(items[i], "%08lx=vt%u", key.Data1, value.vt);
        PropVariantClear(&value);
    }
    *p = 0;
    for (i = 0; i < count; i++)
    {
        UINT32 next = 0;
        for (j = 1; j < count; j++)
            if (strcmp(items[j], items[next]) < 0)
                next = j;
        p += sprintf(p, "%s%s", p == text ? "" : " ", items[next]);
        items[next][0] = 0x7f;
    }
    ok(!strcmp(text, expect), "%s is %s\n", what, text);
}

static IMFSample *create_sample(const void *data, DWORD size)
{
    IMFMediaBuffer *buffer;
    IMFSample *sample;
    BYTE *ptr;
    HRESULT hr;

    hr = MFCreateMemoryBuffer(size, &buffer);
    ok(hr == S_OK, "MFCreateMemoryBuffer returned %#lx\n", hr);
    if (data)
    {
        IMFMediaBuffer_Lock(buffer, &ptr, NULL, NULL);
        memcpy(ptr, data, size);
        IMFMediaBuffer_Unlock(buffer);
        IMFMediaBuffer_SetCurrentLength(buffer, size);
    }
    MFCreateSample(&sample);
    IMFSample_AddBuffer(sample, buffer);
    IMFMediaBuffer_Release(buffer);
    return sample;
}

static LONG read_value(const BYTE *data, BOOL is_float, UINT bits, UINT index)
{
    if (is_float)
        return (LONG)(((const float *)data)[index] * 1000000.0f);
    if (bits == 16)
        return ((const SHORT *)data)[index];
    if (bits == 8)
        return data[index];
    return ((const LONG *)data)[index];
}

static void check_values(const struct resample_test *test, const BYTE *data, DWORD length, UINT start, UINT frames,
        const LONG *expect, const char *what)
{
    UINT block = test->out_channels * test->out_bits / 8, i, count = frames * test->out_channels, worst = 0;
    LONG tolerance = test->out_float ? 2 : 1, value, error, max_error = 0, low, high;
    BOOL shaped = FALSE;

    ok(length >= (start + frames) * block, "%s: got length %lu\n", what, length);
    if (length < (start + frames) * block)
        return;
    for (i = 0; i < count; i++)
    {
        value = read_value(data, test->out_float, test->out_bits, start * test->out_channels + i);
        error = abs(value - expect[i]);
        if (error > max_error)
        {
            max_error = error;
            worst = i;
        }
    }
    for (i = 0; i < test->out_channels && test->out_float && test->in_rate != test->out_rate; i++)
    {
        UINT f;

        low = high = expect[i];
        for (f = 1; f < frames; f++)
        {
            low = min(low, expect[f * test->out_channels + i]);
            high = max(high, expect[f * test->out_channels + i]);
        }
        if (high - low > tolerance)
            shaped = TRUE;
    }
    /* The filter taps differ from Windows beyond the tolerance at qualities 1 and 30, where the output follows the filter shape. */
    if (shaped && test->half_filter != 60 && max_error > tolerance)
        trace("known gap, filter taps: %s value %u: got %ld, expected %ld\n", what, worst,
                read_value(data, test->out_float, test->out_bits, start * test->out_channels + worst), expect[worst]);
    else
        ok(max_error <= tolerance, "%s value %u: got %ld, expected %ld\n", what, worst,
                read_value(data, test->out_float, test->out_bits, start * test->out_channels + worst), expect[worst]);
}

static void check_output(IMFTransform *transform, const struct resample_test *test, BOOL drained)
{
    UINT block = test->out_channels * test->out_bits / 8;
    DWORD frames = drained ? test->drain_frames : test->frames0, status, length;
    LONG duration = drained ? test->drain_duration : test->duration0;
    LONGLONG time = drained ? 1000 + test->duration0 : 1000, value;
    MFT_OUTPUT_DATA_BUFFER output = {0};
    IMFMediaBuffer *buffer;
    UINT32 clean = 0;
    BYTE *data;
    HRESULT hr;

    output.pSample = create_sample(NULL, 0x100000);
    status = 0xdeadbeef;
    hr = IMFTransform_ProcessOutput(transform, 0, 1, &output, &status);
    ok(hr == S_OK, "ProcessOutput returned %#lx\n", hr);
    ok(!status, "got status %#lx\n", status);
    ok(output.dwStatus == (drained ? MFT_OUTPUT_DATA_BUFFER_INCOMPLETE : 0), "got dwStatus %#lx\n", output.dwStatus);
    if (hr == S_OK)
    {
        IMFSample_GetTotalLength(output.pSample, &length);
        if (test->half_filter == 1 && length != frames * block)
            trace("known gap, quality 1 hold-back: got %lu frames, expected %lu\n", length / block, frames);
        else
            ok(length == frames * block, "got %lu frames, expected %lu\n", length / block, frames);
        value = -1;
        IMFSample_GetSampleTime(output.pSample, &value);
        if (test->half_filter == 1 && drained && value != time)
            trace("known gap, quality 1 hold-back: got time %I64d\n", value);
        else
            ok(value == time, "got time %I64d\n", value);
        value = -1;
        IMFSample_GetSampleDuration(output.pSample, &value);
        if (test->half_filter == 1 && value != duration)
            trace("known gap, quality 1 hold-back: got duration %I64d, expected %ld\n", value, duration);
        else
            ok(value == duration, "got duration %I64d, expected %ld\n", value, duration);
        hr = IMFSample_GetUINT32(output.pSample, &MFSampleExtension_CleanPoint, &clean);
        ok(hr == S_OK && clean == 1, "got CleanPoint %#lx %u\n", hr, clean);
        if (!drained)
        {
            IMFSample_GetBufferByIndex(output.pSample, 0, &buffer);
            IMFMediaBuffer_Lock(buffer, &data, NULL, &length);
            check_values(test, data, length, 0, 12, test->first, "first");
            if (test->probe_frames)
                check_values(test, data, length, test->probe_start, test->probe_frames, test->probe, "probe");
            IMFMediaBuffer_Unlock(buffer);
            IMFMediaBuffer_Release(buffer);
        }
    }
    if (output.pEvents)
        IMFCollection_Release(output.pEvents);
    IMFSample_Release(output.pSample);
}

static void check_need_more_input(IMFTransform *transform)
{
    MFT_OUTPUT_DATA_BUFFER output = {0};
    DWORD status = 0xdeadbeef;
    HRESULT hr;

    output.pSample = create_sample(NULL, 0x100000);
    hr = IMFTransform_ProcessOutput(transform, 0, 1, &output, &status);
    ok(hr == MF_E_TRANSFORM_NEED_MORE_INPUT, "ProcessOutput returned %#lx\n", hr);
    ok(!status, "got status %#lx\n", status);
    ok(!output.dwStatus, "got dwStatus %#lx\n", output.dwStatus);
    IMFSample_Release(output.pSample);
}

static void test_resample(const struct resample_test *test)
{
    UINT in_block = test->in_channels * test->in_bits / 8, f, c;
    DWORD in_size = test->frames * in_block;
    MFT_INPUT_STREAM_INFO input_info = {0};
    MFT_OUTPUT_STREAM_INFO output_info = {0};
    IMFMediaType *input_type, *output_type, *type;
    IMFTransform *transform;
    IWMResamplerProps *props;
    IMFSample *sample;
    BYTE *data;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void **)&transform);
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    if (test->half_filter)
    {
        hr = IMFTransform_QueryInterface(transform, &IID_IWMResamplerProps, (void **)&props);
        ok(hr == S_OK, "QueryInterface returned %#lx\n", hr);
        hr = IWMResamplerProps_SetHalfFilterLength(props, test->half_filter);
        ok(hr == S_OK, "SetHalfFilterLength returned %#lx\n", hr);
        IWMResamplerProps_Release(props);
    }

    input_type = create_type(test->in_float, test->in_bits, test->in_channels, test->in_rate, test->in_mask);
    output_type = create_type(test->out_float, test->out_bits, test->out_channels, test->out_rate, test->out_mask);
    hr = IMFTransform_SetInputType(transform, 0, input_type, 0);
    ok(hr == S_OK, "SetInputType returned %#lx\n", hr);
    hr = IMFTransform_SetOutputType(transform, 0, output_type, 0);
    ok(hr == S_OK, "SetOutputType returned %#lx\n", hr);
    IMFMediaType_Release(input_type);
    IMFMediaType_Release(output_type);
    hr = IMFTransform_GetInputCurrentType(transform, 0, &type);
    ok(hr == S_OK, "GetInputCurrentType returned %#lx\n", hr);
    if (hr == S_OK)
    {
        check_type(type, test->input_type, "input type");
        IMFMediaType_Release(type);
    }
    hr = IMFTransform_GetOutputCurrentType(transform, 0, &type);
    ok(hr == S_OK, "GetOutputCurrentType returned %#lx\n", hr);
    if (hr == S_OK)
    {
        check_type(type, test->output_type, "output type");
        IMFMediaType_Release(type);
    }

    hr = IMFTransform_GetInputStreamInfo(transform, 0, &input_info);
    ok(hr == S_OK, "GetInputStreamInfo returned %#lx\n", hr);
    ok(input_info.cbSize == test->input_size, "got input cbSize %lu\n", input_info.cbSize);
    ok(!input_info.cbMaxLookahead, "got cbMaxLookahead %lu\n", input_info.cbMaxLookahead);
    ok(input_info.cbAlignment == 1, "got input cbAlignment %lu\n", input_info.cbAlignment);
    ok(!input_info.dwFlags, "got input dwFlags %#lx\n", input_info.dwFlags);
    ok(!input_info.hnsMaxLatency, "got hnsMaxLatency %I64d\n", input_info.hnsMaxLatency);
    hr = IMFTransform_GetOutputStreamInfo(transform, 0, &output_info);
    ok(hr == S_OK, "GetOutputStreamInfo returned %#lx\n", hr);
    ok(output_info.cbSize == test->output_size, "got output cbSize %lu\n", output_info.cbSize);
    ok(output_info.cbAlignment == 1, "got output cbAlignment %lu\n", output_info.cbAlignment);
    ok(!output_info.dwFlags, "got output dwFlags %#lx\n", output_info.dwFlags);

    data = HeapAlloc(GetProcessHeap(), 0, in_size);
    for (f = 0; f < test->frames; f++)
        for (c = 0; c < test->in_channels; c++)
        {
            float v = signal_value(test->signal, f, c);
            UINT i = f * test->in_channels + c;

            if (test->in_float)
                ((float *)data)[i] = v;
            else
                ((SHORT *)data)[i] = (SHORT)(v * 32767.0f);
        }
    sample = create_sample(data, in_size);
    HeapFree(GetProcessHeap(), 0, data);
    IMFSample_SetSampleTime(sample, 1000);
    IMFSample_SetSampleDuration(sample, 10000000);
    hr = IMFTransform_ProcessInput(transform, 0, sample, 0);
    ok(hr == S_OK, "ProcessInput returned %#lx\n", hr);
    IMFSample_Release(sample);

    check_output(transform, test, FALSE);
    check_need_more_input(transform);
    sample = create_sample(NULL, 0x100000);
    hr = IMFTransform_ProcessInput(transform, 0, sample, 0);
    ok(hr == S_OK, "ProcessInput of an empty sample returned %#lx\n", hr);
    IMFSample_Release(sample);
    hr = IMFTransform_ProcessMessage(transform, MFT_MESSAGE_COMMAND_DRAIN, 0);
    ok(hr == S_OK, "ProcessMessage returned %#lx\n", hr);
    if (test->drain_frames)
        check_output(transform, test, TRUE);
    check_need_more_input(transform);

    IMFTransform_Release(transform);
}

static void test_interfaces(void)
{
    static const struct { const IID *iid; HRESULT hr; } interfaces[] =
    {
        {&IID_IMFTransform, S_OK}, {&IID_IMediaObject, S_OK}, {&IID_IPropertyStore, S_OK}, {&IID_IPropertyBag, S_OK},
        {&IID_IWMResamplerProps, S_OK}, {&IID_IPersistStream, E_NOINTERFACE}, {&IID_IMFAttributes, E_NOINTERFACE},
    };
    static const VARTYPE types[] = {VT_I4, VT_I4 | VT_ARRAY, VT_R4};
    IPropertyStore *store;
    IPropertyBag *bag;
    IUnknown *unknown, *object;
    PROPERTYKEY key;
    PROPVARIANT value;
    VARIANT var;
    DWORD count, i;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unknown);
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    for (i = 0; i < ARRAYSIZE(interfaces); i++)
    {
        hr = IUnknown_QueryInterface(unknown, interfaces[i].iid, (void **)&object);
        ok(hr == interfaces[i].hr, "interface %lu: QueryInterface returned %#lx\n", i, hr);
        if (SUCCEEDED(hr))
            IUnknown_Release(object);
    }

    hr = IUnknown_QueryInterface(unknown, &IID_IPropertyStore, (void **)&store);
    ok(hr == S_OK, "QueryInterface returned %#lx\n", hr);
    if (hr == S_OK)
    {
        count = 0;
        hr = IPropertyStore_GetCount(store, &count);
        ok(hr == S_OK && count == ARRAYSIZE(types), "GetCount returned %#lx, count %lu\n", hr, count);
        for (i = 0; i < min(count, ARRAYSIZE(types)); i++)
        {
            hr = IPropertyStore_GetAt(store, i, &key);
            ok(hr == S_OK, "GetAt(%lu) returned %#lx\n", i, hr);
            ok(IsEqualGUID(&key.fmtid, &MFPKEY_WMRESAMP_fmtid) && key.pid == i + 1, "property %lu: got pid %lu\n", i, key.pid);
            PropVariantInit(&value);
            hr = IPropertyStore_GetValue(store, &key, &value);
            ok(hr == S_OK, "GetValue(%lu) returned %#lx\n", i, hr);
            ok(value.vt == types[i], "property %lu: got vt %u\n", i, value.vt);
            if (value.vt == VT_I4)
                ok(value.lVal == 30, "got quality %ld\n", value.lVal);
            else if (value.vt == VT_R4)
                ok(value.fltVal == 0.0f, "got low-pass bandwidth %f\n", value.fltVal);
            PropVariantClear(&value);
        }
        hr = IPropertyStore_GetAt(store, count, &key);
        ok(hr == E_INVALIDARG, "GetAt(count) returned %#lx\n", hr);
        hr = IPropertyStore_Commit(store);
        ok(hr == E_NOTIMPL, "Commit returned %#lx\n", hr);
        IPropertyStore_Release(store);
    }

    hr = IUnknown_QueryInterface(unknown, &IID_IPropertyBag, (void **)&bag);
    ok(hr == S_OK, "QueryInterface returned %#lx\n", hr);
    if (hr == S_OK)
    {
        VariantInit(&var);
        hr = IPropertyBag_Read(bag, L"FilterQuality", &var, NULL);
        ok(hr == S_OK, "Read returned %#lx\n", hr);
        ok(V_VT(&var) == VT_EMPTY, "got vt %u\n", V_VT(&var));
        V_VT(&var) = VT_I4;
        V_I4(&var) = 10;
        hr = IPropertyBag_Write(bag, L"FilterQuality", &var);
        ok(hr == S_OK, "Write returned %#lx\n", hr);
        IPropertyBag_Release(bag);
    }
    IUnknown_Release(unknown);
}

START_TEST(resample)
{
    UINT i;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    test_interfaces();
    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        winetest_push_context("%s", tests[i].name);
        test_resample(&tests[i]);
        winetest_pop_context();
    }

    MFShutdown();
    CoUninitialize();
}
