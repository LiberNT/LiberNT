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

#ifndef LIBERNT_D2D1_FACTORY_PRIVATE_H
#define LIBERNT_D2D1_FACTORY_PRIVATE_H

#include "d2d1_private.h"

static inline struct d2d_factory *impl_from_ID2D1Factory7(ID2D1Factory7 *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_factory, ID2D1Factory7_iface);
}

void d2d_effect_registration_cleanup(struct d2d_effect_registration *reg);

HRESULT STDMETHODCALLTYPE d2d_factory_RegisterEffectFromStream(ID2D1Factory7 *iface,
        REFCLSID effect_id, IStream *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory);
HRESULT STDMETHODCALLTYPE d2d_factory_RegisterEffectFromString(ID2D1Factory7 *iface,
        REFCLSID effect_id, const WCHAR *property_xml, const D2D1_PROPERTY_BINDING *bindings,
        UINT32 binding_count, PD2D1_EFFECT_FACTORY effect_factory);
HRESULT STDMETHODCALLTYPE d2d_factory_UnregisterEffect(ID2D1Factory7 *iface, REFCLSID effect_id);
HRESULT STDMETHODCALLTYPE d2d_factory_GetRegisteredEffects(ID2D1Factory7 *iface,
        CLSID *effects, UINT32 effect_count, UINT32 *returned, UINT32 *registered);
HRESULT STDMETHODCALLTYPE d2d_factory_GetEffectProperties(ID2D1Factory7 *iface,
        REFCLSID effect_id, ID2D1Properties **props);

#endif
