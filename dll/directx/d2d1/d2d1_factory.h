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

#ifndef LIBERNT_D2D1_FACTORY_H
#define LIBERNT_D2D1_FACTORY_H

#include "d2d1_common.h"

struct d2d_factory
{
    ID2D1Factory7 ID2D1Factory7_iface;
    ID2D1Multithread ID2D1Multithread_iface;
    LONG refcount;

    ID3D10Device1 *device;
    struct d2d_shader_set *shared_shader_set;

    float dpi_x;
    float dpi_y;

    struct list effects;
    INIT_ONCE init_builtins;

    CRITICAL_SECTION cs;
    D2D1_FACTORY_TYPE factory_type;
};

static inline struct d2d_factory *unsafe_impl_from_ID2D1Factory(ID2D1Factory *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_factory, ID2D1Factory7_iface);
}

void d2d_effects_init_builtins(struct d2d_factory *factory);
HRESULT d2d_factory_create_device(ID2D1Factory1 *factory, IDXGIDevice *dxgi_device,
        bool allow_get_dxgi_device, REFIID iid, void **device);
struct d2d_effect_registration * d2d_factory_get_registered_effect(ID2D1Factory *factory,
        const GUID *effect_id);
void d2d_factory_register_effect(struct d2d_factory *factory,
        struct d2d_effect_registration *effect);
HRESULT d2d_effect_property_get_uint32_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, UINT32 *value);

#endif
