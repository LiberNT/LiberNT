/*
 * Copyright 2019 Philip Rebohle
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

#ifndef __VKD3D_SHADERS_H
#define __VKD3D_SHADERS_H

static const char cs_uav_clear_buffer_float_code[] =
    "RWBuffer<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(128, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[u_info.dst_offset.x + thread_id.x] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_buffer_uint_code[] =
    "RWBuffer<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(128, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[u_info.dst_offset.x + thread_id.x] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_1d_array_float_code[] =
    "RWTexture1DArray<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[int2(u_info.dst_offset.x + thread_id.x, thread_id.y)] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_1d_array_uint_code[] =
    "RWTexture1DArray<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[int2(u_info.dst_offset.x + thread_id.x, thread_id.y)] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_1d_float_code[] =
    "RWTexture1D<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[u_info.dst_offset.x + thread_id.x] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_1d_uint_code[] =
    "RWTexture1D<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (thread_id.x < u_info.dst_extent.x)\n"
    "        dst[u_info.dst_offset.x + thread_id.x] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_2d_array_float_code[] =
    "RWTexture2DArray<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[int3(u_info.dst_offset.xy + thread_id.xy, thread_id.z)] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_2d_array_uint_code[] =
    "RWTexture2DArray<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[int3(u_info.dst_offset.xy + thread_id.xy, thread_id.z)] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_2d_float_code[] =
    "RWTexture2D<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[u_info.dst_offset.xy + thread_id.xy] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_2d_uint_code[] =
    "RWTexture2D<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[u_info.dst_offset.xy + thread_id.xy] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_3d_float_code[] =
    "RWTexture3D<float4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    float4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[int3(u_info.dst_offset.xy, 0) + thread_id.xyz] = u_info.clear_value;\n"
    "}\n";

static const char cs_uav_clear_3d_uint_code[] =
    "RWTexture3D<uint4> dst;\n"
    "\n"
    "struct\n"
    "{\n"
    "    uint4 clear_value;\n"
    "    int2 dst_offset;\n"
    "    int2 dst_extent;\n"
    "} u_info;\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(int3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    if (all(thread_id.xy < u_info.dst_extent.xy))\n"
    "        dst[int3(u_info.dst_offset.xy, 0) + thread_id.xyz] = u_info.clear_value;\n"
    "}\n";

static const char cs_predicate_latch_code[] =
    "RWBuffer<uint> dst;\n"
    "\n"
    "[numthreads(1, 1, 1)]\n"
    "void main()\n"
    "{\n"
    "    dst[2] = dst[0] | dst[1];\n"
    "}\n";

static const char cs_predicate_copy_code[] =
    "RWBuffer<uint> buf;\n"
    "\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 thread_id : SV_DispatchThreadID)\n"
    "{\n"
    "    uint count = buf[0];\n"
    "    uint src = buf[1];\n"
    "    uint dst = buf[2];\n"
    "    uint stride = buf[3];\n"
    "    uint i, value;\n"
    "\n"
    "    for (i = thread_id.x; i < count; i += stride)\n"
    "    {\n"
    "        value = buf[src + i];\n"
    "        buf[dst + i] = value;\n"
    "    }\n"
    "}\n";

#endif /* __VKD3D_SHADERS_H */
