
#ifndef __WINE_NTLM_H__
#define __WINE_NTLM_H__

#include <sys/types.h>
#define CP_UNIXCP CP_ACP

void SECUR32_initNTLMSP(void) DECLSPEC_HIDDEN;

typedef enum _helper_mode {
    NTLM_SERVER,
    NTLM_CLIENT,
    NUM_HELPER_MODES
} HelperMode;

typedef struct tag_arc4_info {
    unsigned char x, y;
    unsigned char state[256];
} arc4_info;

typedef struct _NegoHelper {
    HelperMode mode;
    ULONG want_flags;
    WCHAR *user;
    WCHAR *domain;
    WCHAR *password;
    ULONG user_len;
    ULONG domain_len;
    ULONG password_len;
    BOOL have_credentials;
    BYTE *negotiate_msg;
    ULONG negotiate_len;
    BYTE *challenge_msg;
    ULONG challenge_len;
    BYTE server_challenge[8];
    HANDLE token;
    BYTE *session_key;
    ULONG neg_flags;
    struct {
        struct {
            ULONG seq_num;
            arc4_info *a4i;
        } ntlm;
        struct {
            BYTE *send_sign_key;
            BYTE *send_seal_key;
            BYTE *recv_sign_key;
            BYTE *recv_seal_key;
            ULONG send_seq_no;
            ULONG recv_seq_no;
            arc4_info *send_a4i;
            arc4_info *recv_a4i;
        } ntlm2;
    } crypt;
} NegoHelper, *PNegoHelper;

typedef struct _NtlmCredentials
{
    ULONG use;
    WCHAR *user;
    WCHAR *domain;
    WCHAR *password;
    ULONG user_len;
    ULONG domain_len;
    ULONG password_len;
    BOOL have_credentials;
    BOOL default_credentials;
} NtlmCredentials, *PNtlmCredentials;

typedef enum _sign_direction {
    NTLM_SEND,
    NTLM_RECV
} SignDirection;

SECURITY_STATUS SECUR32_CreateNTLM1SessionKey(PBYTE password, int len, PBYTE session_key) DECLSPEC_HIDDEN;
SECURITY_STATUS SECUR32_CreateNTLM2SubKeys(PNegoHelper helper) DECLSPEC_HIDDEN;

/* NTLMSSP flags indicating the negotiated features */
#define NTLMSSP_NEGOTIATE_UNICODE                   0x00000001
#define NTLMSSP_NEGOTIATE_OEM                       0x00000002
#define NTLMSSP_REQUEST_TARGET                      0x00000004
#define NTLMSSP_NEGOTIATE_SIGN                      0x00000010
#define NTLMSSP_NEGOTIATE_SEAL                      0x00000020
#define NTLMSSP_NEGOTIATE_DATAGRAM_STYLE            0x00000040
#define NTLMSSP_NEGOTIATE_LM_SESSION_KEY            0x00000080
#define NTLMSSP_NEGOTIATE_NTLM                      0x00000200
#define NTLMSSP_NEGOTIATE_ANONYMOUS                 0x00000800
#define NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED           0x00001000
#define NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED      0x00002000
#define NTLMSSP_NEGOTIATE_LOCAL_CALL                0x00004000
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN               0x00008000
#define NTLMSSP_NEGOTIATE_TARGET_TYPE_DOMAIN        0x00010000
#define NTLMSSP_NEGOTIATE_TARGET_TYPE_SERVER        0x00020000
#define NTLMSSP_NEGOTIATE_NTLM2                     0x00080000
#define NTLMSSP_NEGOTIATE_TARGET_INFO               0x00800000
#define NTLMSSP_NEGOTIATE_VERSION                   0x02000000
#define NTLMSSP_NEGOTIATE_128                       0x20000000
#define NTLMSSP_NEGOTIATE_KEY_EXCHANGE              0x40000000
#define NTLMSSP_NEGOTIATE_56                        0x80000000

SECURITY_STATUS SEC_ENTRY ntlm_AcquireCredentialsHandleW(SEC_WCHAR *, SEC_WCHAR *,
    ULONG, PLUID, PVOID, SEC_GET_KEY_FN, PVOID, PCredHandle, PTimeStamp) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_InitializeSecurityContextW(PCredHandle, PCtxtHandle,
    SEC_WCHAR *, ULONG fContextReq, ULONG, ULONG, PSecBufferDesc, ULONG, PCtxtHandle,
    PSecBufferDesc, ULONG *, PTimeStamp) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_AcceptSecurityContext(PCredHandle, PCtxtHandle, PSecBufferDesc,
    ULONG, ULONG, PCtxtHandle, PSecBufferDesc, ULONG *, PTimeStamp) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_QueryContextAttributesA(PCtxtHandle, ULONG, void *) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_QueryContextAttributesW(PCtxtHandle, ULONG, void *) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_EncryptMessage(PCtxtHandle, ULONG, PSecBufferDesc, ULONG) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_DecryptMessage(PCtxtHandle, PSecBufferDesc, ULONG, PULONG) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_FreeCredentialsHandle(PCredHandle) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_DeleteSecurityContext(PCtxtHandle) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_MakeSignature(PCtxtHandle, ULONG, PSecBufferDesc, ULONG) DECLSPEC_HIDDEN;
SECURITY_STATUS SEC_ENTRY ntlm_VerifySignature(PCtxtHandle, PSecBufferDesc, ULONG, PULONG) DECLSPEC_HIDDEN;

SECURITY_STATUS NtlmBuildNegotiate(PNegoHelper context, BYTE *buffer, ULONG size, ULONG *length) DECLSPEC_HIDDEN;
SECURITY_STATUS NtlmBuildChallenge(PNegoHelper context, const BYTE *negotiate, ULONG negotiate_len,
    BYTE *buffer, ULONG size, ULONG *length) DECLSPEC_HIDDEN;
SECURITY_STATUS NtlmBuildAuthenticate(PNegoHelper context, const BYTE *challenge, ULONG challenge_len,
    BYTE *buffer, ULONG size, ULONG *length) DECLSPEC_HIDDEN;
SECURITY_STATUS NtlmAcceptAuthenticate(PNegoHelper context, const BYTE *authenticate, ULONG authenticate_len) DECLSPEC_HIDDEN;
void NtlmFreeContext(PNegoHelper context) DECLSPEC_HIDDEN;

extern SecPkgInfoW *ntlm_package_infoW DECLSPEC_HIDDEN;
extern SecPkgInfoA *ntlm_package_infoA DECLSPEC_HIDDEN;

#endif /* __WINE_NTLM_H__ */
