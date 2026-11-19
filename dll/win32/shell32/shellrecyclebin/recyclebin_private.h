#ifndef _RECYCLEBIN_PRIVATE_H_
#define _RECYCLEBIN_PRIVATE_H_

#include <stdio.h>

#define COBJMACROS

#include <shlobj.h>

#include "recyclebin.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(recyclebin);

#ifdef __cplusplus
static inline HRESULT HResultFromWin32(DWORD hr)
{
     // HRESULT_FROM_WIN32 will evaluate its parameter twice, this function will not.
    return HRESULT_FROM_WIN32(hr);
}
#endif

/* Defines */

#define ROUND_UP(N, S) ((( (N) + (S)  - 1) / (S) ) * (S) )

/* Prototypes */

/* recyclebin_generic.c */

EXTERN_C
HRESULT RecycleBinGeneric_Constructor(OUT IUnknown **ppUnknown);

EXTERN_C
BOOL RecycleBinGeneric_IsEqualFileIdentity(const RECYCLEBINFILEIDENTITY *p1, const RECYCLEBINFILEIDENTITY *p2);

/* recyclebin_generic_enumerator.c */

EXTERN_C
HRESULT RecycleBinGenericEnum_Constructor(OUT IRecycleBinEnumList **pprbel);

EXTERN_C
HRESULT RecycleBinVolume_Constructor(_In_ LPCWSTR VolumePath, _Out_ IUnknown **ppUnknown);

#endif /* _RECYCLEBIN_PRIVATE_H_ */
