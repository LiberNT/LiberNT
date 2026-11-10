
/*
 * PROJECT:     LiberNT API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWM stack API-test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_dwmstack(void);
extern void func_caption_cache(void);
extern void func_client_format(void);
extern void func_event_poll(void);
extern void func_gpu_retry(void);
extern void func_mode_switch(void);
extern void func_pointer_image(void);
extern void func_scene_cache(void);

const struct test winetest_testlist[] =
{
    { "dwmstack", func_dwmstack },
    { "caption_cache", func_caption_cache },
    { "client_format", func_client_format },
    { "event_poll", func_event_poll },
    { "gpu_retry", func_gpu_retry },
    { "mode_switch", func_mode_switch },
    { "pointer_image", func_pointer_image },
    { "scene_cache", func_scene_cache },
    { 0, 0 }
};
