/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/secur32/lsa.c
 * PURPOSE:         Client-side LSA functions
 * UPDATE HISTORY:
 *                  Created 05/08/00
 */

#include "precomp.h"

VOID LsapInitLsaPort(VOID);
VOID LsapCloseLsaPort(VOID);

/* GLOBALS *******************************************************************/

HANDLE Secur32Heap;

/* FUNCTIONS *****************************************************************/

#ifdef __REACTOS__
// See sspi.c
extern void SECUR32_freeProviders(void);
#endif

BOOL
WINAPI
DllMain(HINSTANCE hInstance,
        ULONG Reason,
        PVOID Reserved)
{
    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            Secur32Heap = RtlCreateHeap(0, NULL, 0, 4096, NULL, NULL);
            if (Secur32Heap == 0)
            {
                return FALSE;
            }
            LsapInitLsaPort();
            break;

        case DLL_PROCESS_DETACH:
#ifdef __REACTOS__
            SECUR32_freeProviders();
#endif
            LsapCloseLsaPort();
            if (!RtlDestroyHeap(Secur32Heap))
            {
                return FALSE;
            }
            break;
    }

    return TRUE;
}

void *
SECUR32_identityView(void *auth_data, void *view)
{
    const SEC_WINNT_AUTH_IDENTITY_EXW *ex = auth_data;
    SEC_WINNT_AUTH_IDENTITY_W *w = view;

    if (!ex || ex->Version != SEC_WINNT_AUTH_IDENTITY_VERSION)
        return auth_data;

    w->User = ex->User;
    w->UserLength = ex->UserLength;
    w->Domain = ex->Domain;
    w->DomainLength = ex->DomainLength;
    w->Password = ex->Password;
    w->PasswordLength = ex->PasswordLength;
    w->Flags = ex->Flags;
    return w;
}
