/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     View instancing interstage storage validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

struct Native12ViewIdStage
{
    const void *psv;
    UINT psv_size;
};

HRESULT Native12ValidateViewIds(UINT view_count, UINT rasterized_stream, const Native12ViewIdStage *stages,
        UINT stage_count, UINT *failed_stage, UINT *result);
