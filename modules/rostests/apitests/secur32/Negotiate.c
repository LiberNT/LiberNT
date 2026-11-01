/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the SPNEGO exchange of the Negotiate security package
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <rpc.h>
#define SECURITY_WIN32
#include <sspi.h>
#include <lm.h>
#include <stdio.h>

#define TEST_USER L"lntspnego"
#define TEST_PASSWORD L"Spn3go!Probe#2026"
#define MAX_TOKEN 12288

#define ISC_FLAGS (ISC_REQ_MUTUAL_AUTH | ISC_REQ_CONFIDENTIALITY)
#define ASC_FLAGS (ASC_REQ_MUTUAL_AUTH | ASC_REQ_CONFIDENTIALITY | ASC_REQ_CONNECTION | \
                   ASC_REQ_USE_SESSION_KEY | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_EXTENDED_ERROR)

static const BYTE OidSpnego[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02 };
static const BYTE OidKrb5[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x12, 0x01, 0x02, 0x02 };
static const BYTE OidMsKrb5[] = { 0x2a, 0x86, 0x48, 0x82, 0xf7, 0x12, 0x01, 0x02, 0x02 };
static const BYTE OidNtlm[] = { 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a };
static const BYTE OidNegoex[] = { 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x1e };

typedef struct _PEER
{
    CredHandle Cred;
    CtxtHandle Ctxt;
    BOOL HaveCtxt;
    PCWSTR Target;
    ULONG Attr;
    SECURITY_STATUS Status;
    BYTE Out[MAX_TOKEN];
    ULONG OutLen;
} PEER;

static BOOL
DerRead(const BYTE *Buf, ULONG Len, ULONG *Pos, BYTE *Tag, ULONG *ContentLen)
{
    ULONG p = *Pos, l, n;

    if (p + 2 > Len)
        return FALSE;
    *Tag = Buf[p++];
    l = Buf[p++];
    if (l & 0x80)
    {
        n = l & 0x7f;
        if (n == 0 || n > 3 || p + n > Len)
            return FALSE;
        for (l = 0; n; n--)
            l = (l << 8) | Buf[p++];
    }
    if (p + l > Len)
        return FALSE;
    *Pos = p;
    *ContentLen = l;
    return TRUE;
}

static const char *
OidName(const BYTE *Oid, ULONG Len)
{
    if (Len == sizeof(OidSpnego) && !memcmp(Oid, OidSpnego, Len)) return "SPNEGO";
    if (Len == sizeof(OidKrb5) && !memcmp(Oid, OidKrb5, Len)) return "KRB5";
    if (Len == sizeof(OidMsKrb5) && !memcmp(Oid, OidMsKrb5, Len)) return "MS-KRB5";
    if (Len == sizeof(OidNtlm) && !memcmp(Oid, OidNtlm, Len)) return "NTLM";
    if (Len == sizeof(OidNegoex) && !memcmp(Oid, OidNegoex, Len)) return "NEGOEX";
    return "OID?";
}

static void
Append(char *Out, size_t Size, const char *Fmt, ...)
{
    size_t Used = strlen(Out);
    va_list Args;

    va_start(Args, Fmt);
    _vsnprintf(Out + Used, Size - Used - 1, Fmt, Args);
    va_end(Args);
    Out[Size - 1] = 0;
}

static void
DescribeInner(const BYTE *Buf, ULONG Len, char *Out, size_t Size)
{
    if (Len >= 12 && !memcmp(Buf, "NTLMSSP", 8))
        Append(Out, Size, "NTLM%lu(%lu)", (ULONG)Buf[8], Len);
    else if (Len)
        Append(Out, Size, "raw%02x(%lu)", Buf[0], Len);
    else
        Append(Out, Size, "empty");
}

static void
DescribeFields(const BYTE *Buf, ULONG Len, char *Out, size_t Size)
{
    ULONG Pos = 0, Field, Pos2, Len2, Pos3, Len3;
    BYTE Tag, Tag2, Tag3;

    if (!DerRead(Buf, Len, &Pos, &Tag, &Field) || Tag != 0x30)
    {
        Append(Out, Size, " badseq");
        return;
    }
    Len = Pos + Field;
    while (Pos < Len)
    {
        if (!DerRead(Buf, Len, &Pos, &Tag, &Field))
        {
            Append(Out, Size, " badfield");
            return;
        }
        Pos2 = Pos;
        Append(Out, Size, " [%u]", Tag & 0x1f);
        if (DerRead(Buf, Pos + Field, &Pos2, &Tag2, &Len2))
        {
            if (Tag2 == 0x30)
            {
                Append(Out, Size, "{");
                Pos3 = Pos2;
                while (Pos3 < Pos2 + Len2 && DerRead(Buf, Pos2 + Len2, &Pos3, &Tag3, &Len3))
                {
                    if (Tag3 == 0x06)
                        Append(Out, Size, "%s,", OidName(Buf + Pos3, Len3));
                    else
                        Append(Out, Size, "t%02x,", Tag3);
                    Pos3 += Len3;
                }
                Append(Out, Size, "}");
            }
            else if (Tag2 == 0x06)
                Append(Out, Size, "%s", OidName(Buf + Pos2, Len2));
            else if (Tag2 == 0x0a && Len2 == 1)
                Append(Out, Size, "negState=%u", Buf[Pos2]);
            else if (Tag2 == 0x04)
            {
                if ((Tag & 0x1f) == 2)
                    DescribeInner(Buf + Pos2, Len2, Out, Size);
                else
                    Append(Out, Size, "octets(%lu)", Len2);
            }
            else if (Tag2 == 0x03)
                Append(Out, Size, "bits(%lu)", Len2);
            else
                Append(Out, Size, "t%02x(%lu)", Tag2, Len2);
        }
        Pos += Field;
    }
}

static void
Describe(const BYTE *Buf, ULONG Len, char *Out, size_t Size)
{
    ULONG Pos = 0, Field, Pos2, Len2;
    BYTE Tag, Tag2;

    Out[0] = 0;
    if (!Len)
    {
        Append(Out, Size, "none");
        return;
    }
    if (Len >= 12 && !memcmp(Buf, "NTLMSSP", 8))
    {
        DescribeInner(Buf, Len, Out, Size);
        return;
    }
    if (Buf[0] == 0x60 && DerRead(Buf, Len, &Pos, &Tag, &Field))
    {
        Pos2 = Pos;
        if (DerRead(Buf, Len, &Pos2, &Tag2, &Len2) && Tag2 == 0x06)
        {
            Append(Out, Size, "GSS(%s,%lu) ", OidName(Buf + Pos2, Len2), Len);
            Pos = Pos2 + Len2;
            if (Pos < Len && DerRead(Buf, Len, &Pos, &Tag, &Field))
            {
                Append(Out, Size, "init%u:", Tag & 0x1f);
                DescribeFields(Buf + Pos, Field, Out, Size);
            }
            return;
        }
    }
    if ((Buf[0] == 0xa1 || Buf[0] == 0xa0) && DerRead(Buf, Len, &Pos, &Tag, &Field))
    {
        Append(Out, Size, "%s(%lu):", Tag == 0xa1 ? "resp" : "init", Len);
        DescribeFields(Buf + Pos, Field, Out, Size);
        return;
    }
    Append(Out, Size, "raw%02x(%lu)", Buf[0], Len);
}

static ULONG
DerWrap(BYTE Tag, const BYTE *Content, ULONG Len, BYTE *Out)
{
    ULONG p = 0;

    Out[p++] = Tag;
    if (Len < 0x80)
        Out[p++] = (BYTE)Len;
    else if (Len < 0x100)
    {
        Out[p++] = 0x81;
        Out[p++] = (BYTE)Len;
    }
    else
    {
        Out[p++] = 0x82;
        Out[p++] = (BYTE)(Len >> 8);
        Out[p++] = (BYTE)Len;
    }
    memmove(Out + p, Content, Len);
    return p + Len;
}

static ULONG
BuildNegTokenInit(const BYTE *const *Oids, const ULONG *OidLens, ULONG Count,
                  const BYTE *MechToken, ULONG MechTokenLen, BYTE *Out)
{
    BYTE A[MAX_TOKEN], B[MAX_TOKEN];
    ULONG a = 0, b, i;

    for (i = 0; i < Count; i++)
        a += DerWrap(0x06, Oids[i], OidLens[i], A + a);
    b = DerWrap(0x30, A, a, B);
    a = DerWrap(0xa0, B, b, A);
    if (MechToken)
    {
        b = DerWrap(0x04, MechToken, MechTokenLen, B);
        a += DerWrap(0xa2, B, b, A + a);
    }
    b = DerWrap(0x30, A, a, B);
    a = DerWrap(0xa0, B, b, A);
    memmove(B, (const BYTE[]){ 0x06, sizeof(OidSpnego) }, 2);
    memcpy(B + 2, OidSpnego, sizeof(OidSpnego));
    memcpy(B + 2 + sizeof(OidSpnego), A, a);
    return DerWrap(0x60, B, 2 + sizeof(OidSpnego) + a, Out);
}

typedef struct _RESP
{
    int NegState;
    BOOL HaveMech;
    const BYTE *Token;
    ULONG TokenLen;
    const BYTE *Mic;
    ULONG MicLen;
} RESP;

static BOOL
ParseResp(const BYTE *Buf, ULONG Len, RESP *Resp)
{
    ULONG Pos = 0, Field, Inner, Pos2, Len2;
    BYTE Tag, Tag2;

    ZeroMemory(Resp, sizeof(*Resp));
    Resp->NegState = -1;
    if (!DerRead(Buf, Len, &Pos, &Tag, &Field) || Tag != 0xa1)
        return FALSE;
    if (!DerRead(Buf, Len, &Pos, &Tag, &Field) || Tag != 0x30)
        return FALSE;
    Inner = Pos + Field;
    while (Pos < Inner)
    {
        if (!DerRead(Buf, Inner, &Pos, &Tag, &Field))
            return FALSE;
        Pos2 = Pos;
        if (!DerRead(Buf, Pos + Field, &Pos2, &Tag2, &Len2))
            return FALSE;
        if (Tag == 0xa0 && Tag2 == 0x0a && Len2 == 1)
            Resp->NegState = Buf[Pos2];
        else if (Tag == 0xa1 && Tag2 == 0x06)
            Resp->HaveMech = TRUE;
        else if (Tag == 0xa2 && Tag2 == 0x04)
        {
            Resp->Token = Buf + Pos2;
            Resp->TokenLen = Len2;
        }
        else if (Tag == 0xa3 && Tag2 == 0x04)
        {
            Resp->Mic = Buf + Pos2;
            Resp->MicLen = Len2;
        }
        Pos += Field;
    }
    return TRUE;
}

static ULONG
BuildNegTokenResp(const BYTE *Token, ULONG TokenLen, const BYTE *Mic, ULONG MicLen, BYTE *Out)
{
    BYTE A[MAX_TOKEN], B[MAX_TOKEN];
    ULONG a = 0, b;

    if (Token)
    {
        b = DerWrap(0x04, Token, TokenLen, B);
        a += DerWrap(0xa2, B, b, A + a);
    }
    if (Mic)
    {
        b = DerWrap(0x04, Mic, MicLen, B);
        a += DerWrap(0xa3, B, b, A + a);
    }
    b = DerWrap(0x30, A, a, B);
    return DerWrap(0xa1, B, b, Out);
}

static SECURITY_STATUS
AcquireClient(PCWSTR Package, PCWSTR Password, PEER *Peer)
{
    SEC_WINNT_AUTH_IDENTITY_W Id;
    WCHAR Domain[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Size = ARRAYSIZE(Domain);
    TimeStamp Expiry;

    GetComputerNameW(Domain, &Size);
    Id.User = (USHORT *)TEST_USER;
    Id.UserLength = (ULONG)wcslen(TEST_USER);
    Id.Domain = (USHORT *)Domain;
    Id.DomainLength = Size;
    Id.Password = (USHORT *)Password;
    Id.PasswordLength = (ULONG)wcslen(Password);
    Id.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
    ZeroMemory(Peer, sizeof(*Peer));
    return AcquireCredentialsHandleW(NULL, (PWSTR)Package, SECPKG_CRED_OUTBOUND, NULL, &Id,
                                     NULL, NULL, &Peer->Cred, &Expiry);
}

static SECURITY_STATUS
AcquireServer(PCWSTR Package, PEER *Peer)
{
    TimeStamp Expiry;

    ZeroMemory(Peer, sizeof(*Peer));
    return AcquireCredentialsHandleW(NULL, (PWSTR)Package, SECPKG_CRED_INBOUND, NULL, NULL,
                                     NULL, NULL, &Peer->Cred, &Expiry);
}

static SECURITY_STATUS
ClientStep(PEER *Peer, const BYTE *In, ULONG InLen)
{
    SecBuffer InBuf = { InLen, SECBUFFER_TOKEN, (PVOID)In }, OutBuf = { MAX_TOKEN, SECBUFFER_TOKEN, Peer->Out };
    SecBufferDesc InDesc = { SECBUFFER_VERSION, 1, &InBuf }, OutDesc = { SECBUFFER_VERSION, 1, &OutBuf };
    TimeStamp Expiry;

    Peer->Status = InitializeSecurityContextW(&Peer->Cred, Peer->HaveCtxt ? &Peer->Ctxt : NULL, (PWSTR)Peer->Target,
                                              ISC_FLAGS, 0, SECURITY_NATIVE_DREP, In ? &InDesc : NULL, 0,
                                              &Peer->Ctxt, &OutDesc, &Peer->Attr, &Expiry);
    if (Peer->Status == SEC_E_OK || Peer->Status == SEC_I_CONTINUE_NEEDED)
        Peer->HaveCtxt = TRUE;
    Peer->OutLen = OutBuf.cbBuffer;
    return Peer->Status;
}

static SECURITY_STATUS
ServerStep(PEER *Peer, const BYTE *In, ULONG InLen)
{
    SecBuffer InBuf = { InLen, SECBUFFER_TOKEN, (PVOID)In }, OutBuf = { MAX_TOKEN, SECBUFFER_TOKEN, Peer->Out };
    SecBufferDesc InDesc = { SECBUFFER_VERSION, 1, &InBuf }, OutDesc = { SECBUFFER_VERSION, 1, &OutBuf };
    TimeStamp Expiry;

    Peer->Status = AcceptSecurityContext(&Peer->Cred, Peer->HaveCtxt ? &Peer->Ctxt : NULL, &InDesc,
                                         ASC_FLAGS, SECURITY_NATIVE_DREP, &Peer->Ctxt, &OutDesc,
                                         &Peer->Attr, &Expiry);
    if (Peer->Status == SEC_E_OK || Peer->Status == SEC_I_CONTINUE_NEEDED ||
        Peer->Status == SEC_I_COMPLETE_NEEDED || Peer->Status == SEC_I_COMPLETE_AND_CONTINUE)
        Peer->HaveCtxt = TRUE;
    Peer->OutLen = OutBuf.cbBuffer;
    return Peer->Status;
}


static void
Trace(const char *Label, int Leg, const char *Side, const PEER *Peer)
{
    char Desc[1024];

    Describe(Peer->Out, Peer->OutLen, Desc, sizeof(Desc));
    trace("%s leg%d %s: status 0x%08lx attr 0x%08lx out %s\n", Label, Leg, Side,
          Peer->Status, Peer->Attr, Desc);
}

static BOOL
RunExchange(const char *Label, PEER *Client, PEER *Server, int *Legs)
{
    int Leg;

    ClientStep(Client, NULL, 0);
    Trace(Label, 1, "client", Client);
    for (Leg = 1; Leg < 8; Leg++)
    {
        if (Client->Status != SEC_E_OK && Client->Status != SEC_I_CONTINUE_NEEDED)
            break;
        if (!Client->OutLen && Client->Status == SEC_E_OK)
            break;
        ServerStep(Server, Client->Out, Client->OutLen);
        Trace(Label, Leg, "server", Server);
        if (Server->Status != SEC_E_OK && Server->Status != SEC_I_CONTINUE_NEEDED)
            break;
        if (Client->Status == SEC_E_OK)
            break;
        if (!Server->OutLen)
            break;
        ClientStep(Client, Server->Out, Server->OutLen);
        Trace(Label, Leg + 1, "client", Client);
        if (Client->Status == SEC_E_OK && Server->Status == SEC_E_OK)
            break;
    }
    *Legs = Leg;
    return Client->Status == SEC_E_OK && Server->Status == SEC_E_OK;
}

static void
FreePeer(PEER *Peer)
{
    if (Peer->HaveCtxt)
        DeleteSecurityContext(&Peer->Ctxt);
    FreeCredentialsHandle(&Peer->Cred);
}

static void
CheckNegotiationInfo(const char *Label, PEER *Peer)
{
    SecPkgContext_NegotiationInfoW Nego;
    SECURITY_STATUS Status;

    ZeroMemory(&Nego, sizeof(Nego));
    Status = QueryContextAttributesW(&Peer->Ctxt, SECPKG_ATTR_NEGOTIATION_INFO, &Nego);
    ok(Status == SEC_E_OK, "%s: NEGOTIATION_INFO 0x%08lx\n", Label, Status);
    if (Status != SEC_E_OK)
        return;
    ok(Nego.NegotiationState == SECPKG_NEGOTIATION_COMPLETE, "%s: negotiation state %lu\n", Label,
       Nego.NegotiationState);
    ok(Nego.PackageInfo && !wcscmp(Nego.PackageInfo->Name, L"NTLM"), "%s: package %ls\n", Label,
       Nego.PackageInfo ? Nego.PackageInfo->Name : L"(null)");
    if (Nego.PackageInfo)
        FreeContextBuffer(Nego.PackageInfo);
}

static void
TestSealing(const char *Label, PEER *From, PEER *To, SECURITY_STATUS Expected)
{
    BYTE Trailer[16], Data[16];
    SecBuffer Bufs[2];
    SecBufferDesc Desc = { SECBUFFER_VERSION, 2, Bufs };
    SECURITY_STATUS Status;
    ULONG Qop = 0;

    memcpy(Data, "sealing the data", sizeof(Data));
    Bufs[0].BufferType = SECBUFFER_TOKEN;
    Bufs[0].cbBuffer = sizeof(Trailer);
    Bufs[0].pvBuffer = Trailer;
    Bufs[1].BufferType = SECBUFFER_DATA;
    Bufs[1].cbBuffer = sizeof(Data);
    Bufs[1].pvBuffer = Data;
    Status = EncryptMessage(&From->Ctxt, 0, &Desc, 0);
    ok(Status == SEC_E_OK, "%s: EncryptMessage 0x%08lx\n", Label, Status);
    Status = DecryptMessage(&To->Ctxt, &Desc, 0, &Qop);
    ok(Status == Expected, "%s: DecryptMessage 0x%08lx, expected 0x%08lx\n", Label, Status, Expected);
    if (Status == SEC_E_OK)
        ok(!memcmp(Data, "sealing the data", sizeof(Data)), "%s: data differs after the round trip\n", Label);
}

static void
TestSigning(const char *Label, PEER *From, PEER *To)
{
    BYTE Signature[16], Data[16];
    SecBuffer Bufs[2];
    SecBufferDesc Desc = { SECBUFFER_VERSION, 2, Bufs };
    SECURITY_STATUS Status;
    ULONG Qop = 0;

    memcpy(Data, "signing the data", sizeof(Data));
    Bufs[0].BufferType = SECBUFFER_DATA;
    Bufs[0].cbBuffer = sizeof(Data);
    Bufs[0].pvBuffer = Data;
    Bufs[1].BufferType = SECBUFFER_TOKEN;
    Bufs[1].cbBuffer = sizeof(Signature);
    Bufs[1].pvBuffer = Signature;
    Status = MakeSignature(&From->Ctxt, 0, &Desc, 0);
    ok(Status == SEC_E_OK, "%s: MakeSignature 0x%08lx\n", Label, Status);
    Status = VerifySignature(&To->Ctxt, &Desc, 0, &Qop);
    ok(Status == SEC_E_OK, "%s: VerifySignature 0x%08lx\n", Label, Status);

    Bufs[1].cbBuffer = sizeof(Signature);
    Status = MakeSignature(&From->Ctxt, 0, &Desc, 0);
    ok(Status == SEC_E_OK, "%s: MakeSignature 0x%08lx\n", Label, Status);
    Signature[5] ^= 0x55;
    Status = VerifySignature(&To->Ctxt, &Desc, 0, &Qop);
    ok(Status == SEC_E_MESSAGE_ALTERED, "%s: VerifySignature with a changed checksum 0x%08lx\n", Label, Status);
}

static void
TestSealingChecksum(const char *Label, PEER *From, PEER *To)
{
    BYTE Trailer[16], Data[16];
    SecBuffer Bufs[2];
    SecBufferDesc Desc = { SECBUFFER_VERSION, 2, Bufs };
    SECURITY_STATUS Status;
    ULONG Qop = 0;

    memcpy(Data, "sealing the data", sizeof(Data));
    Bufs[0].BufferType = SECBUFFER_TOKEN;
    Bufs[0].cbBuffer = sizeof(Trailer);
    Bufs[0].pvBuffer = Trailer;
    Bufs[1].BufferType = SECBUFFER_DATA;
    Bufs[1].cbBuffer = sizeof(Data);
    Bufs[1].pvBuffer = Data;
    Status = EncryptMessage(&From->Ctxt, 0, &Desc, 0);
    ok(Status == SEC_E_OK, "%s: EncryptMessage 0x%08lx\n", Label, Status);
    Trailer[5] ^= 0x55;
    Status = DecryptMessage(&To->Ctxt, &Desc, 0, &Qop);
    ok(Status == SEC_E_MESSAGE_ALTERED, "%s: DecryptMessage with a changed checksum 0x%08lx\n", Label, Status);
}

static void
TestServerToken(const char *Label, PEER *Server)
{
    WCHAR Name[64], Domain[64];
    DWORD NameLen = ARRAYSIZE(Name), DomainLen = ARRAYSIZE(Domain), Size;
    BYTE Buffer[256];
    SID_NAME_USE Use;
    HANDLE Token;
    SECURITY_STATUS Status;

    Status = QuerySecurityContextToken(&Server->Ctxt, &Token);
    ok(Status == SEC_E_OK, "%s: QuerySecurityContextToken 0x%08lx\n", Label, Status);
    if (Status == SEC_E_OK)
    {
        if (GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Size) &&
            LookupAccountSidW(NULL, ((TOKEN_USER *)Buffer)->User.Sid, Name, &NameLen, Domain, &DomainLen, &Use))
            ok(!_wcsicmp(Name, TEST_USER), "%s: token user %ls\n", Label, Name);
        else
            ok(0, "%s: token user query failed %lu\n", Label, GetLastError());
        CloseHandle(Token);
    }
    Status = ImpersonateSecurityContext(&Server->Ctxt);
    ok(Status == SEC_E_OK, "%s: ImpersonateSecurityContext 0x%08lx\n", Label, Status);
    if (Status == SEC_E_OK)
    {
        NameLen = ARRAYSIZE(Name);
        ok(GetUserNameW(Name, &NameLen) && !_wcsicmp(Name, TEST_USER), "%s: impersonated user %ls\n", Label, Name);
        Status = RevertSecurityContext(&Server->Ctxt);
        ok(Status == SEC_E_OK, "%s: RevertSecurityContext 0x%08lx\n", Label, Status);
    }
}

static void
TestPlainExchange(const char *Label, PCWSTR ClientPackage, PCWSTR ServerPackage, PCWSTR Target)
{
    PEER Client, Server;
    int Legs;
    BOOL Done;

    ok(AcquireClient(ClientPackage, TEST_PASSWORD, &Client) == SEC_E_OK, "%s: client credentials\n", Label);
    ok(AcquireServer(ServerPackage, &Server) == SEC_E_OK, "%s: server credentials\n", Label);
    Client.Target = Target;
    Done = RunExchange(Label, &Client, &Server, &Legs);
    ok(Done, "%s: exchange did not complete: client 0x%08lx server 0x%08lx\n", Label, Client.Status, Server.Status);
    ok(Legs == 2, "%s: %d legs\n", Label, Legs);
    if (Done)
    {
        if (!wcscmp(ServerPackage, L"Negotiate"))
            CheckNegotiationInfo(Label, &Server);
        TestServerToken(Label, &Server);
        if (!wcscmp(ClientPackage, L"Negotiate"))
            CheckNegotiationInfo(Label, &Client);
        TestSealing(Label, &Client, &Server, SEC_E_OK);
        TestSealing(Label, &Server, &Client, SEC_E_OK);
        TestSigning(Label, &Client, &Server);
        TestSigning(Label, &Server, &Client);
        TestSealingChecksum(Label, &Client, &Server);
        TestSealingChecksum(Label, &Server, &Client);
    }
    FreePeer(&Client);
    FreePeer(&Server);
}

static void
TestNegotiateClientToken(void)
{
    PEER Client;

    ok(AcquireClient(L"Negotiate", TEST_PASSWORD, &Client) == SEC_E_OK, "client credentials\n");
    Client.Target = L"TERMSRV/localhost";
    ClientStep(&Client, NULL, 0);
    ok(Client.Status == SEC_I_CONTINUE_NEEDED, "first InitializeSecurityContext 0x%08lx\n", Client.Status);
    ok(Client.OutLen >= 12 && !memcmp(Client.Out, "NTLMSSP", 8) && Client.Out[8] == 1,
       "the first token is not a raw NTLM NEGOTIATE message (%lu bytes, %02x)\n", Client.OutLen, Client.Out[0]);
    FreePeer(&Client);
}

static void
TestWrongPassword(void)
{
    PEER Client, Server;
    int Legs;

    ok(AcquireClient(L"Negotiate", L"wrong-password", &Client) == SEC_E_OK, "client credentials\n");
    ok(AcquireServer(L"Negotiate", &Server) == SEC_E_OK, "server credentials\n");
    ok(!RunExchange("wrong-password", &Client, &Server, &Legs), "exchange completed with a wrong password\n");
    ok(Server.Status == SEC_E_LOGON_DENIED, "server returned 0x%08lx\n", Server.Status);
    ok(Server.OutLen == MAX_TOKEN, "output length %lu\n", Server.OutLen);
    FreePeer(&Client);
    FreePeer(&Server);
}

static void
CheckResp(const char *Label, const PEER *Server, int NegState, BOOL Mech, BOOL Token, ULONG MicLen)
{
    RESP Resp;

    ok(ParseResp(Server->Out, Server->OutLen, &Resp), "%s: the answer is not a NegTokenResp\n", Label);
    ok(Resp.NegState == NegState, "%s: negState %d, expected %d\n", Label, Resp.NegState, NegState);
    ok(Resp.HaveMech == Mech, "%s: supportedMech %d, expected %d\n", Label, Resp.HaveMech, Mech);
    ok((Resp.Token != NULL) == Token, "%s: responseToken %lu bytes, expected %d\n", Label, Resp.TokenLen, Token);
    if (Resp.Token)
        ok(Resp.TokenLen >= 12 && !memcmp(Resp.Token, "NTLMSSP", 8) && Resp.Token[8] == 2,
           "%s: the responseToken is not an NTLM CHALLENGE message\n", Label);
    ok(Resp.MicLen == MicLen, "%s: mechListMIC %lu bytes, expected %lu\n", Label, Resp.MicLen, MicLen);
}

static void
TestInvalidTokens(void)
{
    static const BYTE Garbage[] = { 0x60, 0x05, 0x06, 0x03, 0x2a, 0x03, 0x04 };
    const BYTE *Oids[1];
    ULONG Lens[1], Len;
    BYTE Token[MAX_TOKEN];
    PEER Server;

    AcquireServer(L"Negotiate", &Server);
    ServerStep(&Server, Garbage, sizeof(Garbage));
    ok(Server.Status == SEC_E_INVALID_TOKEN, "garbage: 0x%08lx\n", Server.Status);
    ok(Server.OutLen == MAX_TOKEN, "garbage: output length %lu\n", Server.OutLen);
    FreePeer(&Server);

    Oids[0] = OidKrb5;
    Lens[0] = sizeof(OidKrb5);
    Len = BuildNegTokenInit(Oids, Lens, 1, NULL, 0, Token);
    AcquireServer(L"Negotiate", &Server);
    ServerStep(&Server, Token, Len);
    ok(Server.Status == SEC_E_INVALID_TOKEN, "Kerberos only: 0x%08lx\n", Server.Status);
    FreePeer(&Server);
}

static void
TestNegTokenInit(void)
{
    static const BYTE FakeKrb[] = { 0x60, 0x03, 0x06, 0x01, 0x00 };
    const BYTE *Oids[3];
    ULONG Lens[3], Len;
    BYTE Token[MAX_TOKEN];
    PEER Server, Ntlm;

    Oids[0] = OidNtlm;
    Lens[0] = sizeof(OidNtlm);
    Len = BuildNegTokenInit(Oids, Lens, 1, NULL, 0, Token);
    AcquireServer(L"Negotiate", &Server);
    ServerStep(&Server, Token, Len);
    ok(Server.Status == SEC_I_CONTINUE_NEEDED, "NTLM without a token: 0x%08lx\n", Server.Status);
    CheckResp("NTLM without a token", &Server, 1, TRUE, FALSE, 0);
    FreePeer(&Server);

    Oids[0] = OidMsKrb5;
    Lens[0] = sizeof(OidMsKrb5);
    Oids[1] = OidKrb5;
    Lens[1] = sizeof(OidKrb5);
    Oids[2] = OidNtlm;
    Lens[2] = sizeof(OidNtlm);
    Len = BuildNegTokenInit(Oids, Lens, 3, FakeKrb, sizeof(FakeKrb), Token);
    AcquireServer(L"Negotiate", &Server);
    ServerStep(&Server, Token, Len);
    ok(Server.Status == SEC_I_CONTINUE_NEEDED, "Kerberos first: 0x%08lx\n", Server.Status);
    CheckResp("Kerberos first", &Server, 3, TRUE, FALSE, 0);
    FreePeer(&Server);

    AcquireClient(L"NTLM", TEST_PASSWORD, &Ntlm);
    ClientStep(&Ntlm, NULL, 0);
    Len = BuildNegTokenInit(Oids + 2, Lens + 2, 1, Ntlm.Out, Ntlm.OutLen, Token);
    AcquireServer(L"Negotiate", &Server);
    ServerStep(&Server, Token, Len);
    ok(Server.Status == SEC_I_CONTINUE_NEEDED, "NTLM with a token: 0x%08lx\n", Server.Status);
    ok(Server.Attr == 0x81c, "NTLM with a token: attributes 0x%08lx\n", Server.Attr);
    CheckResp("NTLM with a token", &Server, 1, TRUE, TRUE, 0);
    FreePeer(&Server);
    FreePeer(&Ntlm);
}

enum { MIC_GOOD, MIC_NONE, MIC_BAD };

static void
TestSpnego(const char *Label, BOOL KerberosFirst, int MicMode)
{
    const BYTE *Oids[2];
    ULONG Lens[2], Count = 0, Len, MechTypesLen, ListLen = 0, i;
    BYTE Token[MAX_TOKEN], MechTypes[64], List[64], Mic[16];
    PEER Ntlm, Server;
    RESP Resp;
    SecBuffer Bufs[2];
    SecBufferDesc Desc = { SECBUFFER_VERSION, 2, Bufs };
    SECURITY_STATUS Status;
    ULONG Qop;

    if (KerberosFirst)
    {
        Oids[Count] = OidKrb5;
        Lens[Count++] = sizeof(OidKrb5);
    }
    Oids[Count] = OidNtlm;
    Lens[Count++] = sizeof(OidNtlm);
    for (i = 0; i < Count; i++)
        ListLen += DerWrap(0x06, Oids[i], Lens[i], List + ListLen);
    MechTypesLen = DerWrap(0x30, List, ListLen, MechTypes);
    Bufs[0].BufferType = SECBUFFER_DATA;
    Bufs[0].pvBuffer = MechTypes;
    Bufs[1].BufferType = SECBUFFER_TOKEN;
    Bufs[1].pvBuffer = Mic;

    AcquireClient(L"NTLM", TEST_PASSWORD, &Ntlm);
    AcquireServer(L"Negotiate", &Server);
    ClientStep(&Ntlm, NULL, 0);
    Len = BuildNegTokenInit(Oids, Lens, Count, KerberosFirst ? NULL : Ntlm.Out, KerberosFirst ? 0 : Ntlm.OutLen, Token);
    ServerStep(&Server, Token, Len);
    Trace(Label, 1, "server", &Server);
    ok(Server.Status == SEC_I_CONTINUE_NEEDED, "%s: leg 1 0x%08lx\n", Label, Server.Status);
    if (Server.Status != SEC_I_CONTINUE_NEEDED)
        goto done;
    if (KerberosFirst)
    {
        CheckResp(Label, &Server, 3, TRUE, FALSE, 0);
        Len = BuildNegTokenResp(Ntlm.Out, Ntlm.OutLen, NULL, 0, Token);
        ServerStep(&Server, Token, Len);
        Trace(Label, 2, "server", &Server);
        ok(Server.Status == SEC_I_CONTINUE_NEEDED, "%s: leg 2 0x%08lx\n", Label, Server.Status);
        if (Server.Status != SEC_I_CONTINUE_NEEDED)
            goto done;
        CheckResp(Label, &Server, 1, FALSE, TRUE, 0);
    }
    else
        CheckResp(Label, &Server, 1, TRUE, TRUE, 0);
    ParseResp(Server.Out, Server.OutLen, &Resp);
    if (!Resp.Token)
        goto done;
    ClientStep(&Ntlm, Resp.Token, Resp.TokenLen);
    ok(Ntlm.Status == SEC_E_OK, "%s: NTLM client 0x%08lx\n", Label, Ntlm.Status);
    if (Ntlm.Status != SEC_E_OK)
        goto done;

    if (MicMode != MIC_NONE)
    {
        Bufs[0].cbBuffer = MechTypesLen;
        Bufs[1].cbBuffer = sizeof(Mic);
        Status = MakeSignature(&Ntlm.Ctxt, 0, &Desc, 0);
        ok(Status == SEC_E_OK, "%s: MakeSignature for the mechListMIC 0x%08lx\n", Label, Status);
        if (MicMode == MIC_BAD)
            Mic[5] ^= 0x55;
    }
    Len = BuildNegTokenResp(Ntlm.Out, Ntlm.OutLen, MicMode != MIC_NONE ? Mic : NULL, sizeof(Mic), Token);
    ServerStep(&Server, Token, Len);
    Trace(Label, 3, "server", &Server);
    if (MicMode == MIC_BAD)
    {
        ok(Server.Status == SEC_E_MESSAGE_ALTERED, "%s: leg 3 0x%08lx\n", Label, Server.Status);
        ok(Server.OutLen == MAX_TOKEN, "%s: output length %lu\n", Label, Server.OutLen);
        goto done;
    }
    if (MicMode == MIC_NONE)
    {
        ok(Server.Status == SEC_I_CONTINUE_NEEDED, "%s: leg 3 0x%08lx\n", Label, Server.Status);
        if (Server.Status != SEC_I_CONTINUE_NEEDED)
            goto done;
        CheckResp(Label, &Server, 1, FALSE, FALSE, 16);
        ParseResp(Server.Out, Server.OutLen, &Resp);
        if (Resp.MicLen == sizeof(Mic))
        {
            memcpy(Mic, Resp.Mic, sizeof(Mic));
            Bufs[0].cbBuffer = MechTypesLen;
            Bufs[1].cbBuffer = sizeof(Mic);
            Status = VerifySignature(&Ntlm.Ctxt, &Desc, 0, &Qop);
            ok(Status == SEC_E_OK, "%s: the server mechListMIC does not verify: 0x%08lx\n", Label, Status);
        }
        Bufs[0].cbBuffer = MechTypesLen;
        Bufs[1].cbBuffer = sizeof(Mic);
        Status = MakeSignature(&Ntlm.Ctxt, 0, &Desc, 0);
        Len = BuildNegTokenResp(NULL, 0, Mic, sizeof(Mic), Token);
        ServerStep(&Server, Token, Len);
        Trace(Label, 4, "server", &Server);
        ok(Server.Status == SEC_E_INVALID_TOKEN, "%s: leg 4 0x%08lx\n", Label, Server.Status);
        ok(Server.OutLen == MAX_TOKEN, "%s: output length %lu\n", Label, Server.OutLen);
        goto done;
    }

    ok(Server.Status == SEC_E_OK, "%s: leg 3 0x%08lx\n", Label, Server.Status);
    if (Server.Status != SEC_E_OK)
        goto done;
    CheckResp(Label, &Server, 0, FALSE, FALSE, 16);
    ParseResp(Server.Out, Server.OutLen, &Resp);
    if (Resp.MicLen == sizeof(Mic))
    {
        memcpy(Mic, Resp.Mic, sizeof(Mic));
        Bufs[0].cbBuffer = MechTypesLen;
        Bufs[1].cbBuffer = sizeof(Mic);
        Status = VerifySignature(&Ntlm.Ctxt, &Desc, 0, &Qop);
        ok(Status == SEC_E_OK, "%s: the server mechListMIC does not verify: 0x%08lx\n", Label, Status);
    }
    CheckNegotiationInfo(Label, &Server);
    TestServerToken(Label, &Server);
    TestSealing(Label, &Ntlm, &Server, SEC_E_MESSAGE_ALTERED);
    TestSealing(Label, &Server, &Ntlm, SEC_E_MESSAGE_ALTERED);
done:
    FreePeer(&Ntlm);
    FreePeer(&Server);
}

static void
TestPackageInfo(void)
{
    PSecPkgInfoW Info;
    SECURITY_STATUS Status;

    Status = QuerySecurityPackageInfoW(L"Negotiate", &Info);
    ok(Status == SEC_E_OK, "QuerySecurityPackageInfoW 0x%08lx\n", Status);
    if (Status == SEC_E_OK)
    {
        ok(!wcscmp(Info->Name, L"Negotiate"), "name %ls\n", Info->Name);
        ok(Info->wRPCID == RPC_C_AUTHN_GSS_NEGOTIATE, "RPC ID %u\n", Info->wRPCID);
        ok(Info->wVersion == 1, "version %u\n", Info->wVersion);
        FreeContextBuffer(Info);
    }
}

static BOOL
CreateTestUser(void)
{
    USER_INFO_1 Info;
    NET_API_STATUS Status;
    DWORD Error;

    ZeroMemory(&Info, sizeof(Info));
    Info.usri1_name = (LPWSTR)TEST_USER;
    Info.usri1_password = (LPWSTR)TEST_PASSWORD;
    Info.usri1_priv = USER_PRIV_USER;
    Info.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    NetUserDel(NULL, TEST_USER);
    Status = NetUserAdd(NULL, 1, (LPBYTE)&Info, &Error);
    if (Status != NERR_Success)
    {
        skip("NetUserAdd failed with %lu (run elevated)\n", Status);
        return FALSE;
    }
    return TRUE;
}

START_TEST(Negotiate)
{
    WCHAR Spn[MAX_COMPUTERNAME_LENGTH + 16] = L"TERMSRV/";
    DWORD Size = MAX_COMPUTERNAME_LENGTH + 1;

    winetest_mute_threshold = 10000;
    GetComputerNameW(Spn + 8, &Size);
    TestPackageInfo();
    if (!CreateTestUser())
        return;
    TestPlainExchange("negotiate", L"Negotiate", L"Negotiate", NULL);
    TestPlainExchange("negotiate to TERMSRV/self", L"Negotiate", L"Negotiate", Spn);
    TestPlainExchange("negotiate to TERMSRV/localhost", L"Negotiate", L"Negotiate", L"TERMSRV/localhost");
    TestPlainExchange("NTLM client", L"NTLM", L"Negotiate", NULL);
    TestPlainExchange("NTLM server", L"Negotiate", L"NTLM", NULL);
    TestNegotiateClientToken();
    TestWrongPassword();
    TestInvalidTokens();
    TestNegTokenInit();
    TestSpnego("SPNEGO", FALSE, MIC_GOOD);
    TestSpnego("SPNEGO without MIC", FALSE, MIC_NONE);
    TestSpnego("SPNEGO with a bad MIC", FALSE, MIC_BAD);
    TestSpnego("SPNEGO Kerberos first", TRUE, MIC_GOOD);
    TestSpnego("SPNEGO Kerberos first without MIC", TRUE, MIC_NONE);
    NetUserDel(NULL, TEST_USER);
}
