/*
 * PROJECT:     LiberNT Security Support Provider Interface
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTLM authentication messages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(ntlm);

#define NTLM_NEGOTIATE 1
#define NTLM_CHALLENGE 2
#define NTLM_AUTHENTICATE 3

#define NTLM_NEGOTIATE_SIZE 40
#define NTLM_CHALLENGE_SIZE 56
#define NTLM_AUTHENTICATE_SIZE 88
#define NTLM_AUTHENTICATE_MIC_OFFSET 72
#define NTLM_KEY_SIZE 16
#define NTLM_CHALLENGE_LENGTH 8
#define NTLM_V1_RESPONSE_SIZE 24
#define NTLM_V2_BLOB_HEADER_SIZE 28
#define NTLM_MAX_NAME 256

#define NTLM_AV_EOL 0
#define NTLM_AV_NB_COMPUTER_NAME 1
#define NTLM_AV_NB_DOMAIN_NAME 2
#define NTLM_AV_DNS_COMPUTER_NAME 3
#define NTLM_AV_DNS_DOMAIN_NAME 4
#define NTLM_AV_FLAGS 6
#define NTLM_AV_TIMESTAMP 7
#define NTLM_AV_FLAG_MIC_PRESENT 0x00000002

#define NTLM_CLIENT_FLAGS (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM | NTLMSSP_REQUEST_TARGET | \
                           NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_NTLM2 | \
                           NTLMSSP_NEGOTIATE_VERSION | NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_56)
#define NTLM_SERVER_FLAGS (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM | NTLMSSP_REQUEST_TARGET | \
                           NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL | NTLMSSP_NEGOTIATE_NTLM | \
                           NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_NTLM2 | NTLMSSP_NEGOTIATE_VERSION | \
                           NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_KEY_EXCHANGE | NTLMSSP_NEGOTIATE_56)

typedef struct
{
    unsigned int buf[4];
    unsigned int i[2];
    unsigned char in[64];
    unsigned char digest[16];
} NTLM_MD4_CTX;

VOID WINAPI MD4Init(NTLM_MD4_CTX *ctx);
VOID WINAPI MD4Update(NTLM_MD4_CTX *ctx, const unsigned char *buf, unsigned int len);
VOID WINAPI MD4Final(NTLM_MD4_CTX *ctx);
BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);

static const BYTE NtlmSignature[8] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', 0 };

typedef struct _NTLM_FIELD
{
    const BYTE *Data;
    ULONG Length;
} NTLM_FIELD;

static ULONG NtlmGetUlong(const BYTE *Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static USHORT NtlmGetUshort(const BYTE *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static void NtlmPutUlong(BYTE *Data, ULONG Value)
{
    Data[0] = (BYTE)Value;
    Data[1] = (BYTE)(Value >> 8);
    Data[2] = (BYTE)(Value >> 16);
    Data[3] = (BYTE)(Value >> 24);
}

static void NtlmPutUshort(BYTE *Data, USHORT Value)
{
    Data[0] = (BYTE)Value;
    Data[1] = (BYTE)(Value >> 8);
}

static void NtlmPutField(BYTE *Field, ULONG Length, ULONG Offset)
{
    NtlmPutUshort(Field, (USHORT)Length);
    NtlmPutUshort(Field + 2, (USHORT)Length);
    NtlmPutUlong(Field + 4, Offset);
}

static BOOL NtlmGetField(const BYTE *Message, ULONG MessageLength, ULONG FieldOffset, NTLM_FIELD *Field)
{
    ULONG Length, Offset;

    Field->Data = NULL;
    Field->Length = 0;
    if (FieldOffset + 8 > MessageLength)
        return FALSE;

    Length = NtlmGetUshort(Message + FieldOffset);
    Offset = NtlmGetUlong(Message + FieldOffset + 4);
    if (!Length)
        return TRUE;
    if (Offset > MessageLength || Length > MessageLength - Offset)
        return FALSE;

    Field->Data = Message + Offset;
    Field->Length = Length;
    return TRUE;
}

static void NtlmPutVersion(BYTE *Version)
{
    RTL_OSVERSIONINFOW Info;

    memset(Version, 0, 8);
    Info.dwOSVersionInfoSize = sizeof(Info);
    if (NT_SUCCESS(RtlGetVersion(&Info)))
    {
        Version[0] = (BYTE)Info.dwMajorVersion;
        Version[1] = (BYTE)Info.dwMinorVersion;
        NtlmPutUshort(Version + 2, (USHORT)Info.dwBuildNumber);
    }
    Version[7] = 0x0F;
}

static void NtlmHmacMd5(const BYTE *Key, ULONG KeyLength, const BYTE *First, ULONG FirstLength,
                        const BYTE *Second, ULONG SecondLength, BYTE *Digest)
{
    HMAC_MD5_CTX Context;

    HMACMD5Init(&Context, Key, KeyLength);
    if (FirstLength) HMACMD5Update(&Context, First, FirstLength);
    if (SecondLength) HMACMD5Update(&Context, Second, SecondLength);
    HMACMD5Final(&Context, Digest);
}

static void NtlmOwfV2(const WCHAR *Password, ULONG PasswordLength, const WCHAR *User, ULONG UserLength,
                      const WCHAR *Domain, ULONG DomainLength, BYTE *Key)
{
    WCHAR Identity[2 * NTLM_MAX_NAME];
    NTLM_MD4_CTX Md4;
    ULONG Index;

    MD4Init(&Md4);
    MD4Update(&Md4, (const unsigned char *)Password, PasswordLength * sizeof(WCHAR));
    MD4Final(&Md4);

    if (UserLength > NTLM_MAX_NAME) UserLength = NTLM_MAX_NAME;
    if (DomainLength > NTLM_MAX_NAME) DomainLength = NTLM_MAX_NAME;
    for (Index = 0; Index < UserLength; Index++)
        Identity[Index] = RtlUpcaseUnicodeChar(User[Index]);
    if (DomainLength)
        memcpy(Identity + UserLength, Domain, DomainLength * sizeof(WCHAR));

    NtlmHmacMd5(Md4.digest, NTLM_KEY_SIZE, (const BYTE *)Identity,
                (UserLength + DomainLength) * sizeof(WCHAR), NULL, 0, Key);
    RtlSecureZeroMemory(&Md4, sizeof(Md4));
}

static BOOL NtlmComputerName(WCHAR *Name, ULONG *Length)
{
    DWORD Size = MAX_COMPUTERNAME_LENGTH + 1;

    if (!GetComputerNameW(Name, &Size))
    {
        *Length = 0;
        return FALSE;
    }
    *Length = Size;
    return TRUE;
}

static ULONG NtlmPutString(BYTE *Buffer, const WCHAR *String, ULONG Length, BOOL Unicode)
{
    if (!Length)
        return 0;
    if (Unicode)
    {
        memcpy(Buffer, String, Length * sizeof(WCHAR));
        return Length * sizeof(WCHAR);
    }
    return WideCharToMultiByte(CP_OEMCP, 0, String, Length, (char *)Buffer, Length * 2, NULL, NULL);
}

static BOOL NtlmSetSessionKey(PNegoHelper Context, const BYTE *Key)
{
    if (!Context->session_key)
        Context->session_key = HeapAlloc(GetProcessHeap(), 0, NTLM_KEY_SIZE);
    if (!Context->session_key)
        return FALSE;
    memcpy(Context->session_key, Key, NTLM_KEY_SIZE);
    return TRUE;
}

static BOOL NtlmSaveMessage(BYTE **Saved, ULONG *SavedLength, const BYTE *Message, ULONG Length)
{
    HeapFree(GetProcessHeap(), 0, *Saved);
    *Saved = HeapAlloc(GetProcessHeap(), 0, Length ? Length : 1);
    if (!*Saved)
    {
        *SavedLength = 0;
        return FALSE;
    }
    memcpy(*Saved, Message, Length);
    *SavedLength = Length;
    return TRUE;
}

static const BYTE *NtlmFindAvPair(const BYTE *Pairs, ULONG Length, USHORT Id, ULONG *PairLength)
{
    ULONG Offset = 0;

    while (Offset + 4 <= Length)
    {
        USHORT Current = NtlmGetUshort(Pairs + Offset);
        ULONG Size = NtlmGetUshort(Pairs + Offset + 2);

        if (Offset + 4 + Size > Length)
            break;
        if (Current == Id)
        {
            *PairLength = Size;
            return Pairs + Offset + 4;
        }
        if (Current == NTLM_AV_EOL)
            break;
        Offset += 4 + Size;
    }
    return NULL;
}

static ULONG NtlmAvPairsLength(const BYTE *Pairs, ULONG Length)
{
    ULONG Offset = 0;

    while (Offset + 4 <= Length)
    {
        USHORT Current = NtlmGetUshort(Pairs + Offset);
        ULONG Size = NtlmGetUshort(Pairs + Offset + 2);

        if (Current == NTLM_AV_EOL || Offset + 4 + Size > Length)
            break;
        Offset += 4 + Size;
    }
    return Offset;
}

static void NtlmMic(const BYTE *Key, PNegoHelper Context, const BYTE *Authenticate, ULONG Length, BYTE *Mic)
{
    HMAC_MD5_CTX Hmac;

    HMACMD5Init(&Hmac, Key, NTLM_KEY_SIZE);
    HMACMD5Update(&Hmac, Context->negotiate_msg, Context->negotiate_len);
    HMACMD5Update(&Hmac, Context->challenge_msg, Context->challenge_len);
    HMACMD5Update(&Hmac, Authenticate, Length);
    HMACMD5Final(&Hmac, Mic);
}

SECURITY_STATUS NtlmBuildNegotiate(PNegoHelper Context, BYTE *Buffer, ULONG Size, ULONG *Length)
{
    ULONG Flags = NTLM_CLIENT_FLAGS | Context->want_flags;

    *Length = NTLM_NEGOTIATE_SIZE;
    if (Size < NTLM_NEGOTIATE_SIZE)
        return SEC_E_BUFFER_TOO_SMALL;

    memset(Buffer, 0, NTLM_NEGOTIATE_SIZE);
    memcpy(Buffer, NtlmSignature, sizeof(NtlmSignature));
    NtlmPutUlong(Buffer + 8, NTLM_NEGOTIATE);
    NtlmPutUlong(Buffer + 12, Flags);
    NtlmPutField(Buffer + 16, 0, NTLM_NEGOTIATE_SIZE);
    NtlmPutField(Buffer + 24, 0, NTLM_NEGOTIATE_SIZE);
    NtlmPutVersion(Buffer + 32);

    if (!NtlmSaveMessage(&Context->negotiate_msg, &Context->negotiate_len, Buffer, NTLM_NEGOTIATE_SIZE))
        return SEC_E_INSUFFICIENT_MEMORY;
    return SEC_E_OK;
}

SECURITY_STATUS NtlmBuildChallenge(PNegoHelper Context, const BYTE *Negotiate, ULONG NegotiateLength,
                                   BYTE *Buffer, ULONG Size, ULONG *Length)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1], DnsName[NTLM_MAX_NAME];
    ULONG ComputerLength, Flags, ClientFlags, NameBytes, InfoLength, Offset;
    DWORD DnsLength = ARRAY_SIZE(DnsName);
    FILETIME Time;
    BYTE *Info;

    *Length = 0;
    if (NegotiateLength < 16 || memcmp(Negotiate, NtlmSignature, sizeof(NtlmSignature)) ||
        NtlmGetUlong(Negotiate + 8) != NTLM_NEGOTIATE)
    {
        return SEC_E_INVALID_TOKEN;
    }

    ClientFlags = NtlmGetUlong(Negotiate + 12);
    if (!(ClientFlags & (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM)))
        return SEC_E_INVALID_TOKEN;

    Flags = ClientFlags & NTLM_SERVER_FLAGS;
    if (Flags & NTLMSSP_NEGOTIATE_UNICODE)
        Flags &= ~NTLMSSP_NEGOTIATE_OEM;
    Flags |= NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_TYPE_SERVER | NTLMSSP_NEGOTIATE_TARGET_INFO |
             NTLMSSP_NEGOTIATE_VERSION;

    NtlmComputerName(Computer, &ComputerLength);
    if (!GetComputerNameExW(ComputerNameDnsFullyQualified, DnsName, &DnsLength))
    {
        memcpy(DnsName, Computer, ComputerLength * sizeof(WCHAR));
        DnsLength = ComputerLength;
    }

    NameBytes = (Flags & NTLMSSP_NEGOTIATE_UNICODE) ? ComputerLength * sizeof(WCHAR) : ComputerLength * 2;
    InfoLength = 2 * (4 + ComputerLength * sizeof(WCHAR)) + 2 * (4 + DnsLength * sizeof(WCHAR)) +
                 (4 + sizeof(Time)) + 4;
    *Length = NTLM_CHALLENGE_SIZE + NameBytes + InfoLength;
    if (Size < *Length)
        return SEC_E_BUFFER_TOO_SMALL;

    if (!SystemFunction036(Context->server_challenge, sizeof(Context->server_challenge)))
        return SEC_E_INTERNAL_ERROR;

    memset(Buffer, 0, NTLM_CHALLENGE_SIZE);
    memcpy(Buffer, NtlmSignature, sizeof(NtlmSignature));
    NtlmPutUlong(Buffer + 8, NTLM_CHALLENGE);
    NameBytes = NtlmPutString(Buffer + NTLM_CHALLENGE_SIZE, Computer, ComputerLength,
                              (Flags & NTLMSSP_NEGOTIATE_UNICODE) != 0);
    NtlmPutField(Buffer + 12, NameBytes, NTLM_CHALLENGE_SIZE);
    NtlmPutUlong(Buffer + 20, Flags);
    memcpy(Buffer + 24, Context->server_challenge, sizeof(Context->server_challenge));
    NtlmPutVersion(Buffer + 48);

    Offset = NTLM_CHALLENGE_SIZE + NameBytes;
    Info = Buffer + Offset;
    NtlmPutUshort(Info, NTLM_AV_NB_DOMAIN_NAME);
    NtlmPutUshort(Info + 2, (USHORT)(ComputerLength * sizeof(WCHAR)));
    memcpy(Info + 4, Computer, ComputerLength * sizeof(WCHAR));
    Info += 4 + ComputerLength * sizeof(WCHAR);
    NtlmPutUshort(Info, NTLM_AV_NB_COMPUTER_NAME);
    NtlmPutUshort(Info + 2, (USHORT)(ComputerLength * sizeof(WCHAR)));
    memcpy(Info + 4, Computer, ComputerLength * sizeof(WCHAR));
    Info += 4 + ComputerLength * sizeof(WCHAR);
    NtlmPutUshort(Info, NTLM_AV_DNS_DOMAIN_NAME);
    NtlmPutUshort(Info + 2, (USHORT)(DnsLength * sizeof(WCHAR)));
    memcpy(Info + 4, DnsName, DnsLength * sizeof(WCHAR));
    Info += 4 + DnsLength * sizeof(WCHAR);
    NtlmPutUshort(Info, NTLM_AV_DNS_COMPUTER_NAME);
    NtlmPutUshort(Info + 2, (USHORT)(DnsLength * sizeof(WCHAR)));
    memcpy(Info + 4, DnsName, DnsLength * sizeof(WCHAR));
    Info += 4 + DnsLength * sizeof(WCHAR);
    GetSystemTimeAsFileTime(&Time);
    NtlmPutUshort(Info, NTLM_AV_TIMESTAMP);
    NtlmPutUshort(Info + 2, sizeof(Time));
    NtlmPutUlong(Info + 4, Time.dwLowDateTime);
    NtlmPutUlong(Info + 8, Time.dwHighDateTime);
    Info += 4 + sizeof(Time);
    NtlmPutUlong(Info, 0);
    Info += 4;
    InfoLength = (ULONG)(Info - (Buffer + Offset));
    NtlmPutField(Buffer + 40, InfoLength, Offset);
    *Length = Offset + InfoLength;

    Context->neg_flags = Flags;
    if (!NtlmSaveMessage(&Context->negotiate_msg, &Context->negotiate_len, Negotiate, NegotiateLength) ||
        !NtlmSaveMessage(&Context->challenge_msg, &Context->challenge_len, Buffer, *Length))
    {
        return SEC_E_INSUFFICIENT_MEMORY;
    }
    return SEC_E_OK;
}

SECURITY_STATUS NtlmBuildAuthenticate(PNegoHelper Context, const BYTE *Challenge, ULONG ChallengeLength,
                                      BYTE *Buffer, ULONG Size, ULONG *Length)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1];
    BYTE OwfKey[NTLM_KEY_SIZE], Proof[NTLM_KEY_SIZE], BaseKey[NTLM_KEY_SIZE], ExportedKey[NTLM_KEY_SIZE];
    BYTE EncryptedKey[NTLM_KEY_SIZE], ClientChallenge[NTLM_CHALLENGE_LENGTH], LmResponse[NTLM_V1_RESPONSE_SIZE];
    BYTE *NtResponse = NULL, *Blob, *Payload;
    const BYTE *Pairs, *Timestamp;
    ULONG ComputerLength, ServerFlags, Flags, PairsLength = 0, TimestampLength = 0, BlobLength;
    ULONG NtLength = 0, LmLength, KeyLength = 0, Offset, Written;
    NTLM_FIELD TargetInfo;
    HMAC_MD5_CTX Hmac;
    FILETIME Time;
    BOOL Unicode, UseMic = FALSE;
    SECURITY_STATUS Status = SEC_E_OK;

    *Length = 0;
    if (ChallengeLength < 32 || memcmp(Challenge, NtlmSignature, sizeof(NtlmSignature)) ||
        NtlmGetUlong(Challenge + 8) != NTLM_CHALLENGE)
    {
        return SEC_E_INVALID_TOKEN;
    }

    ServerFlags = NtlmGetUlong(Challenge + 20);
    memcpy(Context->server_challenge, Challenge + 24, NTLM_CHALLENGE_LENGTH);
    TargetInfo.Data = NULL;
    TargetInfo.Length = 0;
    if ((ServerFlags & NTLMSSP_NEGOTIATE_TARGET_INFO) && ChallengeLength >= 48 &&
        !NtlmGetField(Challenge, ChallengeLength, 40, &TargetInfo))
    {
        return SEC_E_INVALID_TOKEN;
    }

    Flags = ServerFlags & (NTLM_CLIENT_FLAGS | Context->want_flags | NTLMSSP_NEGOTIATE_TARGET_INFO);
    if (Flags & NTLMSSP_NEGOTIATE_UNICODE)
        Flags &= ~NTLMSSP_NEGOTIATE_OEM;
    Unicode = (Flags & NTLMSSP_NEGOTIATE_UNICODE) != 0;
    NtlmComputerName(Computer, &ComputerLength);

    if (!NtlmSaveMessage(&Context->challenge_msg, &Context->challenge_len, Challenge, ChallengeLength))
        return SEC_E_INSUFFICIENT_MEMORY;

    if (!Context->have_credentials)
    {
        Flags |= NTLMSSP_NEGOTIATE_ANONYMOUS;
        LmResponse[0] = 0;
        LmLength = 1;
        memset(BaseKey, 0, sizeof(BaseKey));
    }
    else
    {
        if (TargetInfo.Length)
        {
            Pairs = TargetInfo.Data;
            PairsLength = NtlmAvPairsLength(Pairs, TargetInfo.Length);
            Timestamp = NtlmFindAvPair(Pairs, TargetInfo.Length, NTLM_AV_TIMESTAMP, &TimestampLength);
        }
        else
        {
            Pairs = NULL;
            Timestamp = NULL;
        }
        UseMic = (Timestamp && TimestampLength == sizeof(Time));

        BlobLength = NTLM_V2_BLOB_HEADER_SIZE + PairsLength + (UseMic ? 8 : 0) + 4 + 4;
        NtLength = NTLM_KEY_SIZE + BlobLength;
        NtResponse = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NtLength);
        if (!NtResponse)
            return SEC_E_INSUFFICIENT_MEMORY;

        if (!SystemFunction036(ClientChallenge, sizeof(ClientChallenge)))
        {
            Status = SEC_E_INTERNAL_ERROR;
            goto Done;
        }

        Blob = NtResponse + NTLM_KEY_SIZE;
        Blob[0] = 1;
        Blob[1] = 1;
        if (UseMic)
        {
            memcpy(Blob + 8, Timestamp, sizeof(Time));
        }
        else
        {
            GetSystemTimeAsFileTime(&Time);
            NtlmPutUlong(Blob + 8, Time.dwLowDateTime);
            NtlmPutUlong(Blob + 12, Time.dwHighDateTime);
        }
        memcpy(Blob + 16, ClientChallenge, sizeof(ClientChallenge));
        Payload = Blob + NTLM_V2_BLOB_HEADER_SIZE;
        if (PairsLength)
        {
            memcpy(Payload, Pairs, PairsLength);
            Payload += PairsLength;
        }
        if (UseMic)
        {
            NtlmPutUshort(Payload, NTLM_AV_FLAGS);
            NtlmPutUshort(Payload + 2, 4);
            NtlmPutUlong(Payload + 4, NTLM_AV_FLAG_MIC_PRESENT);
            Payload += 8;
        }

        NtlmOwfV2(Context->password, Context->password_len, Context->user, Context->user_len,
                  Context->domain, Context->domain_len, OwfKey);
        NtlmHmacMd5(OwfKey, sizeof(OwfKey), Context->server_challenge, NTLM_CHALLENGE_LENGTH,
                    Blob, BlobLength, Proof);
        memcpy(NtResponse, Proof, sizeof(Proof));
        NtlmHmacMd5(OwfKey, sizeof(OwfKey), Proof, sizeof(Proof), NULL, 0, BaseKey);

        if (UseMic)
        {
            memset(LmResponse, 0, sizeof(LmResponse));
        }
        else
        {
            NtlmHmacMd5(OwfKey, sizeof(OwfKey), Context->server_challenge, NTLM_CHALLENGE_LENGTH,
                        ClientChallenge, sizeof(ClientChallenge), LmResponse);
            memcpy(LmResponse + NTLM_KEY_SIZE, ClientChallenge, sizeof(ClientChallenge));
        }
        LmLength = NTLM_V1_RESPONSE_SIZE;
        RtlSecureZeroMemory(OwfKey, sizeof(OwfKey));
    }

    if (Flags & NTLMSSP_NEGOTIATE_KEY_EXCHANGE)
    {
        arc4_info Rc4;

        if (!SystemFunction036(ExportedKey, sizeof(ExportedKey)))
        {
            Status = SEC_E_INTERNAL_ERROR;
            goto Done;
        }
        memcpy(EncryptedKey, ExportedKey, sizeof(EncryptedKey));
        SECUR32_arc4Init(&Rc4, BaseKey, sizeof(BaseKey));
        SECUR32_arc4Process(&Rc4, EncryptedKey, sizeof(EncryptedKey));
        KeyLength = sizeof(EncryptedKey);
    }
    else
    {
        memcpy(ExportedKey, BaseKey, sizeof(ExportedKey));
    }

    *Length = NTLM_AUTHENTICATE_SIZE + (Context->domain_len + Context->user_len + ComputerLength) * sizeof(WCHAR) +
              LmLength + NtLength + KeyLength;
    if (Size < *Length)
    {
        Status = SEC_E_BUFFER_TOO_SMALL;
        goto Done;
    }

    memset(Buffer, 0, NTLM_AUTHENTICATE_SIZE);
    memcpy(Buffer, NtlmSignature, sizeof(NtlmSignature));
    NtlmPutUlong(Buffer + 8, NTLM_AUTHENTICATE);
    NtlmPutUlong(Buffer + 60, Flags);
    NtlmPutVersion(Buffer + 64);

    Offset = NTLM_AUTHENTICATE_SIZE;
    Written = Context->have_credentials ?
              NtlmPutString(Buffer + Offset, Context->domain, Context->domain_len, Unicode) : 0;
    NtlmPutField(Buffer + 28, Written, Offset);
    Offset += Written;
    Written = Context->have_credentials ?
              NtlmPutString(Buffer + Offset, Context->user, Context->user_len, Unicode) : 0;
    NtlmPutField(Buffer + 36, Written, Offset);
    Offset += Written;
    Written = NtlmPutString(Buffer + Offset, Computer, ComputerLength, Unicode);
    NtlmPutField(Buffer + 44, Written, Offset);
    Offset += Written;
    memcpy(Buffer + Offset, LmResponse, LmLength);
    NtlmPutField(Buffer + 12, LmLength, Offset);
    Offset += LmLength;
    if (NtLength)
        memcpy(Buffer + Offset, NtResponse, NtLength);
    NtlmPutField(Buffer + 20, NtLength, Offset);
    Offset += NtLength;
    if (KeyLength)
        memcpy(Buffer + Offset, EncryptedKey, KeyLength);
    NtlmPutField(Buffer + 52, KeyLength, Offset);
    Offset += KeyLength;
    *Length = Offset;

    if (UseMic && Context->negotiate_msg)
    {
        HMACMD5Init(&Hmac, ExportedKey, sizeof(ExportedKey));
        HMACMD5Update(&Hmac, Context->negotiate_msg, Context->negotiate_len);
        HMACMD5Update(&Hmac, Context->challenge_msg, Context->challenge_len);
        HMACMD5Update(&Hmac, Buffer, Offset);
        HMACMD5Final(&Hmac, Buffer + NTLM_AUTHENTICATE_MIC_OFFSET);
    }

    Context->neg_flags = Flags;
    if (!NtlmSetSessionKey(Context, ExportedKey))
        Status = SEC_E_INSUFFICIENT_MEMORY;

Done:
    HeapFree(GetProcessHeap(), 0, NtResponse);
    RtlSecureZeroMemory(BaseKey, sizeof(BaseKey));
    RtlSecureZeroMemory(ExportedKey, sizeof(ExportedKey));
    return Status;
}

static ULONG NtlmGetString(const NTLM_FIELD *Field, BOOL Unicode, WCHAR *String, ULONG MaxLength)
{
    ULONG Length;

    if (!Field->Length)
        return 0;
    if (Unicode)
    {
        Length = min(Field->Length / sizeof(WCHAR), MaxLength);
        memcpy(String, Field->Data, Length * sizeof(WCHAR));
        return Length;
    }
    return MultiByteToWideChar(CP_OEMCP, 0, (const char *)Field->Data, Field->Length, String, MaxLength);
}

static SECURITY_STATUS NtlmLogonUser(PNegoHelper Context, const WCHAR *Domain, ULONG DomainLength,
                                     const WCHAR *User, ULONG UserLength, const WCHAR *Workstation,
                                     ULONG WorkstationLength, const NTLM_FIELD *NtResponse,
                                     const NTLM_FIELD *LmResponse, BYTE *SessionKey)
{
    static const char PackageNameA[] = MSV1_0_PACKAGE_NAME;
    static const char OriginA[] = "NTLM";
    LSA_STRING PackageName, Origin;
    HANDLE Lsa = NULL, Token = NULL;
    ULONG Package, RequestLength, ProfileLength = 0;
    PMSV1_0_LM20_LOGON Request = NULL;
    PMSV1_0_LM20_LOGON_PROFILE Profile = NULL;
    TOKEN_SOURCE Source;
    QUOTA_LIMITS Quotas;
    NTSTATUS Status, SubStatus = STATUS_SUCCESS;
    LUID LogonId;
    BYTE *Data;

    Status = LsaConnectUntrusted(&Lsa);
    if (!NT_SUCCESS(Status))
        return SEC_E_INTERNAL_ERROR;

    RtlInitAnsiString((PANSI_STRING)&PackageName, PackageNameA);
    Status = LsaLookupAuthenticationPackage(Lsa, &PackageName, &Package);
    if (!NT_SUCCESS(Status))
        goto Done;

    RequestLength = sizeof(*Request) + (DomainLength + UserLength + WorkstationLength) * sizeof(WCHAR) +
                    NtResponse->Length + LmResponse->Length;
    Request = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequestLength);
    if (!Request)
    {
        Status = STATUS_NO_MEMORY;
        goto Done;
    }

    Request->MessageType = MsV1_0Lm20Logon;
    Request->ParameterControl = MSV1_0_RETURN_PROFILE_PATH;
    memcpy(Request->ChallengeToClient, Context->server_challenge, MSV1_0_CHALLENGE_LENGTH);
    Data = (BYTE *)(Request + 1);
    Request->LogonDomainName.Buffer = (PWSTR)Data;
    Request->LogonDomainName.Length = Request->LogonDomainName.MaximumLength = (USHORT)(DomainLength * sizeof(WCHAR));
    memcpy(Data, Domain, DomainLength * sizeof(WCHAR));
    Data += DomainLength * sizeof(WCHAR);
    Request->UserName.Buffer = (PWSTR)Data;
    Request->UserName.Length = Request->UserName.MaximumLength = (USHORT)(UserLength * sizeof(WCHAR));
    memcpy(Data, User, UserLength * sizeof(WCHAR));
    Data += UserLength * sizeof(WCHAR);
    Request->Workstation.Buffer = (PWSTR)Data;
    Request->Workstation.Length = Request->Workstation.MaximumLength = (USHORT)(WorkstationLength * sizeof(WCHAR));
    memcpy(Data, Workstation, WorkstationLength * sizeof(WCHAR));
    Data += WorkstationLength * sizeof(WCHAR);
    Request->CaseSensitiveChallengeResponse.Buffer = (PCHAR)Data;
    Request->CaseSensitiveChallengeResponse.Length =
        Request->CaseSensitiveChallengeResponse.MaximumLength = (USHORT)NtResponse->Length;
    if (NtResponse->Length)
        memcpy(Data, NtResponse->Data, NtResponse->Length);
    Data += NtResponse->Length;
    Request->CaseInsensitiveChallengeResponse.Buffer = (PCHAR)Data;
    Request->CaseInsensitiveChallengeResponse.Length =
        Request->CaseInsensitiveChallengeResponse.MaximumLength = (USHORT)LmResponse->Length;
    if (LmResponse->Length)
        memcpy(Data, LmResponse->Data, LmResponse->Length);

    RtlInitAnsiString((PANSI_STRING)&Origin, OriginA);
    memset(&Source, 0, sizeof(Source));
    memcpy(Source.SourceName, "NtLmSsp ", sizeof(Source.SourceName));
    AllocateLocallyUniqueId(&Source.SourceIdentifier);

    Status = LsaLogonUser(Lsa, &Origin, Network, Package, Request, RequestLength, NULL, &Source,
                          (PVOID *)&Profile, &ProfileLength, &LogonId, &Token, &Quotas, &SubStatus);
    if (NT_SUCCESS(Status))
    {
        if (Profile && ProfileLength >= sizeof(*Profile))
            memcpy(SessionKey, Profile->UserSessionKey, MSV1_0_USER_SESSION_KEY_LENGTH);
        else
            memset(SessionKey, 0, MSV1_0_USER_SESSION_KEY_LENGTH);
        Context->token = Token;
    }

Done:
    if (Profile) LsaFreeReturnBuffer(Profile);
    HeapFree(GetProcessHeap(), 0, Request);
    LsaDeregisterLogonProcess(Lsa);
    if (NT_SUCCESS(Status))
        return SEC_E_OK;
    TRACE("logon failed %08x/%08x\n", (unsigned int)Status, (unsigned int)SubStatus);
    return SEC_E_LOGON_DENIED;
}

SECURITY_STATUS NtlmAcceptAuthenticate(PNegoHelper Context, const BYTE *Authenticate, ULONG AuthenticateLength)
{
    WCHAR Domain[NTLM_MAX_NAME], User[NTLM_MAX_NAME], Workstation[NTLM_MAX_NAME];
    BYTE BaseKey[NTLM_KEY_SIZE], ExportedKey[NTLM_KEY_SIZE], Mic[NTLM_KEY_SIZE];
    NTLM_FIELD LmResponse, NtResponse, DomainField, UserField, WorkstationField, KeyField;
    ULONG Flags, DomainLength, UserLength, WorkstationLength, FlagsLength = 0, MinOffset;
    const BYTE *AvFlags;
    BYTE *Copy;
    BOOL Unicode;
    SECURITY_STATUS Status;

    if (AuthenticateLength < 64 || memcmp(Authenticate, NtlmSignature, sizeof(NtlmSignature)) ||
        NtlmGetUlong(Authenticate + 8) != NTLM_AUTHENTICATE)
    {
        return SEC_E_INVALID_TOKEN;
    }

    if (!NtlmGetField(Authenticate, AuthenticateLength, 12, &LmResponse) ||
        !NtlmGetField(Authenticate, AuthenticateLength, 20, &NtResponse) ||
        !NtlmGetField(Authenticate, AuthenticateLength, 28, &DomainField) ||
        !NtlmGetField(Authenticate, AuthenticateLength, 36, &UserField) ||
        !NtlmGetField(Authenticate, AuthenticateLength, 44, &WorkstationField) ||
        !NtlmGetField(Authenticate, AuthenticateLength, 52, &KeyField))
    {
        return SEC_E_INVALID_TOKEN;
    }

    Flags = NtlmGetUlong(Authenticate + 60) & (Context->neg_flags | NTLMSSP_NEGOTIATE_ANONYMOUS);
    Unicode = (Flags & NTLMSSP_NEGOTIATE_UNICODE) != 0;
    DomainLength = NtlmGetString(&DomainField, Unicode, Domain, ARRAY_SIZE(Domain));
    UserLength = NtlmGetString(&UserField, Unicode, User, ARRAY_SIZE(User));
    WorkstationLength = NtlmGetString(&WorkstationField, Unicode, Workstation, ARRAY_SIZE(Workstation));

    if (!UserLength && !NtResponse.Length && LmResponse.Length <= 1)
    {
        Flags |= NTLMSSP_NEGOTIATE_ANONYMOUS;
        memset(BaseKey, 0, sizeof(BaseKey));
    }
    else
    {
        Status = NtlmLogonUser(Context, Domain, DomainLength, User, UserLength, Workstation, WorkstationLength,
                               &NtResponse, &LmResponse, BaseKey);
        if (Status != SEC_E_OK)
            return Status;
    }

    if ((Flags & NTLMSSP_NEGOTIATE_KEY_EXCHANGE) && KeyField.Length == NTLM_KEY_SIZE)
    {
        arc4_info Rc4;

        memcpy(ExportedKey, KeyField.Data, sizeof(ExportedKey));
        SECUR32_arc4Init(&Rc4, BaseKey, sizeof(BaseKey));
        SECUR32_arc4Process(&Rc4, ExportedKey, sizeof(ExportedKey));
    }
    else
    {
        memcpy(ExportedKey, BaseKey, sizeof(ExportedKey));
    }

    AvFlags = NULL;
    if (NtResponse.Length > NTLM_KEY_SIZE + NTLM_V2_BLOB_HEADER_SIZE)
    {
        AvFlags = NtlmFindAvPair(NtResponse.Data + NTLM_KEY_SIZE + NTLM_V2_BLOB_HEADER_SIZE,
                                 NtResponse.Length - NTLM_KEY_SIZE - NTLM_V2_BLOB_HEADER_SIZE,
                                 NTLM_AV_FLAGS, &FlagsLength);
    }

    MinOffset = AuthenticateLength;
    if (LmResponse.Length) MinOffset = min(MinOffset, (ULONG)(LmResponse.Data - Authenticate));
    if (NtResponse.Length) MinOffset = min(MinOffset, (ULONG)(NtResponse.Data - Authenticate));
    if (DomainField.Length) MinOffset = min(MinOffset, (ULONG)(DomainField.Data - Authenticate));
    if (UserField.Length) MinOffset = min(MinOffset, (ULONG)(UserField.Data - Authenticate));
    if (WorkstationField.Length) MinOffset = min(MinOffset, (ULONG)(WorkstationField.Data - Authenticate));
    if (KeyField.Length) MinOffset = min(MinOffset, (ULONG)(KeyField.Data - Authenticate));

    if (AvFlags && FlagsLength == 4 && (NtlmGetUlong(AvFlags) & NTLM_AV_FLAG_MIC_PRESENT))
    {
        if (MinOffset < NTLM_AUTHENTICATE_SIZE || !Context->negotiate_msg || !Context->challenge_msg)
            return SEC_E_INVALID_TOKEN;

        Copy = HeapAlloc(GetProcessHeap(), 0, AuthenticateLength);
        if (!Copy)
            return SEC_E_INSUFFICIENT_MEMORY;
        memcpy(Copy, Authenticate, AuthenticateLength);
        memset(Copy + NTLM_AUTHENTICATE_MIC_OFFSET, 0, NTLM_KEY_SIZE);
        NtlmMic(ExportedKey, Context, Copy, AuthenticateLength, Mic);
        HeapFree(GetProcessHeap(), 0, Copy);
        if (memcmp(Mic, Authenticate + NTLM_AUTHENTICATE_MIC_OFFSET, sizeof(Mic)))
            return SEC_E_MESSAGE_ALTERED;
    }

    Context->neg_flags = Flags;
    if (!NtlmSetSessionKey(Context, ExportedKey))
        return SEC_E_INSUFFICIENT_MEMORY;
    RtlSecureZeroMemory(BaseKey, sizeof(BaseKey));
    RtlSecureZeroMemory(ExportedKey, sizeof(ExportedKey));
    return SEC_E_OK;
}

void NtlmFreeContext(PNegoHelper Context)
{
    if (!Context)
        return;

    if (Context->password)
        RtlSecureZeroMemory(Context->password, Context->password_len * sizeof(WCHAR));
    if (Context->session_key)
        RtlSecureZeroMemory(Context->session_key, NTLM_KEY_SIZE);
    if (Context->token)
        CloseHandle(Context->token);
    HeapFree(GetProcessHeap(), 0, Context->user);
    HeapFree(GetProcessHeap(), 0, Context->domain);
    HeapFree(GetProcessHeap(), 0, Context->password);
    HeapFree(GetProcessHeap(), 0, Context->negotiate_msg);
    HeapFree(GetProcessHeap(), 0, Context->challenge_msg);
    HeapFree(GetProcessHeap(), 0, Context->session_key);
    HeapFree(GetProcessHeap(), 0, Context);
}
