/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     MPEG-1 video (ISO/IEC 11172-2) decoder core
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#ifndef __MPEG1VIDEO_H
#define __MPEG1VIDEO_H

#include <stddef.h>
#include <stdint.h>

struct mpeg1video_frame
{
    uint8_t *planes[3];
    unsigned int strides[3];
    unsigned int width, height;
    int64_t time;
    int has_time;
    int type;
};

struct mpeg1video;

struct mpeg1video *mpeg1video_create(void);
void mpeg1video_destroy(struct mpeg1video *dec);
void mpeg1video_reset(struct mpeg1video *dec);
int mpeg1video_push(struct mpeg1video *dec, const uint8_t *data, size_t size, int has_time, int64_t time);
const struct mpeg1video_frame *mpeg1video_next(struct mpeg1video *dec, int eos);

#endif
