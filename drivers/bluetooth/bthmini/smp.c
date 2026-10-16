/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth Low Energy Security Manager (pairing, bonding and link encryption)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"
#include <bcrypt.h>
#include <rsna_aes.h>
#include <rsna_cmac.h>
#include <rsna_p256.h>

#define NDEBUG
#include <debug.h>

#define LE_CID_SMP                      0x0006
#define HCI_LE_ENABLE_ENCRYPTION        HCI_OP(0x08, 0x0019)

#define SMP_PAIRING_REQUEST             0x01
#define SMP_PAIRING_RESPONSE            0x02
#define SMP_PAIRING_CONFIRM             0x03
#define SMP_PAIRING_RANDOM              0x04
#define SMP_PAIRING_FAILED              0x05
#define SMP_ENCRYPTION_INFORMATION      0x06
#define SMP_CENTRAL_IDENTIFICATION      0x07
#define SMP_IDENTITY_INFORMATION        0x08
#define SMP_IDENTITY_ADDRESS            0x09
#define SMP_SIGNING_INFORMATION         0x0A
#define SMP_SECURITY_REQUEST            0x0B
#define SMP_PAIRING_PUBLIC_KEY          0x0C
#define SMP_PAIRING_DHKEY_CHECK         0x0D
#define SMP_KEYPRESS_NOTIFICATION       0x0E

#define SMP_IO_DISPLAY_ONLY             0x00
#define SMP_IO_DISPLAY_YES_NO           0x01
#define SMP_IO_KEYBOARD_ONLY            0x02
#define SMP_IO_NO_INPUT_NO_OUTPUT       0x03
#define SMP_IO_KEYBOARD_DISPLAY         0x04
#define SMP_AUTH_BONDING                0x01
#define SMP_AUTH_MITM                   0x04
#define SMP_AUTH_SC                     0x08
#define SMP_DIST_ENC                    0x01
#define SMP_DIST_ID                     0x02
#define SMP_MAX_KEY_SIZE                16
#define SMP_MIN_KEY_SIZE                7
#define SMP_TIMEOUT_MS                  30000

#define SMP_ERROR_PASSKEY_ENTRY_FAILED  0x01
#define SMP_ERROR_CONFIRM_FAILED        0x04
#define SMP_ERROR_PAIRING_NOT_SUPPORTED 0x05
#define SMP_ERROR_KEY_SIZE              0x06
#define SMP_ERROR_UNSPECIFIED           0x08
#define SMP_ERROR_INVALID_PARAMETERS    0x0A
#define SMP_ERROR_DHKEY_CHECK_FAILED    0x0B
#define SMP_ERROR_NUMERIC_COMPARISON    0x0C

#define SMP_METHOD_JUST_WORKS           0
#define SMP_METHOD_NUMERIC_COMPARISON   1
#define SMP_METHOD_PASSKEY_DISPLAY      2
#define SMP_METHOD_PASSKEY_INPUT        3
#define SMP_PASSKEY_ROUNDS              20
#define SMP_PASSKEY_LIMIT               1000000

typedef enum _BTHMINI_SMP_STATE
{
    SmpWaitLink,
    SmpWaitResponse,
    SmpWaitPublicKey,
    SmpWaitConfirm,
    SmpWaitRandom,
    SmpWaitDhKeyCheck,
    SmpWaitUser,
    SmpWaitEncryption,
    SmpWaitKeys,
    SmpEncrypting
} BTHMINI_SMP_STATE;

typedef struct _BTHMINI_SMP
{
    BTHMINI_SMP_STATE State;
    WDFREQUEST Request;
    BOOLEAN Pairing;
    BOOLEAN SecureConnections;
    UCHAR Preq[7];
    UCHAR Pres[7];
    UCHAR LocalRandom[16];
    UCHAR RemoteRandom[16];
    UCHAR RemoteConfirm[16];
    UCHAR PrivateKey[32];
    UCHAR LocalPublic[64];
    UCHAR RemotePublic[64];
    UCHAR DhKey[32];
    UCHAR MacKey[16];
    UCHAR Ltk[16];
    BOOLEAN HaveLtk;
    UCHAR KeySize;
    UCHAR ExpectedKeys;
    USHORT Ediv;
    ULONGLONG Rand;
    UCHAR Irk[16];
    BOOLEAN HaveIrk;
    BOOLEAN HaveIdentity;
    UCHAR IdentityType;
    BTH_ADDR IdentityAddress;
    UCHAR Method;
    UCHAR Round;
    ULONG Passkey;
    UCHAR Tk[16];
    WDFREQUEST PromptResponse;
    ULONGLONG Deadline;
} BTHMINI_SMP, *PBTHMINI_SMP;

static const UCHAR BthP256Gx[32] =
{
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
    0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96
};

static const UCHAR BthP256Gy[32] =
{
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
    0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5
};

static const UCHAR BthSmpSalt[16] =
{
    0x6C, 0x88, 0x83, 0x91, 0xAA, 0xF5, 0xA5, 0x38, 0x60, 0x37, 0x0B, 0xDB, 0x5A, 0x60, 0x83, 0xBE
};

static
ULONGLONG
BthSmpNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
BthSmpReverse(
    _Out_writes_bytes_(Length) PUCHAR Destination,
    _In_reads_bytes_(Length) const UCHAR *Source,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Destination[Index] = Source[Length - 1 - Index];
}

static
VOID
BthSmpEncrypt(
    _In_reads_(16) const UCHAR *KeyLe,
    _In_reads_(16) const UCHAR *InputLe,
    _Out_writes_(16) PUCHAR OutputLe)
{
    RSNA_AES_KEY Key;
    UCHAR KeyBe[16], Block[16];

    BthSmpReverse(KeyBe, KeyLe, 16);
    BthSmpReverse(Block, InputLe, 16);
    RsnaAesSetEncryptKey(&Key, KeyBe, 128);
    RsnaAesEncrypt(&Key, Block, Block);
    BthSmpReverse(OutputLe, Block, 16);
    RtlSecureZeroMemory(&Key, sizeof(Key));
    RtlSecureZeroMemory(KeyBe, sizeof(KeyBe));
}

static
VOID
BthSmpC1(
    _In_reads_(16) const UCHAR *Key,
    _In_reads_(16) const UCHAR *Random,
    _In_ PBTHMINI_SMP Smp,
    _In_ UCHAR InitiatorType,
    _In_reads_(6) const UCHAR *Initiator,
    _In_ UCHAR ResponderType,
    _In_reads_(6) const UCHAR *Responder,
    _Out_writes_(16) PUCHAR Output)
{
    UCHAR P1[16], P2[16], Value[16];
    ULONG Index;

    P1[0] = InitiatorType;
    P1[1] = ResponderType;
    RtlCopyMemory(P1 + 2, Smp->Preq, 7);
    RtlCopyMemory(P1 + 9, Smp->Pres, 7);
    RtlCopyMemory(P2, Responder, 6);
    RtlCopyMemory(P2 + 6, Initiator, 6);
    RtlZeroMemory(P2 + 12, 4);
    for (Index = 0; Index < 16; Index++)
        Value[Index] = Random[Index] ^ P1[Index];
    BthSmpEncrypt(Key, Value, Value);
    for (Index = 0; Index < 16; Index++)
        Value[Index] ^= P2[Index];
    BthSmpEncrypt(Key, Value, Output);
}

static
VOID
BthSmpS1(
    _In_reads_(16) const UCHAR *Key,
    _In_reads_(16) const UCHAR *Responder,
    _In_reads_(16) const UCHAR *Initiator,
    _Out_writes_(16) PUCHAR Output)
{
    UCHAR Value[16];

    RtlCopyMemory(Value, Initiator, 8);
    RtlCopyMemory(Value + 8, Responder, 8);
    BthSmpEncrypt(Key, Value, Output);
}

static
VOID
BthSmpCmac(
    _In_reads_(16) const UCHAR *KeyLe,
    _In_reads_bytes_(Length) const UCHAR *MessageBe,
    _In_ ULONG Length,
    _Out_writes_(16) PUCHAR OutputLe)
{
    UCHAR KeyBe[16], Mac[16];

    BthSmpReverse(KeyBe, KeyLe, 16);
    RsnaAesCmac(KeyBe, MessageBe, Length, Mac);
    BthSmpReverse(OutputLe, Mac, 16);
    RtlSecureZeroMemory(KeyBe, sizeof(KeyBe));
}

static
VOID
BthSmpF4(
    _In_reads_(32) const UCHAR *U,
    _In_reads_(32) const UCHAR *V,
    _In_reads_(16) const UCHAR *X,
    _In_ UCHAR Z,
    _Out_writes_(16) PUCHAR Output)
{
    UCHAR Message[65];

    BthSmpReverse(Message, U, 32);
    BthSmpReverse(Message + 32, V, 32);
    Message[64] = Z;
    BthSmpCmac(X, Message, sizeof(Message), Output);
}

static
VOID
BthSmpAddress(
    _Out_writes_(7) PUCHAR Output,
    _In_ UCHAR Type,
    _In_reads_(6) const UCHAR *Address)
{
    Output[0] = Type;
    BthSmpReverse(Output + 1, Address, 6);
}

static
VOID
BthSmpF5(
    _In_reads_(32) const UCHAR *W,
    _In_reads_(16) const UCHAR *N1,
    _In_reads_(16) const UCHAR *N2,
    _In_reads_(7) const UCHAR *A1,
    _In_reads_(7) const UCHAR *A2,
    _Out_writes_(16) PUCHAR MacKey,
    _Out_writes_(16) PUCHAR Ltk)
{
    UCHAR Message[53], Salt[16], WBe[32], T[16], TBe[16], Mac[16];

    BthSmpReverse(Salt, BthSmpSalt, 16);
    BthSmpReverse(WBe, W, 32);
    BthSmpCmac(Salt, WBe, sizeof(WBe), T);
    BthSmpReverse(TBe, T, 16);

    Message[0] = 0;
    Message[1] = 0x62;
    Message[2] = 0x74;
    Message[3] = 0x6C;
    Message[4] = 0x65;
    BthSmpReverse(Message + 5, N1, 16);
    BthSmpReverse(Message + 21, N2, 16);
    RtlCopyMemory(Message + 37, A1, 7);
    RtlCopyMemory(Message + 44, A2, 7);
    Message[51] = 0x01;
    Message[52] = 0x00;
    RsnaAesCmac(TBe, Message, sizeof(Message), Mac);
    BthSmpReverse(MacKey, Mac, 16);
    Message[0] = 1;
    RsnaAesCmac(TBe, Message, sizeof(Message), Mac);
    BthSmpReverse(Ltk, Mac, 16);
    RtlSecureZeroMemory(T, sizeof(T));
    RtlSecureZeroMemory(TBe, sizeof(TBe));
    RtlSecureZeroMemory(WBe, sizeof(WBe));
}

static
VOID
BthSmpF6(
    _In_reads_(16) const UCHAR *W,
    _In_reads_(16) const UCHAR *N1,
    _In_reads_(16) const UCHAR *N2,
    _In_reads_(16) const UCHAR *R,
    _In_reads_(3) const UCHAR *IoCapability,
    _In_reads_(7) const UCHAR *A1,
    _In_reads_(7) const UCHAR *A2,
    _Out_writes_(16) PUCHAR Output)
{
    UCHAR Message[65];

    BthSmpReverse(Message, N1, 16);
    BthSmpReverse(Message + 16, N2, 16);
    BthSmpReverse(Message + 32, R, 16);
    RtlCopyMemory(Message + 48, IoCapability, 3);
    RtlCopyMemory(Message + 51, A1, 7);
    RtlCopyMemory(Message + 58, A2, 7);
    BthSmpCmac(W, Message, sizeof(Message), Output);
}

static
ULONG
BthSmpG2(
    _In_reads_(32) const UCHAR *U,
    _In_reads_(32) const UCHAR *V,
    _In_reads_(16) const UCHAR *X,
    _In_reads_(16) const UCHAR *Y)
{
    UCHAR Message[80], Mac[16];

    BthSmpReverse(Message, U, 32);
    BthSmpReverse(Message + 32, V, 32);
    BthSmpReverse(Message + 64, Y, 16);
    BthSmpCmac(X, Message, sizeof(Message), Mac);
    return (ULONG)Mac[0] | ((ULONG)Mac[1] << 8) | ((ULONG)Mac[2] << 16) | ((ULONG)Mac[3] << 24);
}

static
BOOLEAN
BthSmpRandom(
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    return NT_SUCCESS(BCryptGenRandom(NULL, Buffer, Length, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

static
BOOLEAN
BthSmpGenerateKeyPair(
    _Inout_ PBTHMINI_SMP Smp)
{
    RSNA_P256_POINT Generator, Public;
    RSNA_P256_FE X, Y;
    UCHAR Seed[32], PrivateBe[32], PublicX[32], PublicY[32];

    if (!BthSmpRandom(Seed, sizeof(Seed)))
        return FALSE;
    RsnaP256ScalarReduceNonZero(PrivateBe, Seed);
    if (!RsnaP256FieldFromBytes(&X, BthP256Gx) || !RsnaP256FieldFromBytes(&Y, BthP256Gy))
        return FALSE;
    RsnaP256PointSetAffine(&Generator, &X, &Y);
    RsnaP256PointMul(&Public, &Generator, PrivateBe);
    if (!RsnaP256PointToBytes(PublicX, PublicY, &Public))
        return FALSE;
    RtlCopyMemory(Smp->PrivateKey, PrivateBe, 32);
    BthSmpReverse(Smp->LocalPublic, PublicX, 32);
    BthSmpReverse(Smp->LocalPublic + 32, PublicY, 32);
    RtlSecureZeroMemory(Seed, sizeof(Seed));
    RtlSecureZeroMemory(PrivateBe, sizeof(PrivateBe));
    return TRUE;
}

static
BOOLEAN
BthSmpComputeDhKey(
    _Inout_ PBTHMINI_SMP Smp)
{
    RSNA_P256_POINT Peer, Shared;
    UCHAR X[32], Y[32];

    BthSmpReverse(X, Smp->RemotePublic, 32);
    BthSmpReverse(Y, Smp->RemotePublic + 32, 32);
    if (!RsnaP256PointFromBytes(&Peer, X, Y))
        return FALSE;
    RsnaP256PointMul(&Shared, &Peer, Smp->PrivateKey);
    if (!RsnaP256PointToBytes(X, Y, &Shared))
        return FALSE;
    BthSmpReverse(Smp->DhKey, X, 32);
    RtlSecureZeroMemory(X, sizeof(X));
    return TRUE;
}

static
VOID
BthSmpSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Code,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Pdu[1 + 64];

    Pdu[0] = Code;
    if (Length)
        RtlCopyMemory(Pdu + 1, Data, Length);
    BthL2capSendLe(Dev, Remote, LE_CID_SMP, Pdu, 1 + Length);
}

static
NTSTATUS
BthSmpOpenKeys(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _Out_ PHANDLE Key)
{
    WCHAR Radio[13], Device[13];
    HANDLE RadioKey;
    NTSTATUS Status;

    BthFormatAddress(Dev->Address, Radio);
    BthFormatAddress(Remote->Address, Device);
    Status = BthOpenParameters(L"Keys", Radio, &RadioKey);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = BthCreateKey(RadioKey, Device, Key);
    ZwClose(RadioKey);
    return Status;
}

static
VOID
BthSmpSaveKeys(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_SMP Smp)
{
    UNICODE_STRING ValueName;
    HANDLE Key;
    ULONG Value;

    if (!NT_SUCCESS(BthSmpOpenKeys(Dev, Remote, &Key)))
        return;
    RtlInitUnicodeString(&ValueName, L"LTK");
    ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Remote->LeLtk, sizeof(Remote->LeLtk));
    RtlInitUnicodeString(&ValueName, L"KeyLength");
    Value = Remote->LeKeySize;
    ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
    RtlInitUnicodeString(&ValueName, L"EDIV");
    Value = Remote->LeEdiv;
    ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
    RtlInitUnicodeString(&ValueName, L"ERand");
    ZwSetValueKey(Key, &ValueName, 0, REG_QWORD, &Remote->LeRand, sizeof(Remote->LeRand));
    RtlInitUnicodeString(&ValueName, L"AddressType");
    Value = Remote->LeAddressType;
    ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
    RtlInitUnicodeString(&ValueName, L"AuthReq");
    Value = (Remote->LeKeyAuthenticated ? SMP_AUTH_MITM : 0) | (Remote->LeKeySecureConnections ? SMP_AUTH_SC : 0) |
            SMP_AUTH_BONDING;
    ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
    if (Smp->HaveIrk)
    {
        RtlInitUnicodeString(&ValueName, L"IRK");
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Smp->Irk, sizeof(Smp->Irk));
    }
    ZwClose(Key);
    BthConnSaveDevice(Remote);
}

static
VOID
BthSmpAdoptIdentity(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_SMP Smp)
{
    PBTHMINI_REMOTE Other;

    if (!Smp->HaveIdentity || !Smp->IdentityAddress || Smp->IdentityAddress == Remote->Address)
        return;
    Other = BthRadioFindRemote(Dev, Smp->IdentityAddress);
    if (Other)
    {
        if ((Other->Flags & BDIF_CONNECTED) || Other->LeConnected || Other->Gatt || Other->Smp || Other->AuthRequest ||
            Other->PromptResponse)
        {
            DPRINT1("BTHMINI: identity %012I64x of %012I64x is busy, keeping the private address\n",
                    Smp->IdentityAddress, Remote->Address);
            return;
        }
        if (Other->HasLinkKey)
        {
            RtlCopyMemory(Remote->LinkKey, Other->LinkKey, sizeof(Remote->LinkKey));
            Remote->HasLinkKey = TRUE;
            Remote->LinkKeyType = Other->LinkKeyType;
        }
        if (Other->Flags & BDIF_COD)
            Remote->ClassOfDevice = Other->ClassOfDevice;
        if (!Remote->Name[0])
            RtlCopyMemory(Remote->Name, Other->Name, sizeof(Remote->Name));
        Remote->Flags |= Other->Flags & (BDIF_COD | BDIF_NAME | BDIF_PAIRED | BDIF_PERSONAL | BDIF_SSP_PAIRED |
                                         BDIF_SSP_MITM_PROTECTED | BDIF_BR | BDIF_EIR);
        RemoveEntryList(&Other->Link);
        Dev->RemoteCount--;
        ExFreePoolWithTag(Other, BTHMINI_TAG);
    }
    DPRINT1("BTHMINI: %012I64x resolves to identity %012I64x\n", Remote->Address, Smp->IdentityAddress);
    Remote->Address = Smp->IdentityAddress;
    Remote->LeAddressType = Smp->IdentityType;
    if (Remote->LeAddressType)
        Remote->Flags |= BDIF_LE_RANDOM_ADDRESS_TYPE;
    else
        Remote->Flags &= ~BDIF_LE_RANDOM_ADDRESS_TYPE;
}

static
VOID
BthSmpFinish(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    ULONG Previous = Remote->Flags;
    BOOLEAN Paired;

    if (!Smp)
        return;
    Remote->Smp = NULL;
    if (NT_SUCCESS(Status) && Smp->Pairing)
    {
        RtlCopyMemory(Remote->LeLtk, Smp->Ltk, sizeof(Remote->LeLtk));
        Remote->LeKeySize = Smp->KeySize;
        Remote->LeEdiv = Smp->Ediv;
        Remote->LeRand = Smp->Rand;
        Remote->LeKeyAuthenticated = Smp->Method != SMP_METHOD_JUST_WORKS;
        Remote->LeKeySecureConnections = Smp->SecureConnections;
        Remote->HasLeKey = Smp->HaveLtk;
        if (Smp->HaveIrk)
        {
            RtlCopyMemory(Remote->LeIrk, Smp->Irk, sizeof(Remote->LeIrk));
            Remote->HasLeIrk = TRUE;
        }
        if (Remote->HasLeKey)
            BthSmpAdoptIdentity(Dev, Remote, Smp);
        if (Remote->HasLeKey)
        {
            Remote->Flags |= BDIF_LE_PAIRED | BDIF_LE_PERSONAL;
            if (Remote->LeKeyAuthenticated)
                Remote->Flags |= BDIF_LE_MITM_PROTECTED;
            if (Smp->SecureConnections)
                Remote->Flags |= BDIF_LE_SECURE_CONNECTION_PAIRED;
            if (Smp->HaveIrk)
                Remote->Flags |= BDIF_LE_PRIVACY_ENABLED;
            BthSmpSaveKeys(Dev, Remote, Smp);
            BthLeBondsChanged(Dev);
        }
        BthRadioReportInRange(Dev, Remote, Previous);
        DPRINT1("BTHMINI: LE paired with %012I64x (%s, key size %u, bonded %u)\n", Remote->Address,
                Smp->SecureConnections ? "Secure Connections" : "legacy", Smp->KeySize, Remote->HasLeKey);
    }
    else if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHMINI: LE security with %012I64x failed 0x%08lx in state %u\n", Remote->Address, Status, Smp->State);
    }
    if (Smp->Request)
        WdfRequestComplete(Smp->Request, Status);
    if (Smp->PromptResponse)
    {
        PBTHMINI_AUTH_RESPONSE Response;

        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Smp->PromptResponse, sizeof(*Response), (PVOID *)&Response, NULL)))
        {
            Response->Authenticated = NT_SUCCESS(Status);
            WdfRequestCompleteWithInformation(Smp->PromptResponse, STATUS_SUCCESS, sizeof(*Response));
        }
        else
        {
            WdfRequestComplete(Smp->PromptResponse, STATUS_INVALID_BUFFER_SIZE);
        }
    }
    Paired = NT_SUCCESS(Status) && Smp->Pairing && Remote->HasLeKey;
    RtlSecureZeroMemory(Smp, sizeof(*Smp));
    ExFreePoolWithTag(Smp, BTHMINI_TAG);
    if (Paired)
        BthGattPaired(Dev, Remote);
    else if (Remote->LeConnected)
        BthGattLinkReady(Dev, Remote);
}

static
VOID
BthSmpFail(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Reason)
{
    BthSmpSend(Dev, Remote, SMP_PAIRING_FAILED, &Reason, 1);
    BthSmpFinish(Dev, Remote, STATUS_ACCESS_DENIED);
}

static
VOID
BthSmpStartEncryption(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_(16) const UCHAR *Key,
    _In_ ULONGLONG Rand,
    _In_ USHORT Ediv)
{
    UCHAR Parameters[28];
    ULONG Index;

    Parameters[0] = (UCHAR)Remote->LeHandle;
    Parameters[1] = (UCHAR)(Remote->LeHandle >> 8);
    for (Index = 0; Index < 8; Index++)
        Parameters[2 + Index] = (UCHAR)(Rand >> (Index * 8));
    Parameters[10] = (UCHAR)Ediv;
    Parameters[11] = (UCHAR)(Ediv >> 8);
    RtlCopyMemory(Parameters + 12, Key, 16);
    Remote->Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
    if (!NT_SUCCESS(BthHciQueueCommand(Dev, HCI_LE_ENABLE_ENCRYPTION, Parameters, sizeof(Parameters))))
        BthSmpFinish(Dev, Remote, STATUS_UNSUCCESSFUL);
}

static
VOID
BthSmpMaskKey(
    _Inout_updates_(16) PUCHAR Key,
    _In_ UCHAR KeySize)
{
    if (KeySize < 16)
        RtlZeroMemory(Key + KeySize, 16 - KeySize);
}

static
VOID
BthSmpSendPairingRequest(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    Smp->Preq[0] = SMP_PAIRING_REQUEST;
    Smp->Preq[1] = SMP_IO_DISPLAY_YES_NO;
    Smp->Preq[2] = 0x00;
    Smp->Preq[3] = SMP_AUTH_BONDING | SMP_AUTH_MITM | SMP_AUTH_SC;
    Smp->Preq[4] = SMP_MAX_KEY_SIZE;
    Smp->Preq[5] = 0x00;
    Smp->Preq[6] = SMP_DIST_ENC | SMP_DIST_ID;
    Smp->State = SmpWaitResponse;
    Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
    BthSmpSend(Dev, Remote, SMP_PAIRING_REQUEST, Smp->Preq + 1, 6);
}

static
VOID
BthSmpContinue(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote);

static
UCHAR
BthSmpSelectMethod(
    _In_ PBTHMINI_SMP Smp)
{
    if (!((Smp->Preq[3] | Smp->Pres[3]) & SMP_AUTH_MITM))
        return SMP_METHOD_JUST_WORKS;
    switch (Smp->Pres[1])
    {
        case SMP_IO_DISPLAY_ONLY:
            return SMP_METHOD_PASSKEY_INPUT;
        case SMP_IO_DISPLAY_YES_NO:
            return Smp->SecureConnections ? SMP_METHOD_NUMERIC_COMPARISON : SMP_METHOD_JUST_WORKS;
        case SMP_IO_KEYBOARD_ONLY:
            return SMP_METHOD_PASSKEY_DISPLAY;
        case SMP_IO_KEYBOARD_DISPLAY:
            return Smp->SecureConnections ? SMP_METHOD_NUMERIC_COMPARISON : SMP_METHOD_PASSKEY_DISPLAY;
        default:
            return SMP_METHOD_JUST_WORKS;
    }
}

static
BOOLEAN
BthSmpPrompt(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ ULONG Method,
    _In_ ULONG Value)
{
    if (!BthConnPrompt(Dev, Remote, Method, Value))
        return FALSE;
    DPRINT1("BTHMINI: LE pairing with %012I64x asks the user (method %lu)\n", Remote->Address, Method);
    return TRUE;
}

static
VOID
BthSmpLegacyConfirm(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    UCHAR Initiator[6], Responder[6], Confirm[16];

    if (!BthSmpRandom(Smp->LocalRandom, sizeof(Smp->LocalRandom)))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
        return;
    }
    BthWriteAddress(Initiator, Dev->Address);
    BthWriteAddress(Responder, Remote->Address);
    BthSmpC1(Smp->Tk, Smp->LocalRandom, Smp, 0, Initiator, Remote->LeAddressType, Responder, Confirm);
    Smp->State = SmpWaitConfirm;
    BthSmpSend(Dev, Remote, SMP_PAIRING_CONFIRM, Confirm, 16);
}

static
VOID
BthSmpPasskeyRound(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    UCHAR Confirm[16];

    if (!BthSmpRandom(Smp->LocalRandom, sizeof(Smp->LocalRandom)))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
        return;
    }
    BthSmpF4(Smp->LocalPublic, Smp->RemotePublic, Smp->LocalRandom, 0x80 | ((Smp->Passkey >> Smp->Round) & 1), Confirm);
    Smp->State = SmpWaitConfirm;
    BthSmpSend(Dev, Remote, SMP_PAIRING_CONFIRM, Confirm, 16);
}

static
BOOLEAN
BthSmpNewPasskey(
    _In_ PBTHMINI_SMP Smp)
{
    ULONG Value;

    if (!BthSmpRandom((PUCHAR)&Value, sizeof(Value)))
        return FALSE;
    Smp->Passkey = Value % SMP_PASSKEY_LIMIT;
    return TRUE;
}

static
VOID
BthSmpStartAuthentication(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    switch (Smp->Method)
    {
        case SMP_METHOD_PASSKEY_DISPLAY:
            if (!BthSmpNewPasskey(Smp))
            {
                BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
                return;
            }
            BthSmpPrompt(Dev, Remote, BTHMINI_AUTH_PASSKEY_NOTIFICATION, Smp->Passkey);
            break;

        case SMP_METHOD_PASSKEY_INPUT:
            if (!BthSmpPrompt(Dev, Remote, BTHMINI_AUTH_PASSKEY, 0))
            {
                BthSmpFail(Dev, Remote, SMP_ERROR_PASSKEY_ENTRY_FAILED);
                return;
            }
            Smp->State = SmpWaitUser;
            Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
            return;

        default:
            Smp->Passkey = 0;
            break;
    }
    BthSmpContinue(Dev, Remote);
}

static
VOID
BthSmpContinue(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    if (!Smp->SecureConnections)
    {
        ULONG Index;

        RtlZeroMemory(Smp->Tk, sizeof(Smp->Tk));
        for (Index = 0; Index < 4; Index++)
            Smp->Tk[Index] = (UCHAR)(Smp->Passkey >> (Index * 8));
        BthSmpLegacyConfirm(Dev, Remote);
        return;
    }
    if (Smp->Method == SMP_METHOD_PASSKEY_DISPLAY || Smp->Method == SMP_METHOD_PASSKEY_INPUT)
    {
        Smp->Round = 0;
        BthSmpPasskeyRound(Dev, Remote);
        return;
    }
    if (!BthSmpRandom(Smp->LocalRandom, sizeof(Smp->LocalRandom)))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
        return;
    }
    Smp->State = SmpWaitConfirm;
}

static
VOID
BthSmpPairingResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_(6) const UCHAR *Data)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    Smp->Pres[0] = SMP_PAIRING_RESPONSE;
    RtlCopyMemory(Smp->Pres + 1, Data, 6);
    Smp->KeySize = Data[3];
    if (Smp->KeySize < SMP_MIN_KEY_SIZE || Smp->KeySize > SMP_MAX_KEY_SIZE)
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_KEY_SIZE);
        return;
    }
    Smp->ExpectedKeys = Data[5] & Smp->Preq[6];
    Smp->SecureConnections = (Data[2] & SMP_AUTH_SC) && (Smp->Preq[3] & SMP_AUTH_SC);
    if (Smp->SecureConnections)
        Smp->ExpectedKeys &= (UCHAR)~SMP_DIST_ENC;
    Smp->Method = BthSmpSelectMethod(Smp);

    if (Smp->SecureConnections)
    {
        if (!BthSmpGenerateKeyPair(Smp))
        {
            BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
            return;
        }
        Smp->State = SmpWaitPublicKey;
        BthSmpSend(Dev, Remote, SMP_PAIRING_PUBLIC_KEY, Smp->LocalPublic, 64);
        return;
    }
    BthSmpStartAuthentication(Dev, Remote);
}

static
VOID
BthSmpPasskeyValue(
    _In_ PBTHMINI_SMP Smp,
    _Out_writes_(16) PUCHAR R)
{
    ULONG Index;

    RtlZeroMemory(R, 16);
    if (Smp->Method != SMP_METHOD_PASSKEY_DISPLAY && Smp->Method != SMP_METHOD_PASSKEY_INPUT)
        return;
    for (Index = 0; Index < 4; Index++)
        R[Index] = (UCHAR)(Smp->Passkey >> (Index * 8));
}

static
VOID
BthSmpCheckDhKey(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_(16) const UCHAR *Check)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    UCHAR A1[7], A2[7], Address[6], IoB[3], Expected[16], R[16];

    BthWriteAddress(Address, Dev->Address);
    BthSmpAddress(A1, 0, Address);
    BthWriteAddress(Address, Remote->Address);
    BthSmpAddress(A2, Remote->LeAddressType, Address);
    IoB[0] = Smp->Pres[3];
    IoB[1] = Smp->Pres[2];
    IoB[2] = Smp->Pres[1];
    BthSmpPasskeyValue(Smp, R);
    BthSmpF6(Smp->MacKey, Smp->RemoteRandom, Smp->LocalRandom, R, IoB, A2, A1, Expected);
    if (!RtlEqualMemory(Expected, Check, 16))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_DHKEY_CHECK_FAILED);
        return;
    }
    Smp->HaveLtk = TRUE;
    BthSmpMaskKey(Smp->Ltk, Smp->KeySize);
    Smp->State = SmpWaitEncryption;
    BthSmpStartEncryption(Dev, Remote, Smp->Ltk, 0, 0);
}

static
VOID
BthSmpSecureComplete(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    UCHAR A1[7], A2[7], Address[6], IoA[3], Check[16], R[16];

    BthWriteAddress(Address, Dev->Address);
    BthSmpAddress(A1, 0, Address);
    BthWriteAddress(Address, Remote->Address);
    BthSmpAddress(A2, Remote->LeAddressType, Address);
    BthSmpF5(Smp->DhKey, Smp->LocalRandom, Smp->RemoteRandom, A1, A2, Smp->MacKey, Smp->Ltk);
    IoA[0] = Smp->Preq[3];
    IoA[1] = Smp->Preq[2];
    IoA[2] = Smp->Preq[1];
    BthSmpPasskeyValue(Smp, R);
    BthSmpF6(Smp->MacKey, Smp->LocalRandom, Smp->RemoteRandom, R, IoA, A1, A2, Check);
    Smp->State = SmpWaitDhKeyCheck;
    BthSmpSend(Dev, Remote, SMP_PAIRING_DHKEY_CHECK, Check, 16);
}

static
VOID
BthSmpSecureRandom(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    BOOLEAN Passkey = Smp->Method == SMP_METHOD_PASSKEY_DISPLAY || Smp->Method == SMP_METHOD_PASSKEY_INPUT;
    UCHAR Expected[16];

    BthSmpF4(Smp->RemotePublic, Smp->LocalPublic, Smp->RemoteRandom,
             Passkey ? 0x80 | ((Smp->Passkey >> Smp->Round) & 1) : 0, Expected);
    if (!RtlEqualMemory(Expected, Smp->RemoteConfirm, 16))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_CONFIRM_FAILED);
        return;
    }
    if (Passkey && ++Smp->Round < SMP_PASSKEY_ROUNDS)
    {
        BthSmpPasskeyRound(Dev, Remote);
        return;
    }
    if (Smp->Method == SMP_METHOD_NUMERIC_COMPARISON)
    {
        ULONG Value = BthSmpG2(Smp->LocalPublic, Smp->RemotePublic, Smp->LocalRandom, Smp->RemoteRandom) %
                      SMP_PASSKEY_LIMIT;

        if (!BthSmpPrompt(Dev, Remote, BTHMINI_AUTH_NUMERIC_COMPARISON, Value))
        {
            BthSmpFail(Dev, Remote, SMP_ERROR_NUMERIC_COMPARISON);
            return;
        }
        Smp->State = SmpWaitUser;
        Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
        return;
    }
    BthSmpSecureComplete(Dev, Remote);
}

static
VOID
BthSmpLegacyRandom(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    UCHAR Expected[16], Initiator[6], Responder[6], Stk[16];

    BthWriteAddress(Initiator, Dev->Address);
    BthWriteAddress(Responder, Remote->Address);
    BthSmpC1(Smp->Tk, Smp->RemoteRandom, Smp, 0, Initiator, Remote->LeAddressType, Responder, Expected);
    if (!RtlEqualMemory(Expected, Smp->RemoteConfirm, 16))
    {
        BthSmpFail(Dev, Remote, SMP_ERROR_CONFIRM_FAILED);
        return;
    }
    BthSmpS1(Smp->Tk, Smp->RemoteRandom, Smp->LocalRandom, Stk);
    BthSmpMaskKey(Stk, Smp->KeySize);
    Smp->State = SmpWaitEncryption;
    BthSmpStartEncryption(Dev, Remote, Stk, 0, 0);
    RtlSecureZeroMemory(Stk, sizeof(Stk));
}

NTSTATUS
BthSmpAuthResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ WDFREQUEST Request,
    _In_ PBTHMINI_AUTH_RESPONSE Response,
    _Out_ PULONG_PTR Information)
{
    PBTHMINI_SMP Smp = Remote->Smp;
    ULONG Expected;

    if (!Smp || Smp->State != SmpWaitUser || Smp->PromptResponse)
        return STATUS_DEVICE_NOT_READY;
    Expected = Smp->Method == SMP_METHOD_NUMERIC_COMPARISON ? BTHMINI_AUTH_NUMERIC_COMPARISON : BTHMINI_AUTH_PASSKEY;
    if (Response->Method != Expected)
        return STATUS_DEVICE_NOT_READY;
    if (Response->Negative || (Expected == BTHMINI_AUTH_PASSKEY && Response->NumericValue >= SMP_PASSKEY_LIMIT))
    {
        PBTHMINI_AUTH_RESPONSE Output;

        BthSmpFail(Dev, Remote, Expected == BTHMINI_AUTH_PASSKEY ? SMP_ERROR_PASSKEY_ENTRY_FAILED :
                                                                  SMP_ERROR_NUMERIC_COMPARISON);
        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(*Output), (PVOID *)&Output, NULL)))
        {
            Output->Authenticated = FALSE;
            *Information = sizeof(*Output);
        }
        return STATUS_SUCCESS;
    }
    Smp->PromptResponse = Request;
    Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
    if (Expected == BTHMINI_AUTH_PASSKEY)
    {
        Smp->Passkey = Response->NumericValue;
        BthSmpContinue(Dev, Remote);
    }
    else
    {
        BthSmpSecureComplete(Dev, Remote);
    }
    return STATUS_PENDING;
}

static
VOID
BthSmpKeysDone(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    if (!Remote->Smp->ExpectedKeys && Remote->Smp->State == SmpWaitKeys)
        BthSmpFinish(Dev, Remote, STATUS_SUCCESS);
}

VOID
BthSmpReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    if (!Length)
        return;
    if (Data[0] == SMP_SECURITY_REQUEST)
    {
        if (!Smp && Remote->HasLeKey)
            BthSmpLinkUp(Dev, Remote);
        else if (!Smp)
            BthSmpPair(Dev, Remote, NULL);
        return;
    }
    if (!Smp)
    {
        UCHAR Reason = SMP_ERROR_PAIRING_NOT_SUPPORTED;

        if (Data[0] == SMP_PAIRING_REQUEST)
            BthSmpSend(Dev, Remote, SMP_PAIRING_FAILED, &Reason, 1);
        return;
    }
    if (Data[0] == SMP_PAIRING_FAILED)
    {
        DPRINT1("BTHMINI: %012I64x refused LE pairing, reason 0x%02x\n", Remote->Address, Length > 1 ? Data[1] : 0);
        BthSmpFinish(Dev, Remote, STATUS_ACCESS_DENIED);
        return;
    }

    switch (Smp->State)
    {
        case SmpWaitResponse:
            if (Data[0] == SMP_PAIRING_RESPONSE && Length >= 7)
                BthSmpPairingResponse(Dev, Remote, Data + 1);
            else
                BthSmpFail(Dev, Remote, SMP_ERROR_INVALID_PARAMETERS);
            return;

        case SmpWaitPublicKey:
            if (Data[0] != SMP_PAIRING_PUBLIC_KEY || Length < 65)
                break;
            RtlCopyMemory(Smp->RemotePublic, Data + 1, 64);
            if (RtlEqualMemory(Smp->RemotePublic, Smp->LocalPublic, 64) || !BthSmpComputeDhKey(Smp))
            {
                BthSmpFail(Dev, Remote, SMP_ERROR_DHKEY_CHECK_FAILED);
                return;
            }
            BthSmpStartAuthentication(Dev, Remote);
            return;

        case SmpWaitConfirm:
            if (Data[0] == SMP_KEYPRESS_NOTIFICATION)
                return;
            if (Data[0] != SMP_PAIRING_CONFIRM || Length < 17)
                break;
            RtlCopyMemory(Smp->RemoteConfirm, Data + 1, 16);
            Smp->State = SmpWaitRandom;
            BthSmpSend(Dev, Remote, SMP_PAIRING_RANDOM, Smp->LocalRandom, 16);
            return;

        case SmpWaitRandom:
            if (Data[0] != SMP_PAIRING_RANDOM || Length < 17)
                break;
            RtlCopyMemory(Smp->RemoteRandom, Data + 1, 16);
            if (Smp->SecureConnections)
                BthSmpSecureRandom(Dev, Remote);
            else
                BthSmpLegacyRandom(Dev, Remote);
            return;

        case SmpWaitDhKeyCheck:
            if (Data[0] != SMP_PAIRING_DHKEY_CHECK || Length < 17)
                break;
            BthSmpCheckDhKey(Dev, Remote, Data + 1);
            return;

        case SmpWaitUser:
            if (Data[0] == SMP_KEYPRESS_NOTIFICATION || Data[0] == SMP_PAIRING_CONFIRM)
            {
                if (Data[0] == SMP_PAIRING_CONFIRM && Length >= 17)
                    RtlCopyMemory(Smp->RemoteConfirm, Data + 1, 16);
                return;
            }
            break;

        case SmpWaitEncryption:
        case SmpWaitKeys:
            switch (Data[0])
            {
                case SMP_ENCRYPTION_INFORMATION:
                    if (Length >= 17)
                    {
                        RtlCopyMemory(Smp->Ltk, Data + 1, 16);
                        BthSmpMaskKey(Smp->Ltk, Smp->KeySize);
                    }
                    return;
                case SMP_CENTRAL_IDENTIFICATION:
                    if (Length >= 11)
                    {
                        ULONG Index;

                        Smp->Ediv = (USHORT)(Data[1] | (Data[2] << 8));
                        Smp->Rand = 0;
                        for (Index = 0; Index < 8; Index++)
                            Smp->Rand |= (ULONGLONG)Data[3 + Index] << (Index * 8);
                        Smp->HaveLtk = TRUE;
                        Smp->ExpectedKeys &= (UCHAR)~SMP_DIST_ENC;
                        BthSmpKeysDone(Dev, Remote);
                    }
                    return;
                case SMP_IDENTITY_INFORMATION:
                    if (Length >= 17)
                    {
                        RtlCopyMemory(Smp->Irk, Data + 1, 16);
                        Smp->HaveIrk = TRUE;
                    }
                    return;
                case SMP_IDENTITY_ADDRESS:
                    if (Length >= 8)
                    {
                        Smp->HaveIdentity = TRUE;
                        Smp->IdentityType = Data[1] & 0x01;
                        Smp->IdentityAddress = BthReadAddress(Data + 2);
                        Smp->ExpectedKeys &= (UCHAR)~SMP_DIST_ID;
                        BthSmpKeysDone(Dev, Remote);
                    }
                    return;
                case SMP_SIGNING_INFORMATION:
                    return;
                default:
                    break;
            }
            break;

        default:
            break;
    }
    BthSmpFail(Dev, Remote, SMP_ERROR_UNSPECIFIED);
}

NTSTATUS
BthSmpPair(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ WDFREQUEST Request)
{
    PBTHMINI_SMP Smp;
    NTSTATUS Status;

    if (Remote->Smp)
        return STATUS_DEVICE_BUSY;
    Smp = ExAllocatePoolZero(NonPagedPool, sizeof(*Smp), BTHMINI_TAG);
    if (!Smp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Smp->Pairing = TRUE;
    Smp->Request = Request;
    Smp->State = SmpWaitLink;
    Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
    Remote->Smp = Smp;

    Status = BthLeConnect(Dev, Remote);
    if (Status == STATUS_SUCCESS)
    {
        BthSmpSendPairingRequest(Dev, Remote);
        return STATUS_PENDING;
    }
    if (Status == STATUS_PENDING)
        return STATUS_PENDING;
    Remote->Smp = NULL;
    ExFreePoolWithTag(Smp, BTHMINI_TAG);
    return Status;
}

VOID
BthSmpLinkUp(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    if (Smp && Smp->Pairing && Smp->State == SmpWaitLink)
    {
        BthSmpSendPairingRequest(Dev, Remote);
        return;
    }
    if (Smp || !Remote->HasLeKey || Remote->LeEncrypted)
        return;
    Smp = ExAllocatePoolZero(NonPagedPool, sizeof(*Smp), BTHMINI_TAG);
    if (!Smp)
        return;
    Smp->State = SmpEncrypting;
    Remote->Smp = Smp;
    BthSmpStartEncryption(Dev, Remote, Remote->LeLtk, Remote->LeRand, Remote->LeEdiv);
}

VOID
BthSmpEncryptionChanged(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    PBTHMINI_SMP Smp = Remote->Smp;

    if (!Smp)
        return;
    if (Smp->State == SmpEncrypting)
    {
        if (!NT_SUCCESS(Status))
            DPRINT1("BTHMINI: stored LE key of %012I64x rejected\n", Remote->Address);
        BthSmpFinish(Dev, Remote, Status);
        return;
    }
    if (Smp->State != SmpWaitEncryption)
        return;
    if (!NT_SUCCESS(Status))
    {
        BthSmpFinish(Dev, Remote, Status);
        return;
    }
    Smp->State = SmpWaitKeys;
    Smp->Deadline = BthSmpNowMs() + SMP_TIMEOUT_MS;
    BthSmpKeysDone(Dev, Remote);
}

VOID
BthSmpLinkDown(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    BthSmpFinish(Dev, Remote, NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status);
}

VOID
BthSmpTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = BthSmpNowMs();
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Remote->Smp && Now >= Remote->Smp->Deadline && Remote->Smp->State != SmpWaitLink)
        {
            BthSmpFinish(Dev, Remote, STATUS_IO_TIMEOUT);
            BthLeDisconnect(Dev, Remote);
        }
    }
}

VOID
BthSmpLoadKeys(
    _In_ PBTHMINI_DEVICE Dev)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 32 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Info = (PKEY_BASIC_INFORMATION)Buffer;
    WCHAR Radio[13];
    HANDLE Keys;
    ULONG Index, Size;

    BthFormatAddress(Dev->Address, Radio);
    if (!NT_SUCCESS(BthOpenParameters(L"Keys", Radio, &Keys)))
        return;
    for (Index = 0; NT_SUCCESS(ZwEnumerateKey(Keys, Index, KeyBasicInformation, Info, sizeof(Buffer) - sizeof(WCHAR), &Size)); Index++)
    {
        UCHAR ValueBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 16];
        PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING Name, ValueName;
        PBTHMINI_REMOTE Remote;
        BTH_ADDR Address = 0;
        HANDLE Device;
        ULONG Char;

        if (Info->NameLength != 12 * sizeof(WCHAR))
            continue;
        for (Char = 0; Char < 12; Char++)
        {
            WCHAR C = Info->Name[Char];
            ULONG Digit = C >= L'0' && C <= L'9' ? C - L'0' : C >= L'a' && C <= L'f' ? C - L'a' + 10 :
                          C >= L'A' && C <= L'F' ? C - L'A' + 10 : 16;

            if (Digit > 15)
                break;
            Address = (Address << 4) | Digit;
        }
        if (Char != 12)
            continue;
        Name.Buffer = Info->Name;
        Name.Length = Name.MaximumLength = (USHORT)Info->NameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Keys, NULL);
        if (!NT_SUCCESS(ZwOpenKey(&Device, KEY_READ, &Attributes)))
            continue;
        RtlInitUnicodeString(&ValueName, L"LTK");
        Remote = NULL;
        if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
            Value->Type == REG_BINARY && Value->DataLength == 16)
        {
            Remote = BthRadioGetRemote(Dev, Address);
        }
        if (Remote)
        {
            RtlCopyMemory(Remote->LeLtk, Value->Data, 16);
            RtlInitUnicodeString(&ValueName, L"KeyLength");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_DWORD)
                Remote->LeKeySize = (UCHAR)*(PULONG)Value->Data;
            RtlInitUnicodeString(&ValueName, L"EDIV");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_DWORD)
                Remote->LeEdiv = (USHORT)*(PULONG)Value->Data;
            RtlInitUnicodeString(&ValueName, L"ERand");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_QWORD)
                Remote->LeRand = *(PULONGLONG)Value->Data;
            RtlInitUnicodeString(&ValueName, L"AddressType");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_DWORD)
                Remote->LeAddressType = (UCHAR)*(PULONG)Value->Data;
            RtlInitUnicodeString(&ValueName, L"IRK");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_BINARY && Value->DataLength == 16)
            {
                RtlCopyMemory(Remote->LeIrk, Value->Data, 16);
                Remote->HasLeIrk = TRUE;
            }
            RtlInitUnicodeString(&ValueName, L"AuthReq");
            if (NT_SUCCESS(ZwQueryValueKey(Device, &ValueName, KeyValuePartialInformation, Value, sizeof(ValueBuffer), &Size)) &&
                Value->Type == REG_DWORD)
            {
                Remote->LeKeyAuthenticated = (*(PULONG)Value->Data & SMP_AUTH_MITM) != 0;
                Remote->LeKeySecureConnections = (*(PULONG)Value->Data & SMP_AUTH_SC) != 0;
            }
            Remote->HasLeKey = TRUE;
            if (!(Remote->Flags & BDIF_PAIRED))
                Remote->Flags &= ~BDIF_BR;
            Remote->Flags |= BDIF_LE | BDIF_LE_PAIRED | BDIF_LE_PERSONAL;
            BthConnLoadDevice(Remote);
            if (Remote->LeAddressType)
                Remote->Flags |= BDIF_LE_RANDOM_ADDRESS_TYPE;
            if (Remote->LeKeySecureConnections)
                Remote->Flags |= BDIF_LE_SECURE_CONNECTION_PAIRED;
        }
        ZwClose(Device);
    }
    ZwClose(Keys);
}

VOID
BthSmpDeleteKeys(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    HANDLE Key;

    if (NT_SUCCESS(BthSmpOpenKeys(Dev, Remote, &Key)))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }
    RtlSecureZeroMemory(Remote->LeLtk, sizeof(Remote->LeLtk));
    RtlSecureZeroMemory(Remote->LeIrk, sizeof(Remote->LeIrk));
    Remote->HasLeIrk = FALSE;
    Remote->HasLeKey = FALSE;
    BthLeBondsChanged(Dev);
    Remote->Flags &= ~(BDIF_LE_PAIRED | BDIF_LE_PERSONAL | BDIF_LE_SECURE_CONNECTION_PAIRED | BDIF_LE_PRIVACY_ENABLED);
}
