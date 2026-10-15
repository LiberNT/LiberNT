/*
 * PROJECT:     LiberNT Native WiFi (802.11) stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WPA2-PSK supplicant integration (RSNA 4-way handshake)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

/* The RSNA supplicant library API (pure crypto + 4-way handshake FSM). */
#include <rsna_supplicant.h>
#include <rsna_sae.h>
#include <nwifi_sae.h>
#include <bcrypt.h>

#define NDEBUG
#include <debug.h>

#define EAPOL_ETHERTYPE     0x888E
#define NWIFI_CREDENTIAL_MAX 128
#define NWIFI_RANDOM_POOL    128

/* Per-interface supplicant session.  Keeps a copy of the seeded passphrase
 * so a session can be re-initialised per SSID (the PMK is SSID-dependent). */
typedef struct _NWIFI_SUPPLICANT
{
    RSNA_CTX  Ctx;
    BOOLEAN   Initialised;          /* RsnaInit done (PMK derived)              */
    BOOLEAN   PassphraseSet;
    UCHAR     Passphrase[NWIFI_CREDENTIAL_MAX + 1];
    ULONG     PassphraseLen;
    UCHAR     OwnMac[IEEE80211_ADDR_LEN];
    BOOLEAN   KeysInstalled;
    volatile LONG InstallQueued;
    ULONG     Generation;           /* invalidates queued work across sessions */
    UCHAR     RandomPool[NWIFI_RANDOM_POOL];
    ULONG     RandomAvailable;
    ULONG     InstalledGroupSerial;
    BOOLEAN   GroupInstallQueued;
    BOOLEAN   SaePending;
    BOOLEAN   SaeH2e;
    UCHAR     SaeBssid[IEEE80211_ADDR_LEN];
    USHORT    SaeSendConfirm;
    RSNA_SAE  Sae;
} NWIFI_SUPPLICANT;

typedef struct _NWIFI_INSTALL_KEYS_CONTEXT
{
    PNWIFI_MSM Msm;
    ULONG Generation;
    UCHAR PeerMac[IEEE80211_ADDR_LEN];
    ULONG EapolLength;
    UCHAR Eapol[256];
} NWIFI_INSTALL_KEYS_CONTEXT, *PNWIFI_INSTALL_KEYS_CONTEXT;

static
int
NwifiSupplicantRandom(
    _In_ void *Context,
    _Out_writes_bytes_(Length) rsna_u8 *Buffer,
    _In_ rsna_size Length)
{
    NWIFI_SUPPLICANT *Sup = Context;

    if (Sup == NULL || Length > Sup->RandomAvailable)
        return -1;

    Sup->RandomAvailable -= (ULONG)Length;
    RtlCopyMemory(Buffer, Sup->RandomPool + Sup->RandomAvailable, Length);
    RtlSecureZeroMemory(Sup->RandomPool + Sup->RandomAvailable, Length);
    return 0;
}

static
VOID
NwifiSupplicantRefillRandom(
    _In_ PNWIFI_MSM Msm,
    _In_ NWIFI_SUPPLICANT *Expected,
    _In_ ULONG Generation)
{
    UCHAR Pool[NWIFI_RANDOM_POOL];
    NWIFI_SUPPLICANT *Sup;

    if (!NT_SUCCESS(BCryptGenRandom(NULL, Pool, sizeof(Pool), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        DPRINT1("NWIFI: random source unavailable\n");
        return;
    }

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL && Sup == Expected && Sup->Generation == Generation)
    {
        RtlCopyMemory(Sup->RandomPool, Pool, sizeof(Pool));
        Sup->RandomAvailable = sizeof(Pool);
    }
    NdisReleaseSpinLock(&Msm->Lock);
    RtlSecureZeroMemory(Pool, sizeof(Pool));
}

static
BOOLEAN
NwifiHexNibble(
    _In_ UCHAR Character,
    _Out_ PUCHAR Value)
{
    if (Character >= '0' && Character <= '9')
    {
        *Value = Character - '0';
        return TRUE;
    }
    if (Character >= 'a' && Character <= 'f')
    {
        *Value = Character - 'a' + 10;
        return TRUE;
    }
    if (Character >= 'A' && Character <= 'F')
    {
        *Value = Character - 'A' + 10;
        return TRUE;
    }
    return FALSE;
}

static
RSNA_STATUS
NwifiInitRsnaContext(
    _Out_ RSNA_CTX *Context,
    _In_ const DOT11_SSID *Ssid,
    _In_reads_bytes_(CredentialLength) const UCHAR *Credential,
    _In_ ULONG CredentialLength)
{
    RSNA_STATUS Status;

    RtlZeroMemory(Context, sizeof(*Context));
    if (Ssid->uSSIDLength == 0 ||
        Ssid->uSSIDLength > RSNA_SSID_MAX_LEN)
    {
        return RSNA_ERR_PARAM;
    }

    /* A 64-digit hexadecimal credential is an already-derived 256-bit PSK;
     * ordinary 8..63 byte credentials use PBKDF2-HMAC-SHA1. */
    if (CredentialLength == 64)
    {
        UCHAR Pmk[RSNA_PMK_LEN];
        ULONG Index;

        for (Index = 0; Index < ARRAYSIZE(Pmk); Index++)
        {
            UCHAR High;
            UCHAR Low;

            if (!NwifiHexNibble(Credential[Index * 2], &High) ||
                !NwifiHexNibble(Credential[Index * 2 + 1], &Low))
            {
                RtlSecureZeroMemory(Pmk, sizeof(Pmk));
                return RSNA_ERR_PARAM;
            }
            Pmk[Index] = (High << 4) | Low;
        }
        Status = RsnaInitWithPmk(Context,
                                 Ssid->ucSSID, Ssid->uSSIDLength,
                                 Pmk);
        RtlSecureZeroMemory(Pmk, sizeof(Pmk));
        return Status;
    }

    return RsnaInit(Context,
                    Ssid->ucSSID, Ssid->uSSIDLength,
                    (const char *)Credential, CredentialLength);
}

/* ===========================================================================
 *  Cipher-key install helpers (build DOT11 cipher-key structs, push down)
 * ===========================================================================
 */

/* Map an RSNA cipher to the dot11 cipher-algorithm id. */
static
DOT11_CIPHER_ALGORITHM
NwifiDot11CipherFromRsna(
    _In_ RSNA_CIPHER Cipher)
{
    return (Cipher == RSNA_CIPHER_TKIP) ? DOT11_CIPHER_ALGO_TKIP
                                        : DOT11_CIPHER_ALGO_CCMP;
}

static
NDIS_STATUS
NwifiEncodeCipherKey(
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength,
    _Out_writes_bytes_(EncodedCapacity) PUCHAR Encoded,
    _In_ ULONG EncodedCapacity,
    _Out_ PULONG EncodedLength)
{
    ULONG HeaderLength;

    *EncodedLength = 0;
    RtlZeroMemory(Encoded, EncodedCapacity);
    if (Key == NULL || KeyLength == 0)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    switch (Algorithm)
    {
        case DOT11_CIPHER_ALGO_CCMP:
        {
            PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Encoded;

            HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey);
            if (KeyLength != 16 || HeaderLength > EncodedCapacity ||
                KeyLength > EncodedCapacity - HeaderLength)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Ccmp->ulCCMPKeyLength = KeyLength;
            RtlCopyMemory(Ccmp->ucCCMPKey, Key, KeyLength);
            *EncodedLength = HeaderLength + KeyLength;
            return NDIS_STATUS_SUCCESS;
        }

        case DOT11_CIPHER_ALGO_TKIP:
        {
            PDOT11_KEY_ALGO_TKIP_MIC Tkip =
                (PDOT11_KEY_ALGO_TKIP_MIC)Encoded;

            HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC,
                                        ucTKIPMICKeys);
            if (KeyLength != 32 || HeaderLength > EncodedCapacity ||
                KeyLength > EncodedCapacity - HeaderLength)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Tkip->ulTKIPKeyLength = 16;
            Tkip->ulMICKeyLength = 16;
            RtlCopyMemory(Tkip->ucTKIPMICKeys, Key, KeyLength);
            *EncodedLength = HeaderLength + KeyLength;
            return NDIS_STATUS_SUCCESS;
        }

        case DOT11_CIPHER_ALGO_WEP40:
            if (KeyLength != 5)
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        case DOT11_CIPHER_ALGO_WEP104:
            if (KeyLength != 13)
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (KeyLength > EncodedCapacity)
        return NDIS_STATUS_INVALID_LENGTH;
    RtlCopyMemory(Encoded, Key, KeyLength);
    *EncodedLength = KeyLength;
    return NDIS_STATUS_SUCCESS;
}

/* OID_DOT11_CIPHER_KEY_MAPPING_KEY carries a DOT11_BYTE_ARRAY whose ucBuffer
 * is a DOT11_CIPHER_KEY_MAPPING_KEY_VALUE (peer MAC + inlined key). */
NDIS_STATUS
NwifiInstallPairwiseKey(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(6) PUCHAR PeerMac,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength)
{
    PDOT11_BYTE_ARRAY ByteArray;
    PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value;
    UCHAR EncodedKey[64];
    ULONG EncodedLength;
    ULONG ValueSize;
    ULONG TotalSize;
    NDIS_STATUS Status;

    Status = NwifiEncodeCipherKey(Algorithm,
                                  Key,
                                  KeyLength,
                                  EncodedKey,
                                  sizeof(EncodedKey),
                                  &EncodedLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    /* DOT11_CIPHER_KEY_MAPPING_KEY_VALUE has a 1-byte ucKey[] placeholder. */
    ValueSize = FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + EncodedLength;
    TotalSize = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + ValueSize;

    ByteArray = (PDOT11_BYTE_ARRAY)NwifiAllocate(TotalSize);
    if (ByteArray == NULL)
    {
        RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
        return NDIS_STATUS_RESOURCES;
    }

    ByteArray->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    ByteArray->Header.Revision = DOT11_CIPHER_KEY_MAPPING_KEY_VALUE_BYTE_ARRAY_REVISION_1;
    ByteArray->Header.Size = sizeof(DOT11_BYTE_ARRAY);
    ByteArray->uNumOfBytes = ValueSize;
    ByteArray->uTotalNumOfBytes = ValueSize;

    Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)ByteArray->ucBuffer;
    RtlCopyMemory(Value->PeerMacAddr, PeerMac, IEEE80211_ADDR_LEN);
    Value->AlgorithmId = Algorithm;
    Value->Direction = DOT11_DIR_BOTH;
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)EncodedLength;
    RtlCopyMemory(Value->ucKey, EncodedKey, EncodedLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
                                    ByteArray, TotalSize, NULL);
    RtlSecureZeroMemory(ByteArray, TotalSize);
    NwifiFree(ByteArray);
    RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
    DPRINT1("NWIFI: install pairwise key (cipher %u len %u) -> 0x%08X\n",
            Algorithm, KeyLength, Status);
    return Status;
}

/* OID_DOT11_CIPHER_DEFAULT_KEY carries a DOT11_CIPHER_DEFAULT_KEY_VALUE
 * directly.  Install the GTK at the given index and select it as default. */
NDIS_STATUS
NwifiInstallGroupKey(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ UCHAR KeyId,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength)
{
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    UCHAR EncodedKey[64];
    ULONG EncodedLength;
    ULONG ValueSize;
    ULONG DefaultKeyId;
    NDIS_STATUS Status;

    Status = NwifiEncodeCipherKey(Algorithm,
                                  Key,
                                  KeyLength,
                                  EncodedKey,
                                  sizeof(EncodedKey),
                                  &EncodedLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + EncodedLength;
    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
    {
        RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
        return NDIS_STATUS_RESOURCES;
    }

    Value->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Value->Header.Revision = DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1;
    Value->Header.Size = sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE);
    Value->uKeyIndex = KeyId;
    Value->AlgorithmId = Algorithm;
    /* A group key is not bound to a peer; the all-zero MAC means "group". */
    RtlZeroMemory(Value->MacAddr, IEEE80211_ADDR_LEN);
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)EncodedLength;
    RtlCopyMemory(Value->ucKey, EncodedKey, EncodedLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value, ValueSize, NULL);
    RtlSecureZeroMemory(Value, ValueSize);
    NwifiFree(Value);
    RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));

    if (Status == NDIS_STATUS_SUCCESS)
    {
        DefaultKeyId = KeyId;
        Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                        OID_DOT11_CIPHER_DEFAULT_KEY_ID,
                                        &DefaultKeyId,
                                        sizeof(DefaultKeyId),
                                        NULL);
    }
    DPRINT1("NWIFI: install group key id %u (cipher %u len %u) -> 0x%08X\n",
            KeyId, Algorithm, KeyLength, Status);
    return Status;
}

static
DOT11_CIPHER_ALGORITHM
NwifiGroupCipherFromKeys(
    _In_ const RSNA_KEYS *Keys)
{
    return Keys->gtkLen == RSNA_TK_TKIP_LEN ? DOT11_CIPHER_ALGO_TKIP : DOT11_CIPHER_ALGO_CCMP;
}

static
NDIS_STATUS
NwifiInstallIgtk(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ const RSNA_KEYS *Keys)
{
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    PDOT11_KEY_ALGO_BIP Bip;
    ULONG KeyLength = FIELD_OFFSET(DOT11_KEY_ALGO_BIP, ucBIPKey) + (ULONG)Keys->igtkLen;
    ULONG ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + KeyLength;
    NDIS_STATUS Status;

    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Value, ValueSize);
    Value->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Value->Header.Revision = DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1;
    Value->Header.Size = sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE);
    Value->uKeyIndex = Keys->igtkKeyId;
    Value->AlgorithmId = DOT11_CIPHER_ALGO_BIP;
    Value->usKeyLength = (USHORT)KeyLength;
    Bip = (PDOT11_KEY_ALGO_BIP)Value->ucKey;
    RtlCopyMemory(Bip->ucIPN, Keys->igtkIpn, sizeof(Bip->ucIPN));
    Bip->ulBIPKeyLength = (ULONG)Keys->igtkLen;
    RtlCopyMemory(Bip->ucBIPKey, Keys->igtk, Keys->igtkLen);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value, ValueSize, NULL);
    RtlSecureZeroMemory(Value, ValueSize);
    NwifiFree(Value);
    DPRINT1("NWIFI: install IGTK id %u -> 0x%08X\n", Keys->igtkKeyId, Status);
    return Status;
}

/* ===========================================================================
 *  Session lifecycle
 * ===========================================================================
 */
NDIS_STATUS
NwifiSupplicantSetPassphrase(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Passphrase,
    _In_ ULONG Length)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_SUPPLICANT Candidate;

    if (Length == 0 || Length > NWIFI_CREDENTIAL_MAX)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    /* The session may not exist yet (SET_KEY can precede CONNECT); allocate a
     * placeholder to stash the passphrase until the connect arms it. */
    Candidate = (PNWIFI_SUPPLICANT)NwifiAllocate(sizeof(NWIFI_SUPPLICANT));

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL && Candidate != NULL)
    {
        Sup = Candidate;
        Candidate = NULL;
        Msm->Supplicant = Sup;
    }
    if (Sup == NULL)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    Sup->Generation++;
    Sup->Initialised = FALSE;
    Sup->KeysInstalled = FALSE;
    Sup->InstallQueued = 0;
    RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
    RtlCopyMemory(Sup->Passphrase, Passphrase, Length);
    Sup->Passphrase[Length] = 0;
    Sup->PassphraseLen = Length;
    Sup->PassphraseSet = TRUE;
    NdisReleaseSpinLock(&Msm->Lock);

    if (Candidate != NULL)
    {
        NwifiFree(Candidate);
    }
    DPRINT1("NWIFI: supplicant passphrase seeded (%u bytes)\n", Length);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NwifiSupplicantStart(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    DOT11_SSID Ssid;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;
    UCHAR Credential[NWIFI_CREDENTIAL_MAX];
    ULONG CredentialLength;
    ULONG Generation;
    RSNA_CTX NewCtx;
    RSNA_STATUS RStatus;
    UCHAR RandomPool[NWIFI_RANDOM_POOL];
    UCHAR RsnIe[22] = {
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x00,
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x00,
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,
        0x00, 0x00
    };
    static const UCHAR NoPmk[RSNA_PMK_LEN] = { 0 };

    RtlZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlZeroMemory(Credential, sizeof(Credential));

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->PassphraseSet)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_FAILURE;
    }
    CredentialLength = Sup->PassphraseLen;
    if (CredentialLength > sizeof(Credential))
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_INVALID_LENGTH;
    }
    RtlCopyMemory(Credential, Sup->Passphrase, CredentialLength);
    Generation = Sup->Generation;
    Ssid = Msm->Connect.Ssid;
    AuthAlgorithm = Msm->Connect.AuthAlgorithm;
    UnicastCipher = Msm->Connect.UnicastCipher;
    MulticastCipher = Msm->Connect.MulticastCipher;
    NdisReleaseSpinLock(&Msm->Lock);

    if (AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE)
    {
        RStatus = RsnaInitWithPmk(&NewCtx, Ssid.ucSSID, Ssid.uSSIDLength, NoPmk);
        RsnIe[19] = RSNA_AKM_SAE;
        RsnIe[20] = 0xC0;
    }
    else if (AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK)
    {
        RStatus = NwifiInitRsnaContext(&NewCtx,
                                       &Ssid,
                                       Credential,
                                       CredentialLength);
    }
    else
    {
        RStatus = RSNA_ERR_PARAM;
    }
    if (RStatus != RSNA_OK)
    {
        goto StartFailed;
    }

    if (UnicastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        RsnIe[13] = 4;
    }
    else if (UnicastCipher == DOT11_CIPHER_ALGO_TKIP)
    {
        RsnIe[13] = 2;
    }
    else
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }
    if (MulticastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        RsnIe[7] = 4;
    }
    else if (MulticastCipher == DOT11_CIPHER_ALGO_TKIP)
    {
        RsnIe[7] = 2;
    }
    else
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }
    RStatus = RsnaSetRsnIe(&NewCtx, RsnIe, sizeof(RsnIe));
    if (RStatus != RSNA_OK)
        goto StartFailed;

    /* The supplicant is the station (SPA == our MAC). */
    RStatus = RsnaSetStaAddr(&NewCtx, Adapter->MacAddress);
    if (RStatus != RSNA_OK)
        goto StartFailed;

    if (!NT_SUCCESS(BCryptGenRandom(NULL, RandomPool, sizeof(RandomPool),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        RStatus = RSNA_ERR_RANDOM;
        goto StartFailed;
    }
    RsnaSetRandom(&NewCtx, NwifiSupplicantRandom, Sup);

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->Supplicant != Sup || Sup->Generation != Generation ||
        Msm->Connect.AuthAlgorithm != AuthAlgorithm ||
        Msm->Connect.Ssid.uSSIDLength != Ssid.uSSIDLength ||
        RtlCompareMemory(Msm->Connect.Ssid.ucSSID, Ssid.ucSSID, Ssid.uSSIDLength) != Ssid.uSSIDLength)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        RStatus = RSNA_ERR_STATE;
        goto StartFailed;
    }
    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    Sup->Ctx = NewCtx;
    RtlCopyMemory(Sup->OwnMac, Adapter->MacAddress, IEEE80211_ADDR_LEN);
    RtlCopyMemory(Sup->RandomPool, RandomPool, sizeof(RandomPool));
    Sup->RandomAvailable = sizeof(RandomPool);
    Sup->Generation++;
    Sup->Initialised = AuthAlgorithm != DOT11_AUTH_ALGO_WPA3_SAE;
    Sup->SaePending = AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE;
    Sup->SaeH2e = FALSE;
    RsnaSaeClear(&Sup->Sae);
    Sup->KeysInstalled = FALSE;
    Sup->InstallQueued = 0;
    Sup->InstalledGroupSerial = 0;
    Sup->GroupInstallQueued = FALSE;
    NdisReleaseSpinLock(&Msm->Lock);

    RtlSecureZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlSecureZeroMemory(RandomPool, sizeof(RandomPool));
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    DPRINT1("NWIFI: supplicant started for SSID len %u\n", Ssid.uSSIDLength);
    return NDIS_STATUS_SUCCESS;

StartFailed:
    RtlSecureZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    RtlSecureZeroMemory(RandomPool, sizeof(RandomPool));
    DPRINT1("NWIFI: supplicant initialization failed %d\n", RStatus);
    return (RStatus == RSNA_ERR_PARAM) ? NDIS_STATUS_INVALID_DATA
                                      : NDIS_STATUS_FAILURE;
}

static
int
NwifiSaeRandom(
    _In_ void *Context,
    _Out_writes_bytes_(Length) rsna_u8 *Buffer,
    _In_ rsna_size Length)
{
    UNREFERENCED_PARAMETER(Context);
    return NT_SUCCESS(BCryptGenRandom(NULL, Buffer, (ULONG)Length, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ? 0 : -1;
}

static
NDIS_STATUS
NwifiSaeSendParams(
    _In_ PNWIFI_MSM Msm,
    _In_ PNWIFI_SAE_PARAMS Params)
{
    UCHAR Message[NWIFI_SAE_MAX_MESSAGE];
    ULONG Length;
    NDIS_STATUS Status;

    Length = NwifiSaeBuildParams(Message, sizeof(Message), Params);
    if (Length == 0)
        return NDIS_STATUS_INVALID_LENGTH;

    Status = NwifiProtocolDoRequest(Msm->Adapter,
                                    NdisRequestSetInformation,
                                    OID_WDI_SET_SAE_AUTH_PARAMS,
                                    Message,
                                    Length,
                                    NULL);
    RtlSecureZeroMemory(Message, sizeof(Message));
    return Status;
}

VOID
NwifiSupplicantSaeIndication(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_SAE_PARAMS Params;
    RSNA_SAE *Sae;
    UCHAR Bssid[IEEE80211_ADDR_LEN], OwnMac[IEEE80211_ADDR_LEN];
    UCHAR Password[NWIFI_CREDENTIAL_MAX];
    UCHAR Pt[RSNA_SAE_ELEMENT_LEN];
    DOT11_SSID Ssid;
    const UCHAR *Frame;
    ULONG IndicationType, FrameLength, PasswordLength, Generation, CommitRequest;
    USHORT FrameStatus, SendConfirm = 0;
    BOOLEAN Done = FALSE, Success = FALSE, H2e;

    if (!NwifiSaeParseIndication(Message, Length, Bssid, &IndicationType, &Frame, &FrameLength))
        return;

    Params = (PNWIFI_SAE_PARAMS)NwifiAllocate(sizeof(*Params));
    Sae = (RSNA_SAE *)NwifiAllocate(sizeof(*Sae));
    if (Params == NULL || Sae == NULL)
    {
        if (Params)
            NwifiFree(Params);
        if (Sae)
            NwifiFree(Sae);
        return;
    }
    RtlZeroMemory(Params, sizeof(*Params));
    RtlCopyMemory(Params->Bssid, Bssid, IEEE80211_ADDR_LEN);
    Params->RequestType = WDI_SAE_REQUEST_TYPE_FAILURE;
    Params->HaveStatus = TRUE;
    Params->Status = WDI_SAE_STATUS_FAILURE;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->SaePending || !Sup->PassphraseSet ||
        Sup->PassphraseLen == 0 || Sup->PassphraseLen > sizeof(Password))
    {
        NdisReleaseSpinLock(&Msm->Lock);
        Params->Status = WDI_SAE_STATUS_INITIALIZATION_NO_KEY;
        goto Reply;
    }
    PasswordLength = Sup->PassphraseLen;
    RtlCopyMemory(Password, Sup->Passphrase, PasswordLength);
    RtlCopyMemory(OwnMac, Msm->Adapter->MacAddress, IEEE80211_ADDR_LEN);
    *Sae = Sup->Sae;
    SendConfirm = Sup->SaeSendConfirm;
    Generation = Sup->Generation;
    H2e = Sup->SaeH2e;
    Ssid = Msm->Connect.Ssid;
    NdisReleaseSpinLock(&Msm->Lock);

    switch (IndicationType)
    {
        case WDI_SAE_INDICATION_TYPE_COMMIT_REQUEST_PARAMS_NEEDED:
            RsnaSaeClear(Sae);
            H2e = NwifiMsmBssUsesSaeH2e(Msm, Bssid);
            if (H2e)
            {
                if (!RsnaSaeDerivePt(Pt, Ssid.ucSSID, Ssid.uSSIDLength, Password, PasswordLength, NULL, 0) ||
                    !RsnaSaeDerivePweFromPt(Sae, Pt, OwnMac, Bssid))
                {
                    Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_INTERNAL_COMPUTATION_ERROR;
                    break;
                }
            }
            else if (!RsnaSaeDerivePwe(Sae, OwnMac, Bssid, Password, PasswordLength))
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_INTERNAL_COMPUTATION_ERROR;
                break;
            }
            if (!RsnaSaeGenerateCommit(Sae, NwifiSaeRandom, NULL))
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_INTERNAL_COMPUTATION_ERROR;
                break;
            }
            SendConfirm = 0;
            Params->RequestType = H2e ? WDI_SAE_REQUEST_TYPE_COMMIT_H2E_PARAMS : WDI_SAE_REQUEST_TYPE_COMMIT_PARAMS;
            break;

        case WDI_SAE_INDICATION_TYPE_COMMIT_FRAME:
            if (Frame == NULL || FrameLength < 8 || !Sae->HaveCommit)
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_MALFORMED_AUTH_FRAME;
                break;
            }
            FrameStatus = (USHORT)(Frame[4] | (Frame[5] << 8));
            CommitRequest = H2e ? WDI_SAE_REQUEST_TYPE_COMMIT_H2E_PARAMS : WDI_SAE_REQUEST_TYPE_COMMIT_PARAMS;
            if (FrameStatus == IEEE80211_STATUS_SAE_ANTI_CLOGGING)
            {
                const UCHAR *Token = Frame + 8;
                ULONG TokenLength = FrameLength - 8;

                if (H2e)
                {
                    if (TokenLength < 3 || Token[0] != IEEE80211_ELEMENT_EXTENSION ||
                        Token[2] != IEEE80211_EXT_ANTI_CLOGGING_TOKEN || Token[1] < 1 ||
                        (ULONG)Token[1] + 2 > TokenLength)
                    {
                        Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_MALFORMED_AUTH_FRAME;
                        break;
                    }
                    TokenLength = Token[1] - 1;
                    Token += 3;
                }
                if (TokenLength > NWIFI_SAE_MAX_TOKEN)
                {
                    Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_MALFORMED_AUTH_FRAME;
                    break;
                }
                Params->TokenLength = TokenLength;
                RtlCopyMemory(Params->Token, Token, TokenLength);
                Params->RequestType = CommitRequest;
                break;
            }
            if (FrameStatus != (H2e ? IEEE80211_STATUS_SAE_HASH_TO_ELEMENT : 0))
            {
                Params->Status = FrameStatus == IEEE80211_STATUS_SAE_UNSUPPORTED_GROUP ?
                                 WDI_SAE_STATUS_COMMIT_MESSAGE_UNSUPPORTED_FINITE_GROUP :
                                 WDI_SAE_STATUS_COMMIT_MESSAGE_FAILURE_FROM_PEER;
                break;
            }
            if ((Frame[6] | (Frame[7] << 8)) != NWIFI_SAE_GROUP_P256)
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_UNSUPPORTED_FINITE_GROUP;
                break;
            }
            if (FrameLength < 8 + NWIFI_SAE_SCALAR_LENGTH + NWIFI_SAE_ELEMENT_LENGTH)
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_INVALID_SCALAR_ELEMENT_SIZE;
                break;
            }
            if (!RsnaSaeProcessCommit(Sae, Frame + 8, Frame + 8 + NWIFI_SAE_SCALAR_LENGTH))
            {
                Params->Status = WDI_SAE_STATUS_COMMIT_MESSAGE_INTERNAL_COMPUTATION_ERROR;
                break;
            }
            SendConfirm++;
            Params->HaveConfirm = TRUE;
            Params->SendConfirm = SendConfirm;
            RsnaSaeComputeConfirm(Sae, SendConfirm, Params->Confirm);
            Params->RequestType = WDI_SAE_REQUEST_TYPE_CONFIRM_PARAMS;
            break;

        case WDI_SAE_INDICATION_TYPE_CONFIRM_REQUEST_RESEND_REQUEST:
            if (!Sae->HaveKeys)
            {
                Params->Status = WDI_SAE_STATUS_CONFIRM_MESSAGE_RECEIVED_IN_INVALID_STATE;
                break;
            }
            SendConfirm++;
            Params->HaveConfirm = TRUE;
            Params->SendConfirm = SendConfirm;
            RsnaSaeComputeConfirm(Sae, SendConfirm, Params->Confirm);
            Params->RequestType = WDI_SAE_REQUEST_TYPE_CONFIRM_PARAMS;
            break;

        case WDI_SAE_INDICATION_TYPE_CONFIRM_FRAME:
            if (Frame == NULL || FrameLength < 8 + NWIFI_SAE_CONFIRM_LENGTH || !Sae->HaveKeys)
            {
                Params->Status = WDI_SAE_STATUS_CONFIRM_MESSAGE_MALFORMED_AUTH_FRAME;
                break;
            }
            if (Frame[4] | (Frame[5] << 8))
            {
                Params->Status = WDI_SAE_STATUS_CONFIRM_MESSAGE_FAILURE_FROM_PEER;
                break;
            }
            if (!RsnaSaeVerifyConfirm(Sae, (USHORT)(Frame[6] | (Frame[7] << 8)), Frame + 8))
            {
                Params->Status = WDI_SAE_STATUS_CONFIRM_MESSAGE_VERIFICATION_FAILED;
                break;
            }
            Params->RequestType = WDI_SAE_REQUEST_TYPE_SUCCESS;
            Params->Status = WDI_SAE_STATUS_SUCCESS;
            Success = TRUE;
            break;

        default:
            Done = TRUE;
            break;
    }

    if (Params->RequestType == WDI_SAE_REQUEST_TYPE_COMMIT_PARAMS ||
        Params->RequestType == WDI_SAE_REQUEST_TYPE_COMMIT_H2E_PARAMS)
    {
        Params->HaveCommit = TRUE;
        Params->HaveStatus = FALSE;
        Params->Group = NWIFI_SAE_GROUP_P256;
        RtlCopyMemory(Params->Scalar, Sae->Scalar, NWIFI_SAE_SCALAR_LENGTH);
        RtlCopyMemory(Params->Element, Sae->Element, NWIFI_SAE_ELEMENT_LENGTH);
    }
    else if (Params->RequestType == WDI_SAE_REQUEST_TYPE_CONFIRM_PARAMS)
    {
        Params->HaveStatus = FALSE;
    }

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->Supplicant == Sup && Sup->Generation == Generation && Sup->SaePending)
    {
        Sup->Sae = *Sae;
        Sup->SaeSendConfirm = SendConfirm;
        Sup->SaeH2e = H2e;
        RtlCopyMemory(Sup->SaeBssid, Bssid, IEEE80211_ADDR_LEN);
        if (Success)
        {
            RsnaSetPmk(&Sup->Ctx, Sae->Pmk);
            Sup->Initialised = TRUE;
            Sup->SaePending = FALSE;
        }
    }
    else
    {
        Params->RequestType = WDI_SAE_REQUEST_TYPE_FAILURE;
        Params->HaveCommit = FALSE;
        Params->HaveConfirm = FALSE;
        Params->HaveStatus = TRUE;
        Params->Status = WDI_SAE_STATUS_CONNECTION_CLEANED_UP;
    }
    NdisReleaseSpinLock(&Msm->Lock);

Reply:
    if (!Done)
    {
        DPRINT1("NWIFI: SAE indication %lu -> request %lu status %lu\n",
                IndicationType, Params->RequestType, Params->HaveStatus ? Params->Status : 0);
        NwifiSaeSendParams(Msm, Params);
    }
    RtlSecureZeroMemory(Password, sizeof(Password));
    RtlSecureZeroMemory(Pt, sizeof(Pt));
    RtlSecureZeroMemory(Params, sizeof(*Params));
    RtlSecureZeroMemory(Sae, sizeof(*Sae));
    NwifiFree(Params);
    NwifiFree(Sae);
}

typedef struct _NWIFI_SAE_WORK
{
    PNWIFI_MSM Msm;
    ULONG Length;
    UCHAR Message[NWIFI_SAE_MAX_MESSAGE];
} NWIFI_SAE_WORK, *PNWIFI_SAE_WORK;

static
VOID
NTAPI
NwifiSupplicantSaeWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_SAE_WORK Work = (PNWIFI_SAE_WORK)WorkItemContext;
    PNWIFI_MSM Msm;

    if (Work == NULL)
        return;
    Msm = Work->Msm;
    NwifiSupplicantSaeIndication(Msm, Work->Message, Work->Length);
    RtlSecureZeroMemory(Work, sizeof(*Work));
    NwifiFree(Work);
    NwifiMsmCompleteWorkItem(Msm, NdisIoWorkItemHandle);
}

VOID
NwifiSupplicantQueueSaeIndication(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) const VOID *Message,
    _In_ ULONG Length)
{
    PNWIFI_SAE_WORK Work;

    if (Length > NWIFI_SAE_MAX_MESSAGE)
        return;
    Work = (PNWIFI_SAE_WORK)NwifiAllocate(sizeof(*Work));
    if (Work == NULL)
        return;
    Work->Msm = Msm;
    Work->Length = Length;
    RtlCopyMemory(Work->Message, Message, Length);
    if (!NwifiMsmQueueWorkItem(Msm, NwifiSupplicantSaeWorker, Work))
        NwifiFree(Work);
}

NDIS_STATUS
NwifiSupplicantSeedFirmware(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    UCHAR Credential[NWIFI_CREDENTIAL_MAX];
    UCHAR FirmwareCredential[NWIFI_CREDENTIAL_MAX];
    DOT11_SSID Ssid;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    ULONG CredentialLength;
    ULONG FirmwareCredentialLength;
    ULONG ValueSize;
    NDIS_STATUS Status;

    RtlZeroMemory(Credential, sizeof(Credential));
    RtlZeroMemory(FirmwareCredential, sizeof(FirmwareCredential));
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->PassphraseSet ||
        Sup->PassphraseLen == 0 ||
        Sup->PassphraseLen > sizeof(Credential))
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_INVALID_DATA;
    }
    CredentialLength = Sup->PassphraseLen;
    RtlCopyMemory(Credential, Sup->Passphrase, CredentialLength);
    Ssid = Msm->Connect.Ssid;
    AuthAlgorithm = Msm->Connect.AuthAlgorithm;
    NdisReleaseSpinLock(&Msm->Lock);

    if (AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK)
    {
        RSNA_CTX Context;
        RSNA_STATUS RStatus;

        RStatus = NwifiInitRsnaContext(&Context,
                                       &Ssid,
                                       Credential,
                                       CredentialLength);
        if (RStatus == RSNA_OK)
        {
            RStatus = RsnaGetPmk(&Context, FirmwareCredential);
        }
        RsnaClear(&Context);
        if (RStatus != RSNA_OK)
        {
            RtlSecureZeroMemory(Credential, sizeof(Credential));
            RtlSecureZeroMemory(FirmwareCredential,
                                sizeof(FirmwareCredential));
            return NDIS_STATUS_INVALID_DATA;
        }
        FirmwareCredentialLength = RSNA_PMK_LEN;
    }
    else if (AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE)
    {
        RtlCopyMemory(FirmwareCredential, Credential, CredentialLength);
        FirmwareCredentialLength = CredentialLength;
    }
    else
    {
        RtlSecureZeroMemory(Credential, sizeof(Credential));
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) +
                FirmwareCredentialLength;
    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
    {
        RtlSecureZeroMemory(Credential, sizeof(Credential));
        RtlSecureZeroMemory(FirmwareCredential, sizeof(FirmwareCredential));
        return NDIS_STATUS_RESOURCES;
    }

    Value->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Value->Header.Revision = DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1;
    Value->Header.Size = sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE);
    Value->uKeyIndex = 0;
    Value->AlgorithmId = DOT11_CIPHER_ALGO_NONE;
    RtlZeroMemory(Value->MacAddr, IEEE80211_ADDR_LEN);
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)FirmwareCredentialLength;
    RtlCopyMemory(Value->ucKey, FirmwareCredential,
                  FirmwareCredentialLength);

    Status = NwifiProtocolDoRequest(Msm->Adapter,
                                    NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value,
                                    ValueSize,
                                    NULL);
    RtlSecureZeroMemory(Value, ValueSize);
    NwifiFree(Value);
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    RtlSecureZeroMemory(FirmwareCredential, sizeof(FirmwareCredential));
    return Status;
}

VOID
NwifiSupplicantSetAssociation(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(6) PUCHAR ApMac,
    _In_reads_bytes_opt_(RequestIeLength) PUCHAR RequestIes,
    _In_ ULONG RequestIeLength)
{
    PNWIFI_SUPPLICANT Sup;
    ULONG Offset = 0;
    ULONG Length;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL && Sup->Initialised)
    {
        RsnaSetApAddr(&Sup->Ctx, ApMac);
        RsnaSetRsnxe(&Sup->Ctx, NULL, 0);
        while (RequestIes != NULL && Offset + 2 <= RequestIeLength)
        {
            Length = RequestIes[Offset + 1];
            if (Offset + 2 + Length > RequestIeLength)
                break;
            if (RequestIes[Offset] == IEEE80211_ELEMENT_RSN)
                RsnaSetRsnIe(&Sup->Ctx, RequestIes + Offset, Length + 2);
            else if (RequestIes[Offset] == IEEE80211_ELEMENT_RSNXE)
                RsnaSetRsnxe(&Sup->Ctx, RequestIes + Offset, Length + 2);
            Offset += 2 + Length;
        }
    }
    NdisReleaseSpinLock(&Msm->Lock);
}

/*
 * Zeroise key material and disarm the session, but do NOT free the object:
 * the RX path may still hold a raw pointer at DISPATCH_LEVEL on another CPU.
 * It is freed only in NwifiSupplicantFree once the data path is quiesced.
 */
VOID
NwifiSupplicantStop(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL)
    {
        if (Sup->Initialised)
        {
            RsnaClear(&Sup->Ctx);   /* zeroise PMK / PTK / GTK */
        }
        Sup->Generation++;
        Sup->Initialised = FALSE;
        Sup->KeysInstalled = FALSE;
        Sup->InstallQueued = 0;
        RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
        Sup->PassphraseLen = 0;
        Sup->PassphraseSet = FALSE;
    }
    NdisReleaseSpinLock(&Msm->Lock);
}

/* Fully free the supplicant object.  Only safe once the data path is
 * quiesced (NwifiMsmDestroy at unbind). */
VOID
NwifiSupplicantFree(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return;
    }
    Msm->Supplicant = NULL;
    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    NdisReleaseSpinLock(&Msm->Lock);

    RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
    RtlSecureZeroMemory(Sup, sizeof(*Sup));
    NwifiFree(Sup);
}

/* ===========================================================================
 *  Install the derived keys once the 4-way handshake completes
 *
 *  Runs at PASSIVE_LEVEL as a rundown-protected one-shot NDIS IO work item.
 * ===========================================================================
 */
static
VOID
NTAPI
NwifiSupplicantInstallKeysWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_INSTALL_KEYS_CONTEXT Context =
        (PNWIFI_INSTALL_KEYS_CONTEXT)WorkItemContext;
    PNWIFI_MSM Msm;
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_ADAPTER Adapter;
    RSNA_KEYS Keys;
    DOT11_CIPHER_ALGORITHM Cipher;
    DOT11_MAC_ADDRESS Bssid;
    NDIS_STATUS Status;

    if (Context == NULL || Context->Msm == NULL)
    {
        return;
    }
    Msm = Context->Msm;
    Adapter = Msm->Adapter;
    RtlZeroMemory(&Keys, sizeof(Keys));
    RtlZeroMemory(Bssid, sizeof(Bssid));

    /* Re-validate under the MSM lock: a disconnect/disassoc may have stopped the
     * supplicant between queueing and running this item. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || Sup->Generation != Context->Generation ||
        !Sup->Initialised || Sup->KeysInstalled)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    if (RsnaGetKeys(&Sup->Ctx, &Keys) != RSNA_OK)
    {
        Sup->InstallQueued = 0;
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    RtlCopyMemory(Bssid, Msm->CurrentBssid, IEEE80211_ADDR_LEN);
    NdisReleaseSpinLock(&Msm->Lock);

    Cipher = NwifiDot11CipherFromRsna(Keys.pairwiseCipher);

    /* Pairwise temporal key (PTK TK) for the AP, then the group key (GTK). */
    Status = NwifiInstallPairwiseKey(Adapter, Cipher, Bssid,
                                     (PUCHAR)Keys.tk, (ULONG)Keys.tkLen);

    if (Status == NDIS_STATUS_SUCCESS && Keys.gtkLen != 0)
    {
        Status = NwifiInstallGroupKey(Adapter, NwifiGroupCipherFromKeys(&Keys), Keys.gtkKeyId,
                                      (PUCHAR)Keys.gtk, (ULONG)Keys.gtkLen);
    }
    if (Status == NDIS_STATUS_SUCCESS && Keys.igtkLen != 0)
    {
        NwifiInstallIgtk(Adapter, &Keys);
    }
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Msm->Lock);
        Sup = Msm->Supplicant;
        if (Sup != NULL && Sup->Generation == Context->Generation)
        {
            Sup->KeysInstalled = FALSE;
            Sup->InstallQueued = 0;
        }
        NdisReleaseSpinLock(&Msm->Lock);
        NwifiQueueNotification(Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete,
                               Status);
        DPRINT1("NWIFI: WPA2 key installation failed 0x%08X\n", Status);
        goto Exit;
    }

    /* A disconnect can invalidate the session while the blocking key OIDs run. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || Sup->Generation != Context->Generation ||
        !Sup->Initialised || Sup->KeysInstalled)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    Sup->KeysInstalled = TRUE;
    Sup->InstallQueued = 0;
    Sup->InstalledGroupSerial = Keys.groupSerial;
    NdisReleaseSpinLock(&Msm->Lock);

    Status = NwifiInjectL2Frame(Adapter,
                                Context->PeerMac,
                                EAPOL_ETHERTYPE,
                                Context->Eapol,
                                Context->EapolLength,
                                TRUE);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Msm->Lock);
        Sup = Msm->Supplicant;
        if (Sup != NULL && Sup->Generation == Context->Generation)
            Sup->KeysInstalled = FALSE;
        NdisReleaseSpinLock(&Msm->Lock);
        NwifiQueueNotification(Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete,
                               Status);
        DPRINT1("NWIFI: WPA2 message 4 send failed 0x%08X\n", Status);
        goto Exit;
    }

    NwifiMsmLinkUp(Msm, TRUE);
    NwifiSupplicantRefillRandom(Msm, Sup, Context->Generation);
    DPRINT1("NWIFI: WPA2 4-way handshake complete; secure link up\n");

Exit:
    RtlSecureZeroMemory(&Keys, sizeof(Keys));
    RtlSecureZeroMemory(Context, sizeof(*Context));
    NwifiFree(Context);
    NwifiMsmCompleteWorkItem(Msm, NdisIoWorkItemHandle);
}

static
VOID
NTAPI
NwifiSupplicantInstallGroupKeysWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_INSTALL_KEYS_CONTEXT Context =
        (PNWIFI_INSTALL_KEYS_CONTEXT)WorkItemContext;
    PNWIFI_MSM Msm;
    PNWIFI_SUPPLICANT Sup;
    RSNA_KEYS Keys;
    NDIS_STATUS Status;

    if (Context == NULL || Context->Msm == NULL)
        return;
    Msm = Context->Msm;
    RtlZeroMemory(&Keys, sizeof(Keys));

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || Sup->Generation != Context->Generation ||
        !Sup->Initialised || !Sup->KeysInstalled ||
        RsnaGetKeys(&Sup->Ctx, &Keys) != RSNA_OK)
    {
        if (Sup != NULL && Sup->Generation == Context->Generation)
            Sup->GroupInstallQueued = FALSE;
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    NdisReleaseSpinLock(&Msm->Lock);

    Status = NwifiInstallGroupKey(Msm->Adapter, NwifiGroupCipherFromKeys(&Keys), Keys.gtkKeyId,
                                  (PUCHAR)Keys.gtk, (ULONG)Keys.gtkLen);
    if (Status == NDIS_STATUS_SUCCESS && Keys.igtkLen != 0)
        NwifiInstallIgtk(Msm->Adapter, &Keys);

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL && Sup->Generation == Context->Generation)
    {
        if (Status == NDIS_STATUS_SUCCESS)
            Sup->InstalledGroupSerial = Keys.groupSerial;
        Sup->GroupInstallQueued = FALSE;
    }
    NdisReleaseSpinLock(&Msm->Lock);

    NwifiSupplicantRefillRandom(Msm, Sup, Context->Generation);
    DPRINT1("NWIFI: group key rekey installed -> 0x%08X\n", Status);

Exit:
    RtlSecureZeroMemory(&Keys, sizeof(Keys));
    RtlSecureZeroMemory(Context, sizeof(*Context));
    NwifiFree(Context);
    NwifiMsmCompleteWorkItem(Msm, NdisIoWorkItemHandle);
}

/* ===========================================================================
 *  EAPOL-Key RX feed
 * ===========================================================================
 */
BOOLEAN
NwifiSupplicantRxEapol(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Eapol,
    _In_ ULONG Length,
    _In_ PUCHAR SrcMac,
    _In_ PUCHAR DstMac)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_INSTALL_KEYS_CONTEXT Context = NULL;
    UCHAR OutFrame[256];
    rsna_size OutLen = sizeof(OutFrame);
    RSNA_STATE NewState = RSNA_STATE_IDLE;
    RSNA_STATUS Error = RSNA_OK;
    ULONG Generation = 0;
    BOOLEAN QueueInstall = FALSE;
    BOOLEAN QueueGroupInstall = FALSE;
    BOOLEAN DeferReply = FALSE;
    BOOLEAN ReplyUnencrypted = TRUE;
    NDIS_STATUS SendStatus;

    UNREFERENCED_PARAMETER(DstMac);

    RtlZeroMemory(OutFrame, sizeof(OutFrame));

    /* No supplicant armed (open network, or PSK not seeded): consume the EAPOL
     * frame anyway so it never leaks up to TCP/IP as bogus Ethernet. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->Initialised)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return TRUE;
    }

    /* The AP (authenticator) MAC is the frame source. */
    RsnaSetApAddr(&Sup->Ctx, SrcMac);

    NewState = RsnaRxEapol(&Sup->Ctx, Eapol, Length, OutFrame, &OutLen);
    Error = RsnaLastError(&Sup->Ctx);
    if (NewState == RSNA_STATE_COMPLETED &&
        !Sup->KeysInstalled && Sup->InstallQueued == 0)
    {
        Sup->InstallQueued = 1;
        Generation = Sup->Generation;
        QueueInstall = TRUE;
    }
    if (NewState == RSNA_STATE_COMPLETED && Sup->KeysInstalled &&
        !Sup->GroupInstallQueued &&
        Sup->Ctx.keys.groupSerial != Sup->InstalledGroupSerial)
    {
        Sup->GroupInstallQueued = TRUE;
        Generation = Sup->Generation;
        QueueGroupInstall = TRUE;
    }
    DeferReply = (NewState == RSNA_STATE_COMPLETED &&
                  !Sup->KeysInstalled);
    if (NewState == RSNA_STATE_COMPLETED && Sup->KeysInstalled &&
        Length >= 7 && !(Eapol[6] & 0x08))
        ReplyUnencrypted = FALSE;
    NdisReleaseSpinLock(&Msm->Lock);

    DPRINT1("NWIFI: sup eapol in %lu out %lu state %d err %d\n",
            Length, (ULONG)OutLen, NewState, Error);
    if (OutLen != 0 && !DeferReply)
    {
        SendStatus = NwifiInjectL2Frame(Msm->Adapter,
                                        SrcMac,
                                        EAPOL_ETHERTYPE,
                                        OutFrame,
                                        (ULONG)OutLen,
                                        ReplyUnencrypted);
        if (SendStatus != NDIS_STATUS_SUCCESS)
        {
            NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                                   NwifiNotifyConnectComplete,
                                   SendStatus);
        }
    }

    if (QueueInstall)
    {
        Context = (PNWIFI_INSTALL_KEYS_CONTEXT)
            NwifiAllocate(sizeof(*Context));
        if (Context != NULL)
        {
            if (OutLen == 0 || OutLen > sizeof(Context->Eapol))
            {
                NwifiFree(Context);
                Context = NULL;
            }
        }
        if (Context != NULL)
        {
            Context->Msm = Msm;
            Context->Generation = Generation;
            RtlCopyMemory(Context->PeerMac, SrcMac, sizeof(Context->PeerMac));
            Context->EapolLength = (ULONG)OutLen;
            RtlCopyMemory(Context->Eapol, OutFrame, Context->EapolLength);
        }
        if (Context == NULL ||
            !NwifiMsmQueueWorkItem(Msm,
                                   NwifiSupplicantInstallKeysWorker,
                                   Context))
        {
            NwifiFree(Context);
            NdisAcquireSpinLock(&Msm->Lock);
            Sup = Msm->Supplicant;
            if (Sup != NULL && Sup->Generation == Generation)
            {
                Sup->InstallQueued = 0;
            }
            NdisReleaseSpinLock(&Msm->Lock);
            NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                                   NwifiNotifyConnectComplete,
                                   NDIS_STATUS_RESOURCES);
        }
    }
    else if (QueueGroupInstall)
    {
        Context = (PNWIFI_INSTALL_KEYS_CONTEXT)NwifiAllocate(sizeof(*Context));
        if (Context != NULL)
        {
            Context->Msm = Msm;
            Context->Generation = Generation;
            Context->EapolLength = 0;
        }
        if (Context == NULL ||
            !NwifiMsmQueueWorkItem(Msm, NwifiSupplicantInstallGroupKeysWorker, Context))
        {
            NwifiFree(Context);
            NdisAcquireSpinLock(&Msm->Lock);
            Sup = Msm->Supplicant;
            if (Sup != NULL && Sup->Generation == Generation)
                Sup->GroupInstallQueued = FALSE;
            NdisReleaseSpinLock(&Msm->Lock);
        }
    }
    else if (NewState == RSNA_STATE_FAILED)
    {
        DPRINT1("NWIFI: 4-way handshake failed (err %d)\n", Error);
        NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete, NDIS_STATUS_FAILURE);
    }

    RtlSecureZeroMemory(OutFrame, sizeof(OutFrame));
    return TRUE;        /* EAPOL frames are always consumed by the supplicant. */
}
