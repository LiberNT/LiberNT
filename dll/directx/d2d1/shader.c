/*
 * Copyright 2014 Henri Verbeet for CodeWeavers, 2026 Earzu Chan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "device_private.h"
#include <float.h>
#include <d3dcompiler.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_outline[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_curve_outline[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_triangle[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_triangle_aa[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COVERAGE", 0, DXGI_FORMAT_R32_FLOAT,    0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_curve[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const char shape_vs_code_outline[] =
    "float3x2 transform_geometry;\n"
    "float stroke_width;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "/* The lines PₚᵣₑᵥP₀ and P₀Pₙₑₓₜ, both offset by ±½w, intersect each other at:\n"
    " *\n"
    " *   Pᵢ = P₀ ± w · ½q⃑ᵢ.\n"
    " *\n"
    " * Where:\n"
    " *\n"
    " *   q⃑ᵢ = q̂ₚᵣₑᵥ⊥ + tan(½θ) · -q̂ₚᵣₑᵥ\n"
    " *   θ  = ∠PₚᵣₑᵥP₀Pₙₑₓₜ\n"
    " *   q⃑ₚᵣₑᵥ = P₀ - Pₚᵣₑᵥ */\n"
    "void main(float2 position : POSITION, float2 prev : PREV, float2 next : NEXT, out struct output o)\n"
    "{\n"
    "    float2 q_prev, q_next, v_p, q_i;\n"
    "    float2x2 geom;\n"
    "    float l;\n"
    "\n"
    "    o.stroke_transform = float2x2(transform_rtx.xy, transform_rty.xy) * stroke_width * 0.5f;\n"
    "\n"
    "    geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);\n"
    "    q_prev = normalize(mul(geom, prev));\n"
    "    q_next = normalize(mul(geom, next));\n"
    "\n"
    "    /* tan(½θ) = sin(θ) / (1 + cos(θ))\n"
    "     *         = (q̂ₚᵣₑᵥ⊥ · q̂ₙₑₓₜ) / (1 + (q̂ₚᵣₑᵥ · q̂ₙₑₓₜ)) */\n"
    "    v_p = float2(-q_prev.y, q_prev.x);\n"
    "    l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));\n"
    "    q_i = l * q_prev + v_p;\n"
    "\n"
    "    o.b = float4(0.0, 0.0, 0.0, 0.0);\n"
    "\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry) + stroke_width * 0.5f * q_i;\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
/*     ⎡p0.x p0.y 1⎤
 * A = ⎢p1.x p1.y 1⎥
 *     ⎣p2.x p2.y 1⎦
 *
 *     ⎡0 0⎤
 * B = ⎢½ 0⎥
 *     ⎣1 1⎦
 *
 * A' = ⎡p1.x-p0.x p1.y-p0.y⎤
 *      ⎣p2.x-p0.x p2.y-p0.y⎦
 *
 * B' = ⎡½ 0⎤
 *      ⎣1 1⎦
 *
 * A'T = B'
 * T = A'⁻¹B'
 */
static const char shape_vs_code_bezier_outline[] =
    "float3x2 transform_geometry;\n"
    "float stroke_width;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "void main(float2 position : POSITION, float2 p0 : P0, float2 p1 : P1, float2 p2 : P2,\n"
    "        float2 prev : PREV, float2 next : NEXT, out struct output o)\n"
    "{\n"
    "    float2 q_prev, q_next, v_p, q_i, p;\n"
    "    float2x2 geom, rt;\n"
    "    float l;\n"
    "\n"
    "    geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);\n"
    "    rt = float2x2(transform_rtx.xy, transform_rty.xy);\n"
    "    o.stroke_transform = rt * stroke_width * 0.5f;\n"
    "\n"
    "    p = mul(geom, position);\n"
    "    p0 = mul(geom, p0);\n"
    "    p1 = mul(geom, p1);\n"
    "    p2 = mul(geom, p2);\n"
    "\n"
    "    p -= p0;\n"
    "    p1 -= p0;\n"
    "    p2 -= p0;\n"
    "\n"
    "    q_prev = normalize(mul(geom, prev));\n"
    "    q_next = normalize(mul(geom, next));\n"
    "\n"
    "    v_p = float2(-q_prev.y, q_prev.x);\n"
    "    l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));\n"
    "    q_i = l * q_prev + v_p;\n"
    "    p += 0.5f * stroke_width * q_i;\n"
    "\n"
    "    v_p = mul(rt, p2);\n"
    "    v_p = normalize(float2(-v_p.y, v_p.x));\n"
    "    if (abs(dot(mul(rt, p1), v_p)) < 1.0f)\n"
    "    {\n"
    "        o.b.xzw = float3(0.0f, 0.0f, 0.0f);\n"
    "        o.b.y = dot(mul(rt, p), v_p);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        o.b.zw = sign(dot(mul(rt, p1), v_p)) * v_p;\n"
    "        v_p = -float2(-p.y, p.x) / dot(float2(-p1.y, p1.x), p2);\n"
    "        o.b.x = dot(v_p, p1 - 0.5f * p2);\n"
    "        o.b.y = dot(v_p, p1);\n"
    "    }\n"
    "\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry) + 0.5f * stroke_width * q_i;\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
/*     ⎡p0.x p0.y 1⎤
 * A = ⎢p1.x p1.y 1⎥
 *     ⎣p2.x p2.y 1⎦
 *
 *     ⎡1 0⎤
 * B = ⎢1 1⎥
 *     ⎣0 1⎦
 *
 * A' = ⎡p1.x-p0.x p1.y-p0.y⎤
 *      ⎣p2.x-p0.x p2.y-p0.y⎦
 *
 * B' = ⎡ 0 1⎤
 *      ⎣-1 1⎦
 *
 * A'T = B'
 * T = A'⁻¹B' = (B'⁻¹A')⁻¹
 */
static const char shape_vs_code_arc_outline[] =
    "float3x2 transform_geometry;\n"
    "float stroke_width;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "void main(float2 position : POSITION, float2 p0 : P0, float2 p1 : P1, float2 p2 : P2,\n"
    "        float2 prev : PREV, float2 next : NEXT, out struct output o)\n"
    "{\n"
    "    float2 q_prev, q_next, v_p, q_i, p;\n"
    "    float2x2 geom, rt, p_inv;\n"
    "    float l;\n"
    "    float a;\n"
    "    float2 bc;\n"
    "\n"
    "    geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);\n"
    "    rt = float2x2(transform_rtx.xy, transform_rty.xy);\n"
    "    o.stroke_transform = rt * stroke_width * 0.5f;\n"
    "\n"
    "    p = mul(geom, position);\n"
    "    p0 = mul(geom, p0);\n"
    "    p1 = mul(geom, p1);\n"
    "    p2 = mul(geom, p2);\n"
    "\n"
    "    p -= p0;\n"
    "    p1 -= p0;\n"
    "    p2 -= p0;\n"
    "\n"
    "    q_prev = normalize(mul(geom, prev));\n"
    "    q_next = normalize(mul(geom, next));\n"
    "\n"
    "    v_p = float2(-q_prev.y, q_prev.x);\n"
    "    l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));\n"
    "    q_i = l * q_prev + v_p;\n"
    "    p += 0.5f * stroke_width * q_i;\n"
    "\n"
    "    p_inv = float2x2(p1.y, -p1.x, p2.y - p1.y, p1.x - p2.x) / (p1.x * p2.y - p2.x * p1.y);\n"
    "    o.b.xy = mul(p_inv, p) + float2(1.0f, 0.0f);\n"
    "    o.b.zw = 0.0f;\n"
    "\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry) + 0.5f * stroke_width * q_i;\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
static const char shape_vs_code_triangle[] =
    "float3x2 transform_geometry;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "void main(float2 position : POSITION, out struct output o)\n"
    "{\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry);\n"
    "    o.b = float4(1.0, 0.0, 1.0, 1.0);\n"
    "    o.stroke_transform = float2x2(1.0, 0.0, 0.0, 1.0);\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
static const char shape_vs_code_triangle_aa[] =
    "float3x2 transform_geometry;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "void main(float2 position : POSITION, float coverage : COVERAGE, out struct output o)\n"
    "{\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry);\n"
    "    o.b = float4(1.0, 0.0, 1.0, coverage);\n"
    "    o.stroke_transform = float2x2(1.0, 0.0, 0.0, 1.0);\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
static const char shape_vs_code_curve[] =
    "float3x2 transform_geometry;\n"
    "float4 transform_rtx;\n"
    "float4 transform_rty;\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "    float4 position : SV_POSITION;\n"
    "};\n"
    "\n"
    "void main(float2 position : POSITION, float3 texcoord : TEXCOORD0, out struct output o)\n"
    "{\n"
    "    o.p = mul(float3(position, 1.0f), transform_geometry);\n"
    "    o.b = float4(texcoord, 1.0);\n"
    "    o.stroke_transform = float2x2(1.0, 0.0, 0.0, 1.0);\n"
    "    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))\n"
    "            * float2(transform_rtx.w, transform_rty.w);\n"
    "    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "}\n";
static const char shape_ps_code[] =
    "#define BRUSH_TYPE_SOLID    0\n"
    "#define BRUSH_TYPE_LINEAR   1\n"
    "#define BRUSH_TYPE_RADIAL   2\n"
    "#define BRUSH_TYPE_BITMAP   3\n"
    "#define BRUSH_TYPE_COUNT    4\n"
    "\n"
    "bool outline;\n"
    "bool is_arc;\n"
    "bool mask;\n"
    "bool mask_pad;\n"
    "int2 mask_origin;\n"
    "float4 clip_rect;\n"
    "bool clip_on;\n"
    "bool blend_min;\n"
    "struct brush\n"
    "{\n"
    "    uint type;\n"
    "    float opacity;\n"
    "    float4 data[3];\n"
    "} colour_brush, opacity_brush;\n"
    "\n"
    "SamplerState s0, s1;\n"
    "Texture2D t0, t1;\n"
    "Texture2D t4 : register(t4);\n"
    "Buffer<float4> b0, b1;\n"
    "\n"
    "struct input\n"
    "{\n"
    "    float2 p : WORLD_POSITION;\n"
    "    float4 b : BEZIER;\n"
    "    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;\n"
    "};\n"
    "\n"
    "float3 srgb_to_linear(float3 c)\n"
    "{\n"
    "    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
    "}\n"
    "\n"
    "float3 linear_to_srgb(float3 c)\n"
    "{\n"
    "    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;\n"
    "}\n"
    "\n"
    "float extend_position(float position, uint extend_mode)\n"
    "{\n"
    "    if (extend_mode == 1)\n"
    "        return position - floor(position);\n"
    "    if (extend_mode == 2)\n"
    "    {\n"
    "        position = position - 2.0 * floor(position * 0.5);\n"
    "        return position > 1.0 ? 2.0 - position : position;\n"
    "    }\n"
    "    return position;\n"
    "}\n"
    "\n"
    "float4 sample_gradient(Buffer<float4> gradient, uint stop_count, uint extend_mode, uint gamma, float position)\n"
    "{\n"
    "    float4 c_low, c_high;\n"
    "    float p_low, p_high;\n"
    "    uint i;\n"
    "\n"
    "    position = extend_position(position, extend_mode);\n"
    "\n"
    "    p_low = gradient.Load(0).x;\n"
    "    c_low = gradient.Load(1);\n"
    "    c_high = c_low;\n"
    "    if (gamma)\n"
    "        c_low.rgb = srgb_to_linear(c_low.rgb);\n"
    "    c_high = c_low;\n"
    "\n"
    "    if (position >= p_low)\n"
    "    {\n"
    "        [loop]\n"
    "        for (i = 1; i < stop_count; ++i)\n"
    "        {\n"
    "            p_high = gradient.Load(i * 2).x;\n"
    "            c_high = gradient.Load(i * 2 + 1);\n"
    "            if (gamma)\n"
    "                c_high.rgb = srgb_to_linear(c_high.rgb);\n"
    "\n"
    "            if (position >= p_low && position <= p_high)\n"
    "            {\n"
    "                c_low = lerp(c_low, c_high, (position - p_low) / (p_high - p_low));\n"
    "                break;\n"
    "            }\n"
    "\n"
    "            p_low = p_high;\n"
    "            c_low = c_high;\n"
    "        }\n"
    "        if (i == stop_count)\n"
    "            c_low = c_high;\n"
    "    }\n"
    "\n"
    "    if (gamma)\n"
    "        c_low.rgb = linear_to_srgb(c_low.rgb);\n"
    "    return c_low;\n"
    "}\n"
    "float4 brush_linear(struct brush brush, Buffer<float4> gradient, float2 position)\n"
    "{\n"
    "    float2 start, end, v_p, v_q;\n"
    "    uint stop_count;\n"
    "    uint extend_mode, gamma;\n"
    "    float p;\n"
    "\n"
    "    start = brush.data[0].xy;\n"
    "    end = brush.data[0].zw;\n"
    "    stop_count = asuint(brush.data[1].x);\n"
    "    extend_mode = asuint(brush.data[1].y);\n"
    "    gamma = asuint(brush.data[1].z);\n"
    "\n"
    "    v_p = position - start;\n"
    "    v_q = end - start;\n"
    "    p = dot(v_q, v_p) / dot(v_q, v_q);\n"
    "\n"
    "    return sample_gradient(gradient, stop_count, extend_mode, gamma, p);\n"
    "}\n"
    "\n"
    "float4 brush_radial(struct brush brush, Buffer<float4> gradient, float2 position)\n"
    "{\n"
    "    float2 centre, offset, ra, rb, v_p, v_q, r;\n"
    "    float b, c, l, t;\n"
    "    uint stop_count;\n"
    "    uint extend_mode, gamma;\n"
    "\n"
    "    centre = brush.data[0].xy;\n"
    "    offset = brush.data[0].zw;\n"
    "    ra = brush.data[1].xy;\n"
    "    rb = brush.data[1].zw;\n"
    "    stop_count = asuint(brush.data[2].x);\n"
    "    extend_mode = asuint(brush.data[2].y);\n"
    "    gamma = asuint(brush.data[2].z);\n"
    "\n"
    "    /* Project onto ra, rb. */\n"
    "    r = float2(dot(ra, ra), dot(rb, rb));\n"
    "    v_p = position - (centre + offset);\n"
    "    v_p = float2(dot(v_p, ra), dot(v_p, rb)) / r;\n"
    "    v_q = float2(dot(offset, ra), dot(offset, rb)) / r;\n"
    "\n"
    "    /* ‖t·p̂ + q⃑‖ = 1\n"
    "     * (t·p̂ + q⃑) · (t·p̂ + q⃑) = 1\n"
    "     * t² + 2·(p̂·q⃑)·t + (q⃑·q⃑) = 1\n"
    "     *\n"
    "     * b = p̂·q⃑\n"
    "     * c = q⃑·q⃑ - 1\n"
    "     * t = -b + √(b² - c) */\n"
    "    l = length(v_p);\n"
    "    b = dot(v_p, v_q) / l;\n"
    "    c = dot(v_q, v_q) - 1.0;\n"
    "    t = -b + sqrt(b * b - c);\n"
    "\n"
    "    return sample_gradient(gradient, stop_count, extend_mode, gamma, l / t);\n"
    "}\n"
    "\n"
    "float4 brush_bitmap(struct brush brush, Texture2D t, SamplerState s, float2 position)\n"
    "{\n"
    "    float3 transform[2];\n"
    "    bool ignore_alpha;\n"
    "    float2 texcoord;\n"
    "    float4 colour;\n"
    "\n"
    "    transform[0] = brush.data[0].xyz;\n"
    "    transform[1] = brush.data[1].xyz;\n"
    "    ignore_alpha = asuint(brush.data[1].w);\n"
    "\n"
    "    texcoord.x = dot(position.xy, transform[0].xy) + transform[0].z;\n"
    "    texcoord.y = dot(position.xy, transform[1].xy) + transform[1].z;\n"
    "    colour = t.Sample(s, texcoord);\n"
    "    if (ignore_alpha)\n"
    "        colour.a = 1.0;\n"
    "    return colour;\n"
    "}\n"
    "\n"
    "float4 premultiply(float4 colour)\n"
    "{\n"
    "    return float4(colour.rgb * colour.a, colour.a);\n"
    "}\n"
    "\n"
    "float4 sample_brush(struct brush brush, Texture2D t, SamplerState s, Buffer<float4> b, float2 position)\n"
    "{\n"
    "    if (brush.type == BRUSH_TYPE_SOLID)\n"
    "        return brush.data[0] * brush.opacity;\n"
    "    if (brush.type == BRUSH_TYPE_LINEAR)\n"
    "        return premultiply(brush_linear(brush, b, position)) * brush.opacity;\n"
    "    if (brush.type == BRUSH_TYPE_RADIAL)\n"
    "        return premultiply(brush_radial(brush, b, position)) * brush.opacity;\n"
    "    if (brush.type == BRUSH_TYPE_BITMAP)\n"
    "        return brush_bitmap(brush, t, s, position) * brush.opacity;\n"
    "    return float4(0.0, 0.0, 0.0, brush.opacity);\n"
    "}\n"
    "\n"
    "struct output\n"
    "{\n"
    "    float4 colour : SV_Target0;\n"
    "    float4 coverage : SV_Target1;\n"
    "};\n"
    "\n"
    "struct output main(struct input i, float4 pos : SV_POSITION)\n"
    "{\n"
    "    struct output o;\n"
    "    float4 colour;\n"
    "    float coverage = 1.0;\n"
    "\n"
    "    colour = sample_brush(colour_brush, t0, s0, b0, i.p);\n"
    "    if (opacity_brush.type < BRUSH_TYPE_COUNT)\n"
    "        colour *= sample_brush(opacity_brush, t1, s1, b1, i.p).a;\n"
    "    if (mask)\n"
    "        coverage *= t4.Load(int3(int2(pos.xy) - mask_origin, 0)).x;\n"
    "    if (clip_on)\n"
    "    {\n"
    "        float2 q = pos.xy - 0.5;\n"
    "        coverage *= saturate(min(q.x + 1.0, clip_rect.z) - max(q.x, clip_rect.x))\n"
    "                * saturate(min(q.y + 1.0, clip_rect.w) - max(q.y, clip_rect.y));\n"
    "    }\n"
    "\n"
    "    if (outline)\n"
    "    {\n"
    "        float2 du, dv, df;\n"
    "        float4 uv;\n"
    "\n"
    "        /* Evaluate the implicit form of the curve (u² - v = 0\n"
    "         * for Béziers, u² + v² - 1 = 0 for arcs) in texture\n"
    "         * space, using the screen-space partial derivatives\n"
    "         * to convert the calculated distance to object space.\n"
    "         *\n"
    "         * d(x, y) = |f(x, y)| / ‖∇f(x, y)‖\n"
    "         *         = |f(x, y)| / √((∂f/∂x)² + (∂f/∂y)²)\n"
    "         *\n"
    "         * For Béziers:\n"
    "         * f(x, y) = u(x, y)² - v(x, y)\n"
    "         * ∂f/∂x = 2u · ∂u/∂x - ∂v/∂x\n"
    "         * ∂f/∂y = 2u · ∂u/∂y - ∂v/∂y\n"
    "         *\n"
    "         * For arcs:\n"
    "         * f(x, y) = u(x, y)² + v(x, y)² - 1\n"
    "         * ∂f/∂x = 2u · ∂u/∂x + 2v · ∂v/∂x\n"
    "         * ∂f/∂y = 2u · ∂u/∂y + 2v · ∂v/∂y */\n"
    "        uv = i.b;\n"
    "        du = float2(ddx(uv.x), ddy(uv.x));\n"
    "        dv = float2(ddx(uv.y), ddy(uv.y));\n"
    "\n"
    "        if (!is_arc)\n"
    "        {\n"
    "            df = 2.0f * uv.x * du - dv;\n"
    "\n"
    "            clip(dot(df, uv.zw));\n"
    "            clip(length(mul(i.stroke_transform, df)) - abs(uv.x * uv.x - uv.y));\n"
    "        }\n"
    "        else\n"
    "        {\n"
    "            df = 2.0f * uv.x * du + 2.0f * uv.y * dv;\n"
    "\n"
    "            clip(dot(df, uv.zw));\n"
    "            clip(length(mul(i.stroke_transform, df)) - abs(uv.x * uv.x + uv.y * uv.y - 1.0f));\n"
    "        }\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        coverage *= i.b.w;\n"
    "        /* Evaluate the implicit form of the curve in texture space.\n"
    "         * \"i.b.z\" determines which side of the curve is shaded. */\n"
    "        if (!is_arc)\n"
    "        {\n"
    "            clip((i.b.x * i.b.x - i.b.y) * i.b.z);\n"
    "        }\n"
    "        else\n"
    "        {\n"
    "            clip((i.b.x * i.b.x + i.b.y * i.b.y - 1.0) * i.b.z);\n"
    "        }\n"
    "    }\n"
    "\n"
    "    o.colour = colour * coverage;\n"
    "    if (blend_min)\n"
    "        o.colour += 1.0 - o.colour.a;\n"
    "    o.coverage = coverage;\n"
    "    return o;\n"
    "}\n";
static const struct shape_info
{
    enum d2d_shape_type shape_type;
    const D3D11_INPUT_ELEMENT_DESC *il_desc;
    unsigned int il_element_count;
    const char *name;
    const char *vs_code;
    size_t vs_code_size;
}
shape_info[] =
{
    {D2D_SHAPE_TYPE_OUTLINE,        shape_il_desc_outline,        ARRAY_SIZE(shape_il_desc_outline),
     "outline",                     shape_vs_code_outline,        sizeof(shape_vs_code_outline) - 1},
    {D2D_SHAPE_TYPE_BEZIER_OUTLINE, shape_il_desc_curve_outline,  ARRAY_SIZE(shape_il_desc_curve_outline),
     "bezier_outline",              shape_vs_code_bezier_outline, sizeof(shape_vs_code_bezier_outline) - 1},
    {D2D_SHAPE_TYPE_ARC_OUTLINE,    shape_il_desc_curve_outline,  ARRAY_SIZE(shape_il_desc_curve_outline),
     "arc_outline",                 shape_vs_code_arc_outline,    sizeof(shape_vs_code_arc_outline) - 1},
    {D2D_SHAPE_TYPE_TRIANGLE,       shape_il_desc_triangle,       ARRAY_SIZE(shape_il_desc_triangle),
     "triangle",                    shape_vs_code_triangle,       sizeof(shape_vs_code_triangle) - 1},
    {D2D_SHAPE_TYPE_CURVE,          shape_il_desc_curve,          ARRAY_SIZE(shape_il_desc_curve),
     "curve",                       shape_vs_code_curve,          sizeof(shape_vs_code_curve) - 1},
    {D2D_SHAPE_TYPE_TRIANGLE_AA,    shape_il_desc_triangle_aa,    ARRAY_SIZE(shape_il_desc_triangle_aa),
     "triangle_aa",                 shape_vs_code_triangle_aa,    sizeof(shape_vs_code_triangle_aa) - 1},
};

HRESULT d2d_device_context_init(struct d2d_device_context *render_target,
        struct d2d_device *device, IUnknown *outer_unknown, const struct d2d_device_context_ops *ops)
{
    D3D11_SUBRESOURCE_DATA buffer_data;
    IDWriteFactory *dwrite_factory;
    D3D11_RASTERIZER_DESC rs_desc;
    D3D11_BUFFER_DESC buffer_desc;
    struct d2d_factory *factory;
    ID3D10Blob *precompiled;
    struct d2d_shader_set *shader_set;
    unsigned int i;
    HRESULT hr;

    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f,  1.0f},
        {-1.0f, -1.0f},
        { 1.0f,  1.0f},
        { 1.0f, -1.0f},
    };
    static const UINT16 indices[] = {0, 1, 2, 2, 1, 3};
    static const D3D_FEATURE_LEVEL feature_levels = D3D_FEATURE_LEVEL_10_0;

    render_target->ID2D1DeviceContext6_iface.lpVtbl = &d2d_device_context_vtbl;
    render_target->ID2D1GdiInteropRenderTarget_iface.lpVtbl = &d2d_gdi_interop_render_target_vtbl;
    render_target->IDWriteTextRenderer_iface.lpVtbl = &d2d_text_renderer_vtbl;
    render_target->IUnknown_iface.lpVtbl = &d2d_device_context_inner_unknown_vtbl;
    render_target->refcount = 1;
    ID2D1Device1_GetFactory((ID2D1Device1 *)&device->ID2D1Device6_iface, &render_target->factory);
    render_target->device = device;
    ID2D1Device6_AddRef(&render_target->device->ID2D1Device6_iface);

    factory = unsafe_impl_from_ID2D1Factory(render_target->factory);
    if (factory->factory_type == D2D1_FACTORY_TYPE_MULTI_THREADED)
        render_target->cs = &factory->cs;

    render_target->outer_unknown = outer_unknown ? outer_unknown : &render_target->IUnknown_iface;
    render_target->ops = ops;

    if (FAILED(hr = IDXGIDevice_QueryInterface(device->dxgi_device,
            &IID_ID3D11Device1, (void **)&render_target->d3d_device)))
    {
        WARN("Failed to query ID3D11Device1 interface, hr %#lx.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D11Device1_CreateDeviceContextState(render_target->d3d_device,
            0, &feature_levels, 1, D3D11_SDK_VERSION, &IID_ID3D11Device1, NULL,
            &render_target->d3d_state)))
    {
        WARN("Failed to create device context state, hr %#lx.\n", hr);
        goto err;
    }

    shader_set = device->shader_set;
    EnterCriticalSection(&shader_set->cs);
    for (i = 0; i < ARRAY_SIZE(shape_info); ++i)
    {
        const struct shape_info *si = &shape_info[i];

        if (!shader_set->vs[i])
        {
            assert(shader_set->precompiled_shape_vs[i]);
            precompiled = shader_set->precompiled_shape_vs[i];
            if (FAILED(hr = ID3D11Device1_CreateInputLayout(render_target->d3d_device, si->il_desc,
                    si->il_element_count, ID3D10Blob_GetBufferPointer(precompiled),
                    ID3D10Blob_GetBufferSize(precompiled), &shader_set->il[i])))
            {
                WARN("Failed to create input layout for shape type %#x, hr %#lx.\n", si->shape_type, hr);
                LeaveCriticalSection(&shader_set->cs);
                goto err;
            }

            if (FAILED(hr = ID3D11Device1_CreateVertexShader(render_target->d3d_device,
                    ID3D10Blob_GetBufferPointer(precompiled), ID3D10Blob_GetBufferSize(precompiled),
                    NULL, &shader_set->vs[i])))
            {
                WARN("Failed to create vertex shader for shape type %#x, hr %#lx.\n", si->shape_type, hr);
                ID3D11InputLayout_Release(shader_set->il[i]);
                shader_set->il[i] = NULL;
                LeaveCriticalSection(&shader_set->cs);
                goto err;
            }
        }

        ID3D11InputLayout_AddRef(render_target->shape_resources[si->shape_type].il = shader_set->il[i]);
        ID3D11VertexShader_AddRef(render_target->shape_resources[si->shape_type].vs = shader_set->vs[i]);
    }

    if (!shader_set->ps)
    {
        assert(shader_set->precompiled_shape_ps);
        precompiled = shader_set->precompiled_shape_ps;
        if (FAILED(hr = ID3D11Device1_CreatePixelShader(render_target->d3d_device,
                ID3D10Blob_GetBufferPointer(precompiled), ID3D10Blob_GetBufferSize(precompiled),
                NULL, &shader_set->ps)))
        {
            WARN("Failed to create pixel shader, hr %#lx.\n", hr);
            LeaveCriticalSection(&shader_set->cs);
            goto err;
        }
    }
    ID3D11PixelShader_AddRef(render_target->ps = shader_set->ps);
    LeaveCriticalSection(&shader_set->cs);

    buffer_desc.ByteWidth = sizeof(struct d2d_vs_cb);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, NULL,
            &render_target->vs_cb)))
    {
        WARN("Failed to create constant buffer, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(struct d2d_ps_cb);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, NULL,
            &render_target->ps_cb)))
    {
        WARN("Failed to create constant buffer, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(indices);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = indices;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device,
            &buffer_desc, &buffer_data, &render_target->ib)))
    {
        WARN("Failed to create clear index buffer, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_data.pSysMem = quad;

    render_target->vb_stride = sizeof(*quad);
    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device,
            &buffer_desc, &buffer_data, &render_target->vb)))
    {
        WARN("Failed to create clear vertex buffer, hr %#lx.\n", hr);
        goto err;
    }

    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.FrontCounterClockwise = FALSE;
    rs_desc.DepthBias = 0;
    rs_desc.DepthBiasClamp = 0.0f;
    rs_desc.SlopeScaledDepthBias = 0.0f;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.ScissorEnable = TRUE;
    rs_desc.MultisampleEnable = FALSE;
    rs_desc.AntialiasedLineEnable = FALSE;
    if (FAILED(hr = ID3D11Device1_CreateRasterizerState(render_target->d3d_device, &rs_desc, &render_target->rs)))
    {
        WARN("Failed to create clear rasteriser state, hr %#lx.\n", hr);
        goto err;
    }

    if (FAILED(hr = d2d_device_context_create_blend_states(render_target)))
        goto err;

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        goto err;
    }

    hr = IDWriteFactory_CreateRenderingParams(dwrite_factory, &render_target->default_text_rendering_params);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create default text rendering parameters, hr %#lx.\n", hr);
        goto err;
    }

    render_target->drawing_state.transform = identity;

    if (!d2d_clip_stack_init(&render_target->clip_stack))
    {
        WARN("Failed to initialize clip stack.\n");
        hr = E_FAIL;
        goto err;
    }

    render_target->desc.dpiX = 96.0f;
    render_target->desc.dpiY = 96.0f;

    return S_OK;

err:
    d2d_device_context_release_blend_states(render_target);
    if (render_target->default_text_rendering_params)
        IDWriteRenderingParams_Release(render_target->default_text_rendering_params);
    if (render_target->rs)
        ID3D11RasterizerState_Release(render_target->rs);
    if (render_target->vb)
        ID3D11Buffer_Release(render_target->vb);
    if (render_target->ib)
        ID3D11Buffer_Release(render_target->ib);
    if (render_target->ps_cb)
        ID3D11Buffer_Release(render_target->ps_cb);
    if (render_target->ps)
        ID3D11PixelShader_Release(render_target->ps);
    if (render_target->vs_cb)
        ID3D11Buffer_Release(render_target->vs_cb);
    for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
    {
        if (render_target->shape_resources[i].vs)
            ID3D11VertexShader_Release(render_target->shape_resources[i].vs);
        if (render_target->shape_resources[i].il)
            ID3D11InputLayout_Release(render_target->shape_resources[i].il);
    }
    if (render_target->d3d_state)
        ID3DDeviceContextState_Release(render_target->d3d_state);
    if (render_target->d3d_device)
        ID3D11Device1_Release(render_target->d3d_device);
    ID2D1Device6_Release(&render_target->device->ID2D1Device6_iface);
    ID2D1Factory_Release(render_target->factory);
    return hr;
}

HRESULT d2d_shader_set_create(struct d2d_shader_set **out)
{
    struct d2d_shader_set *set;
    ID3D10Blob *compiled;
    HRESULT hr;

    if (!(set = calloc(1, sizeof(*set))))
        return E_OUTOFMEMORY;

    set->refcount = 1;
    InitializeCriticalSection(&set->cs);

    for (unsigned int i = 0; i < ARRAY_SIZE(shape_info); ++i)
    {
        const struct shape_info *si = &shape_info[i];

        if (FAILED(hr = D3DCompile(si->vs_code, si->vs_code_size, si->name, NULL, NULL,
                "main", "vs_4_0", 0, 0, &compiled, NULL)))
        {
            WARN("Failed to compile shader for shape type %#x, hr %#lx.\n", si->shape_type, hr);
            d2d_shader_set_release(set);
            return hr;
        }
        set->precompiled_shape_vs[i] = compiled;
    }

    if (FAILED(hr = D3DCompile(shape_ps_code, sizeof(shape_ps_code) - 1, "ps", NULL, NULL,
            "main", "ps_4_0", 0, 0, &compiled, NULL)))
    {
        WARN("Failed to compile the pixel shader, hr %#lx.\n", hr);
        d2d_shader_set_release(set);
        return hr;
    }
    set->precompiled_shape_ps = compiled;

    *out = set;
    return S_OK;
}

void d2d_shader_set_release(struct d2d_shader_set *set)
{
    if (InterlockedDecrement(&set->refcount))
        return;

    for (unsigned int i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
    {
        if (set->vs[i])
            ID3D11VertexShader_Release(set->vs[i]);
        if (set->il[i])
            ID3D11InputLayout_Release(set->il[i]);
        if (set->precompiled_shape_vs[i])
            ID3D10Blob_Release(set->precompiled_shape_vs[i]);
    }
    if (set->ps)
        ID3D11PixelShader_Release(set->ps);
    if (set->precompiled_shape_ps)
        ID3D10Blob_Release(set->precompiled_shape_ps);
    DeleteCriticalSection(&set->cs);
    free(set);
}
