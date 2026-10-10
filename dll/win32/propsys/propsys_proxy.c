/*
 * PROJECT:     LiberNT Property System
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Marshalling shims for the property system call_as methods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "propsys.h"

HRESULT CALLBACK IInitializeWithStream_Initialize_Proxy(IInitializeWithStream *This, IStream *pstream, DWORD grfMode)
{
    return IInitializeWithStream_RemoteInitialize_Proxy(This, pstream, grfMode);
}

HRESULT __RPC_STUB IInitializeWithStream_Initialize_Stub(IInitializeWithStream *This, IStream *pstream, DWORD grfMode)
{
    return IInitializeWithStream_Initialize(This, pstream, grfMode);
}

HRESULT CALLBACK IPropertyDescription_CoerceToCanonicalValue_Proxy(IPropertyDescription *This, PROPVARIANT *propvar)
{
    PROPVARIANT value;
    HRESULT hr;

    PropVariantInit(&value);
    hr = IPropertyDescription_RemoteCoerceToCanonicalValue_Proxy(This, propvar, &value);
    PropVariantClear(propvar);
    *propvar = value;
    return hr;
}

HRESULT __RPC_STUB IPropertyDescription_CoerceToCanonicalValue_Stub(IPropertyDescription *This, REFPROPVARIANT propvar,
                                                                   PROPVARIANT *ppropvar)
{
    HRESULT hr;

    hr = PropVariantCopy(ppropvar, propvar);
    if (FAILED(hr))
        return hr;
    return IPropertyDescription_CoerceToCanonicalValue(This, ppropvar);
}
