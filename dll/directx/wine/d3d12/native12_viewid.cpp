/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     View instancing interstage storage validation with the DXC view ID validator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define NOMINMAX
#include <windows.h>
#include <assert.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#define DXASSERT(expression, message) ((void)sizeof(!!(expression)))
#define DXASSERT_NOMSG(expression) ((void)sizeof(!!(expression)))

#include "dxc/DXIL/DxilConstants.h"
#include "dxc/DXIL/DxilSigPoint.h"
#include "dxc/HLSL/DxilSignatureAllocator.h"
#include "dxc/DxilContainer/DxilPipelineStateValidation.h"
#include "dxc/DXIL/DxilSigPoint.inl"
#include "dxc/HLSL/DxilSignatureAllocator.inl"
#include "dxc/HLSL/ViewIDPipelineValidation.inl"

#include "native12_viewid.h"

HRESULT Native12ValidateViewIds(UINT view_count, UINT rasterized_stream, const Native12ViewIdStage *stages,
        UINT stage_count, UINT *failed_stage, UINT *result)
{
    std::unique_ptr<hlsl::ViewIDValidator> validator(hlsl::NewViewIDValidator(view_count, rasterized_stream));
    bool expand_input_only = false, last = false;

    for (UINT i = 0; i < stage_count && !last; ++i)
    {
        DxilPipelineStateValidation psv;
        unsigned mismatch = 0;

        last = expand_input_only;
        *failed_stage = i;
        if (!psv.InitFromPSV0(stages[i].psv, stages[i].psv_size))
        {
            *result = static_cast<UINT>(hlsl::ViewIDValidator::Result::InvalidPSV);
            return E_INVALIDARG;
        }
        hlsl::ViewIDValidator::Result status = validator->ValidateStage(psv, i + 1 == stage_count, expand_input_only,
                mismatch);
        *result = static_cast<UINT>(status);
        if (status == hlsl::ViewIDValidator::Result::SuccessWithViewIDDependentTessFactor)
        {
            expand_input_only = true;
            continue;
        }
        if (status != hlsl::ViewIDValidator::Result::Success) return E_INVALIDARG;
    }
    return S_OK;
}
