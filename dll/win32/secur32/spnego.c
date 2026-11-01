/*
 * PROJECT:     LiberNT Security Support Provider Interface
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SPNEGO exchange of the Negotiate package
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define NEGO_MAX_NTLM_TOKEN 2888

#define NEG_STATE_ACCEPT_COMPLETED  0
#define NEG_STATE_ACCEPT_INCOMPLETE 1
#define NEG_STATE_REQUEST_MIC       3

typedef enum _NEGO_STATE
{
    NegoStateRaw,
    NegoStateMechToken,
    NegoStateAuthenticate,
    NegoStateDone
} NEGO_STATE;

typedef struct _NEGO_CONTEXT
{
    CtxtHandle Ntlm;
    BOOL HaveNtlm;
    NEGO_STATE State;
    BYTE *MechTypes;
    ULONG MechTypesLength;
} NEGO_CONTEXT, *PNEGO_CONTEXT;

typedef struct _NEGO_TOKEN
{
    const BYTE *MechTypes;
    ULONG MechTypesLength;
    BOOL NtlmFirst;
    BOOL NtlmListed;
    const BYTE *MechToken;
    ULONG MechTokenLength;
    const BYTE *MechListMic;
    ULONG MechListMicLength;
} NEGO_TOKEN, *PNEGO_TOKEN;

typedef struct _NTLM_RC4_STATE
{
    arc4_info Ntlm;
    arc4_info Send;
    arc4_info Recv;
} NTLM_RC4_STATE;

static const BYTE SpnegoOid[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02 };
static const BYTE NtlmOid[] = { 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a };

static BOOL
DerRead(const BYTE *Buffer, ULONG End, ULONG *Position, BYTE *Tag, ULONG *Length)
{
    ULONG Pos = *Position, Len, Count;

    if (Pos >= End || End - Pos < 2)
        return FALSE;
    *Tag = Buffer[Pos++];
    Len = Buffer[Pos++];
    if (Len & 0x80)
    {
        Count = Len & 0x7f;
        if (Count == 0 || Count > 4 || End - Pos < Count)
            return FALSE;
        for (Len = 0; Count; Count--)
        {
            if (Len > 0xffffff)
                return FALSE;
            Len = (Len << 8) | Buffer[Pos++];
        }
    }
    if (Len > End - Pos)
        return FALSE;
    *Position = Pos;
    *Length = Len;
    return TRUE;
}

static ULONG
DerHeaderSize(ULONG Length)
{
    if (Length < 0x80)
        return 2;
    if (Length < 0x100)
        return 3;
    if (Length < 0x10000)
        return 4;
    return 5;
}

static ULONG
DerPutHeader(BYTE *Out, BYTE Tag, ULONG Length)
{
    ULONG Size = DerHeaderSize(Length), i;

    Out[0] = Tag;
    if (Size == 2)
    {
        Out[1] = (BYTE)Length;
        return 2;
    }
    Out[1] = (BYTE)(0x80 | (Size - 2));
    for (i = Size - 1; i >= 2; i--)
    {
        Out[i] = (BYTE)Length;
        Length >>= 8;
    }
    return Size;
}

static BOOL
ParseNegTokenInit(const BYTE *Buffer, ULONG Size, PNEGO_TOKEN Token)
{
    ULONG Pos = 0, Len, End, FieldEnd, Inner, ListEnd, Index;
    BYTE Tag;

    ZeroMemory(Token, sizeof(*Token));
    if (!DerRead(Buffer, Size, &Pos, &Tag, &Len) || Tag != 0x60)
        return FALSE;
    End = Pos + Len;
    if (!DerRead(Buffer, End, &Pos, &Tag, &Len) || Tag != 0x06 ||
        Len != sizeof(SpnegoOid) || memcmp(Buffer + Pos, SpnegoOid, Len))
        return FALSE;
    Pos += Len;
    if (!DerRead(Buffer, End, &Pos, &Tag, &Len) || Tag != 0xa0)
        return FALSE;
    End = Pos + Len;
    if (!DerRead(Buffer, End, &Pos, &Tag, &Len) || Tag != 0x30)
        return FALSE;
    End = Pos + Len;
    while (Pos < End)
    {
        if (!DerRead(Buffer, End, &Pos, &Tag, &Len))
            return FALSE;
        FieldEnd = Pos + Len;
        if (Tag == 0xa0)
        {
            Token->MechTypes = Buffer + Pos;
            Inner = Pos;
            if (!DerRead(Buffer, FieldEnd, &Inner, &Tag, &Len) || Tag != 0x30)
                return FALSE;
            Token->MechTypesLength = Inner + Len - Pos;
            ListEnd = Inner + Len;
            for (Index = 0; Inner < ListEnd; Index++)
            {
                if (!DerRead(Buffer, ListEnd, &Inner, &Tag, &Len) || Tag != 0x06)
                    return FALSE;
                if (Len == sizeof(NtlmOid) && !memcmp(Buffer + Inner, NtlmOid, Len) && !Token->NtlmListed)
                {
                    Token->NtlmListed = TRUE;
                    Token->NtlmFirst = (Index == 0);
                }
                Inner += Len;
            }
        }
        else if (Tag == 0xa2)
        {
            Inner = Pos;
            if (!DerRead(Buffer, FieldEnd, &Inner, &Tag, &Len) || Tag != 0x04)
                return FALSE;
            Token->MechToken = Buffer + Inner;
            Token->MechTokenLength = Len;
        }
        Pos = FieldEnd;
    }
    return Token->MechTypes != NULL;
}

static BOOL
ParseNegTokenResp(const BYTE *Buffer, ULONG Size, PNEGO_TOKEN Token)
{
    ULONG Pos = 0, Len, End, FieldEnd, Inner;
    BYTE Tag;

    ZeroMemory(Token, sizeof(*Token));
    if (!DerRead(Buffer, Size, &Pos, &Tag, &Len) || Tag != 0xa1)
        return FALSE;
    End = Pos + Len;
    if (!DerRead(Buffer, End, &Pos, &Tag, &Len) || Tag != 0x30)
        return FALSE;
    End = Pos + Len;
    while (Pos < End)
    {
        if (!DerRead(Buffer, End, &Pos, &Tag, &Len))
            return FALSE;
        FieldEnd = Pos + Len;
        Inner = Pos;
        if (Tag == 0xa2 || Tag == 0xa3)
        {
            BYTE Field = Tag;

            if (!DerRead(Buffer, FieldEnd, &Inner, &Tag, &Len) || Tag != 0x04)
                return FALSE;
            if (Field == 0xa2)
            {
                Token->MechToken = Buffer + Inner;
                Token->MechTokenLength = Len;
            }
            else
            {
                Token->MechListMic = Buffer + Inner;
                Token->MechListMicLength = Len;
            }
        }
        Pos = FieldEnd;
    }
    return TRUE;
}

static BYTE *
BuildNegTokenResp(ULONG NegState, BOOL SupportedMech, const BYTE *ResponseToken, ULONG ResponseTokenLength,
                  const BYTE *MechListMic, ULONG MechListMicLength, ULONG *Size)
{
    ULONG Fields, Sequence, Total, Pos = 0;
    BYTE *Out;

    Fields = 5;
    if (SupportedMech)
        Fields += 2 + 2 + sizeof(NtlmOid);
    if (ResponseToken)
        Fields += DerHeaderSize(DerHeaderSize(ResponseTokenLength) + ResponseTokenLength) +
                  DerHeaderSize(ResponseTokenLength) + ResponseTokenLength;
    if (MechListMic)
        Fields += DerHeaderSize(DerHeaderSize(MechListMicLength) + MechListMicLength) +
                  DerHeaderSize(MechListMicLength) + MechListMicLength;
    Sequence = DerHeaderSize(Fields) + Fields;
    Total = DerHeaderSize(Sequence) + Sequence;

    Out = HeapAlloc(GetProcessHeap(), 0, Total);
    if (!Out)
        return NULL;
    Pos += DerPutHeader(Out + Pos, 0xa1, Sequence);
    Pos += DerPutHeader(Out + Pos, 0x30, Fields);
    Pos += DerPutHeader(Out + Pos, 0xa0, 3);
    Pos += DerPutHeader(Out + Pos, 0x0a, 1);
    Out[Pos++] = (BYTE)NegState;
    if (SupportedMech)
    {
        Pos += DerPutHeader(Out + Pos, 0xa1, 2 + sizeof(NtlmOid));
        Pos += DerPutHeader(Out + Pos, 0x06, sizeof(NtlmOid));
        memcpy(Out + Pos, NtlmOid, sizeof(NtlmOid));
        Pos += sizeof(NtlmOid);
    }
    if (ResponseToken)
    {
        Pos += DerPutHeader(Out + Pos, 0xa2, DerHeaderSize(ResponseTokenLength) + ResponseTokenLength);
        Pos += DerPutHeader(Out + Pos, 0x04, ResponseTokenLength);
        memcpy(Out + Pos, ResponseToken, ResponseTokenLength);
        Pos += ResponseTokenLength;
    }
    if (MechListMic)
    {
        Pos += DerPutHeader(Out + Pos, 0xa3, DerHeaderSize(MechListMicLength) + MechListMicLength);
        Pos += DerPutHeader(Out + Pos, 0x04, MechListMicLength);
        memcpy(Out + Pos, MechListMic, MechListMicLength);
        Pos += MechListMicLength;
    }
    *Size = Pos;
    return Out;
}

static SECURITY_STATUS
WriteOutput(PSecBufferDesc pOutput, ULONG fContextReq, const BYTE *Data, ULONG Size)
{
    PSecBuffer Buffer;

    if (!pOutput || pOutput->cBuffers < 1)
        return SEC_E_INSUFFICIENT_MEMORY;
    Buffer = &pOutput->pBuffers[0];
    if (fContextReq & ASC_REQ_ALLOCATE_MEMORY)
    {
        Buffer->pvBuffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Buffer->pvBuffer)
            return SEC_E_INSUFFICIENT_MEMORY;
    }
    else if (Buffer->cbBuffer < Size)
    {
        return SEC_E_BUFFER_TOO_SMALL;
    }
    memcpy(Buffer->pvBuffer, Data, Size);
    Buffer->cbBuffer = Size;
    return SEC_E_OK;
}

static SECURITY_STATUS
NtlmAcceptStep(PNEGO_CONTEXT Context, PCredHandle phCredential, const BYTE *Token, ULONG TokenLength,
               ULONG fContextReq, ULONG TargetDataRep, BYTE *Out, ULONG *OutLength, ULONG *Attributes,
               PTimeStamp ptsExpiry)
{
    SecBuffer InBuffer = { TokenLength, SECBUFFER_TOKEN, (PVOID)Token };
    SecBuffer OutBuffer = { NEGO_MAX_NTLM_TOKEN, SECBUFFER_TOKEN, Out };
    SecBufferDesc InDesc = { SECBUFFER_VERSION, 1, &InBuffer };
    SecBufferDesc OutDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
    SECURITY_STATUS Status;

    Status = ntlm_AcceptSecurityContext(phCredential, Context->HaveNtlm ? &Context->Ntlm : NULL, &InDesc,
                                        fContextReq & ~ASC_REQ_ALLOCATE_MEMORY, TargetDataRep, &Context->Ntlm,
                                        &OutDesc, Attributes, ptsExpiry);
    if (Status == SEC_E_OK || Status == SEC_I_CONTINUE_NEEDED)
        Context->HaveNtlm = TRUE;
    *OutLength = OutBuffer.cbBuffer;
    return Status;
}

static VOID
SaveRc4State(PNegoHelper Helper, NTLM_RC4_STATE *State)
{
    if (Helper->crypt.ntlm.a4i)
        State->Ntlm = *Helper->crypt.ntlm.a4i;
    if (Helper->crypt.ntlm2.send_a4i)
        State->Send = *Helper->crypt.ntlm2.send_a4i;
    if (Helper->crypt.ntlm2.recv_a4i)
        State->Recv = *Helper->crypt.ntlm2.recv_a4i;
}

static VOID
RestoreRc4State(PNegoHelper Helper, const NTLM_RC4_STATE *State)
{
    if (Helper->crypt.ntlm.a4i)
        *Helper->crypt.ntlm.a4i = State->Ntlm;
    if (Helper->crypt.ntlm2.send_a4i)
        *Helper->crypt.ntlm2.send_a4i = State->Send;
    if (Helper->crypt.ntlm2.recv_a4i)
        *Helper->crypt.ntlm2.recv_a4i = State->Recv;
}

static SECURITY_STATUS
MechListMic(PNEGO_CONTEXT Context, BOOL Verify, BYTE *Mic, ULONG MicLength)
{
    PNegoHelper Helper = (PNegoHelper)Context->Ntlm.dwLower;
    SecBuffer Buffers[2];
    SecBufferDesc Desc = { SECBUFFER_VERSION, 2, Buffers };
    NTLM_RC4_STATE Saved;
    SECURITY_STATUS Status;
    ULONG Qop;

    Buffers[0].BufferType = SECBUFFER_DATA;
    Buffers[0].cbBuffer = Context->MechTypesLength;
    Buffers[0].pvBuffer = Context->MechTypes;
    Buffers[1].BufferType = SECBUFFER_TOKEN;
    Buffers[1].cbBuffer = MicLength;
    Buffers[1].pvBuffer = Mic;

    SaveRc4State(Helper, &Saved);
    if (Verify)
        Status = ntlm_VerifySignature(&Context->Ntlm, &Desc, 0, &Qop);
    else
        Status = ntlm_MakeSignature(&Context->Ntlm, 0, &Desc, 0);
    RestoreRc4State(Helper, &Saved);
    return Status;
}

static VOID
FreeNegoContext(PNEGO_CONTEXT Context)
{
    if (Context->HaveNtlm)
        ntlm_DeleteSecurityContext(&Context->Ntlm);
    HeapFree(GetProcessHeap(), 0, Context->MechTypes);
    HeapFree(GetProcessHeap(), 0, Context);
}

static SECURITY_STATUS
AcceptNegTokenInit(PCredHandle phCredential, const BYTE *Input, ULONG InputLength, ULONG fContextReq,
                   ULONG TargetDataRep, PCtxtHandle phNewContext, PSecBufferDesc pOutput,
                   ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    NEGO_TOKEN Token;
    PNEGO_CONTEXT Context;
    BYTE NtlmOut[NEGO_MAX_NTLM_TOKEN], *Resp;
    ULONG NtlmOutLength = 0, RespLength, Attributes = 0;
    SECURITY_STATUS Status;

    if (!ParseNegTokenInit(Input, InputLength, &Token) || !Token.NtlmListed)
        return SEC_E_INVALID_TOKEN;

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
        return SEC_E_INSUFFICIENT_MEMORY;
    Context->MechTypes = HeapAlloc(GetProcessHeap(), 0, Token.MechTypesLength);
    if (!Context->MechTypes)
    {
        FreeNegoContext(Context);
        return SEC_E_INSUFFICIENT_MEMORY;
    }
    memcpy(Context->MechTypes, Token.MechTypes, Token.MechTypesLength);
    Context->MechTypesLength = Token.MechTypesLength;
    Context->State = NegoStateMechToken;

    if (Token.NtlmFirst && Token.MechToken)
    {
        Status = NtlmAcceptStep(Context, phCredential, Token.MechToken, Token.MechTokenLength, fContextReq,
                                TargetDataRep, NtlmOut, &NtlmOutLength, &Attributes, ptsExpiry);
        if (Status != SEC_I_CONTINUE_NEEDED)
        {
            FreeNegoContext(Context);
            return Status == SEC_E_OK ? SEC_E_INVALID_TOKEN : Status;
        }
        Context->State = NegoStateAuthenticate;
        Resp = BuildNegTokenResp(NEG_STATE_ACCEPT_INCOMPLETE, TRUE, NtlmOut, NtlmOutLength, NULL, 0, &RespLength);
    }
    else
    {
        Resp = BuildNegTokenResp(Token.NtlmFirst ? NEG_STATE_ACCEPT_INCOMPLETE : NEG_STATE_REQUEST_MIC, TRUE,
                                 NULL, 0, NULL, 0, &RespLength);
    }
    if (!Resp)
    {
        FreeNegoContext(Context);
        return SEC_E_INSUFFICIENT_MEMORY;
    }
    Status = WriteOutput(pOutput, fContextReq, Resp, RespLength);
    HeapFree(GetProcessHeap(), 0, Resp);
    if (Status != SEC_E_OK)
    {
        FreeNegoContext(Context);
        return Status;
    }
    if (pfContextAttr)
        *pfContextAttr = Attributes;
    phNewContext->dwLower = (ULONG_PTR)Context;
    phNewContext->dwUpper = 0;
    return SEC_I_CONTINUE_NEEDED;
}

static SECURITY_STATUS
AcceptNegTokenResp(PNEGO_CONTEXT Context, PCredHandle phCredential, const BYTE *Input, ULONG InputLength,
                   ULONG fContextReq, ULONG TargetDataRep, PSecBufferDesc pOutput, ULONG *pfContextAttr,
                   PTimeStamp ptsExpiry)
{
    NEGO_TOKEN Token;
    BYTE NtlmOut[NEGO_MAX_NTLM_TOKEN], Mic[16], *Resp;
    ULONG NtlmOutLength = 0, RespLength, Attributes = 0, NegState;
    SECURITY_STATUS Status, Result;

    if (!ParseNegTokenResp(Input, InputLength, &Token) || !Token.MechToken)
        return SEC_E_INVALID_TOKEN;

    if (Context->State == NegoStateMechToken)
    {
        if (Context->HaveNtlm)
            return SEC_E_INVALID_TOKEN;
        Status = NtlmAcceptStep(Context, phCredential, Token.MechToken, Token.MechTokenLength, fContextReq,
                                TargetDataRep, NtlmOut, &NtlmOutLength, &Attributes, ptsExpiry);
        if (Status != SEC_I_CONTINUE_NEEDED)
            return Status == SEC_E_OK ? SEC_E_INVALID_TOKEN : Status;
        Resp = BuildNegTokenResp(NEG_STATE_ACCEPT_INCOMPLETE, FALSE, NtlmOut, NtlmOutLength, NULL, 0, &RespLength);
        Result = SEC_I_CONTINUE_NEEDED;
        Context->State = NegoStateAuthenticate;
    }
    else if (Context->State == NegoStateAuthenticate)
    {
        Status = NtlmAcceptStep(Context, phCredential, Token.MechToken, Token.MechTokenLength, fContextReq,
                                TargetDataRep, NtlmOut, &NtlmOutLength, &Attributes, ptsExpiry);
        if (Status != SEC_E_OK)
        {
            if (pfContextAttr)
                *pfContextAttr = Attributes;
            Context->State = NegoStateDone;
            return Status == SEC_I_CONTINUE_NEEDED ? SEC_E_INVALID_TOKEN : Status;
        }
        Context->State = NegoStateDone;
        if (Token.MechListMic)
        {
            if (Token.MechListMicLength != sizeof(Mic))
                return SEC_E_MESSAGE_ALTERED;
            memcpy(Mic, Token.MechListMic, sizeof(Mic));
            Status = MechListMic(Context, TRUE, Mic, sizeof(Mic));
            if (Status != SEC_E_OK)
                return SEC_E_MESSAGE_ALTERED;
            NegState = NEG_STATE_ACCEPT_COMPLETED;
            Result = SEC_E_OK;
        }
        else
        {
            NegState = NEG_STATE_ACCEPT_INCOMPLETE;
            Result = SEC_I_CONTINUE_NEEDED;
        }
        Status = MechListMic(Context, FALSE, Mic, sizeof(Mic));
        if (Status != SEC_E_OK)
            return Status;
        Resp = BuildNegTokenResp(NegState, FALSE, NULL, 0, Mic, sizeof(Mic), &RespLength);
    }
    else
    {
        return SEC_E_INVALID_TOKEN;
    }

    if (!Resp)
        return SEC_E_INSUFFICIENT_MEMORY;
    Status = WriteOutput(pOutput, fContextReq, Resp, RespLength);
    HeapFree(GetProcessHeap(), 0, Resp);
    if (Status != SEC_E_OK)
        return Status;
    if (pfContextAttr)
        *pfContextAttr = Attributes;
    return Result;
}

SECURITY_STATUS
NegoAcceptSecurityContext(PCredHandle phCredential, PCtxtHandle phContext, PSecBufferDesc pInput,
                          ULONG fContextReq, ULONG TargetDataRep, PCtxtHandle phNewContext,
                          PSecBufferDesc pOutput, ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    PNEGO_CONTEXT Context;
    const BYTE *Input;
    ULONG InputLength;
    SECURITY_STATUS Status;

    if (!pInput || pInput->cBuffers < 1 || !pInput->pBuffers)
        return SEC_E_INCOMPLETE_MESSAGE;
    if (!phNewContext)
        return SEC_E_INVALID_HANDLE;
    Input = pInput->pBuffers[0].pvBuffer;
    InputLength = pInput->pBuffers[0].cbBuffer;
    if (!Input && InputLength)
        return SEC_E_INVALID_TOKEN;

    if (!phContext)
    {
        if (InputLength >= 8 && !memcmp(Input, "NTLMSSP", 8))
        {
            Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
            if (!Context)
                return SEC_E_INSUFFICIENT_MEMORY;
            Context->State = NegoStateRaw;
            Status = ntlm_AcceptSecurityContext(phCredential, NULL, pInput, fContextReq, TargetDataRep,
                                                &Context->Ntlm, pOutput, pfContextAttr, ptsExpiry);
            if (Status != SEC_E_OK && Status != SEC_I_CONTINUE_NEEDED)
            {
                HeapFree(GetProcessHeap(), 0, Context);
                return Status;
            }
            Context->HaveNtlm = TRUE;
            phNewContext->dwLower = (ULONG_PTR)Context;
            phNewContext->dwUpper = 0;
            return Status;
        }
        if (InputLength && Input[0] == 0x60)
            return AcceptNegTokenInit(phCredential, Input, InputLength, fContextReq, TargetDataRep,
                                      phNewContext, pOutput, pfContextAttr, ptsExpiry);
        return SEC_E_INVALID_TOKEN;
    }

    Context = (PNEGO_CONTEXT)phContext->dwLower;
    if (!Context)
        return SEC_E_INVALID_HANDLE;
    if (Context->State == NegoStateRaw)
    {
        Status = ntlm_AcceptSecurityContext(phCredential, &Context->Ntlm, pInput, fContextReq, TargetDataRep,
                                            &Context->Ntlm, pOutput, pfContextAttr, ptsExpiry);
    }
    else
    {
        Status = AcceptNegTokenResp(Context, phCredential, Input, InputLength, fContextReq, TargetDataRep,
                                    pOutput, pfContextAttr, ptsExpiry);
    }
    if (phNewContext != phContext)
        *phNewContext = *phContext;
    return Status;
}

SECURITY_STATUS
NegoInitializeSecurityContextW(PCredHandle phCredential, PCtxtHandle phContext, SEC_WCHAR *pszTargetName,
                               ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, PSecBufferDesc pInput,
                               ULONG Reserved2, PCtxtHandle phNewContext, PSecBufferDesc pOutput,
                               ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    PNEGO_CONTEXT Context;
    SECURITY_STATUS Status;

    if (!phNewContext)
        return SEC_E_INVALID_HANDLE;
    if (phContext)
    {
        Context = (PNEGO_CONTEXT)phContext->dwLower;
        if (!Context || !Context->HaveNtlm)
            return SEC_E_INVALID_HANDLE;
        Status = ntlm_InitializeSecurityContextW(phCredential, &Context->Ntlm, pszTargetName, fContextReq,
                                                 Reserved1, TargetDataRep, pInput, Reserved2, &Context->Ntlm,
                                                 pOutput, pfContextAttr, ptsExpiry);
        if (phNewContext != phContext)
            *phNewContext = *phContext;
        return Status;
    }

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
        return SEC_E_INSUFFICIENT_MEMORY;
    Context->State = NegoStateRaw;
    Status = ntlm_InitializeSecurityContextW(phCredential, NULL, pszTargetName, fContextReq, Reserved1,
                                             TargetDataRep, pInput, Reserved2, &Context->Ntlm, pOutput,
                                             pfContextAttr, ptsExpiry);
    if (Status != SEC_E_OK && Status != SEC_I_CONTINUE_NEEDED)
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return Status;
    }
    Context->HaveNtlm = TRUE;
    phNewContext->dwLower = (ULONG_PTR)Context;
    phNewContext->dwUpper = 0;
    return Status;
}

SECURITY_STATUS
NegoDeleteSecurityContext(PCtxtHandle phContext)
{
    PNEGO_CONTEXT Context;

    if (!phContext || !phContext->dwLower)
        return SEC_E_INVALID_HANDLE;
    Context = (PNEGO_CONTEXT)phContext->dwLower;
    phContext->dwLower = 0;
    phContext->dwUpper = 0;
    FreeNegoContext(Context);
    return SEC_E_OK;
}

PCtxtHandle
NegoNtlmContext(PCtxtHandle phContext)
{
    PNEGO_CONTEXT Context;

    if (!phContext || !phContext->dwLower)
        return NULL;
    Context = (PNEGO_CONTEXT)phContext->dwLower;
    return Context->HaveNtlm ? &Context->Ntlm : NULL;
}
