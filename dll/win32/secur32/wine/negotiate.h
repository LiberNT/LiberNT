
#ifndef __WINE_NEGOTIATE_H__
#define __WINE_NEGOTIATE_H__

void SECUR32_initNegotiateSP(void) DECLSPEC_HIDDEN;

SECURITY_STATUS NegoAcceptSecurityContext(PCredHandle, PCtxtHandle, PSecBufferDesc, ULONG, ULONG,
    PCtxtHandle, PSecBufferDesc, ULONG *, PTimeStamp) DECLSPEC_HIDDEN;
SECURITY_STATUS NegoInitializeSecurityContextW(PCredHandle, PCtxtHandle, SEC_WCHAR *, ULONG, ULONG, ULONG,
    PSecBufferDesc, ULONG, PCtxtHandle, PSecBufferDesc, ULONG *, PTimeStamp) DECLSPEC_HIDDEN;
SECURITY_STATUS NegoDeleteSecurityContext(PCtxtHandle) DECLSPEC_HIDDEN;
PCtxtHandle NegoNtlmContext(PCtxtHandle) DECLSPEC_HIDDEN;

#endif /* __WINE_NEGOTIATE_H__ */
