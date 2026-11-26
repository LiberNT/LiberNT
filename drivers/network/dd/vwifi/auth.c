/*
 * PROJECT:     LiberNT Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Access point side of the WPA2-PSK 4-way handshake for the secured test network
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vwifi.h"
#include <rsna_supplicant.h>
#include <rsna_eapol.h>
#include <rsna_hmac.h>
#include <rsna_kdf.h>
#include <rsna_keywrap.h>
#include <debug.h>

#define VWIFI_80211_HEADER_LENGTH   24
#define VWIFI_SNAP_LENGTH           8
#define VWIFI_EAPOL_OFFSET          (VWIFI_80211_HEADER_LENGTH + VWIFI_SNAP_LENGTH)
#define VWIFI_FC0_DATA              0x08
#define VWIFI_FC1_TODS              0x01
#define VWIFI_FC1_FROMDS            0x02
#define VWIFI_FC1_DIR_MASK          0x03
#define VWIFI_REASON_4WAY_TIMEOUT   15
#define VWIFI_GTK_LENGTH            16
#define VWIFI_GTK_KEY_ID            1
#define VWIFI_KEY_DATA_PLAIN        48

C_ASSERT(VWIFI_PMK_LENGTH == RSNA_PMK_LEN);
C_ASSERT(VWIFI_NONCE_LENGTH == RSNA_NONCE_LEN);
C_ASSERT(VWIFI_PTK_LENGTH == RSNA_PTK_CCMP_LEN);
C_ASSERT(2 + VWIFI_RSN_IE_BODY_LENGTH + 2 + 4 + GTK_KDE_HDR_LEN + VWIFI_GTK_LENGTH <= VWIFI_KEY_DATA_PLAIN);
C_ASSERT(VWIFI_KEY_DATA_PLAIN % 8 == 0);
C_ASSERT(EAPOL_KEY_FRAME_FIXED_LEN + VWIFI_KEY_DATA_PLAIN + 8 <= VWIFI_MAX_EAPOL);

static const UCHAR VWifiEapolSnap[VWIFI_SNAP_LENGTH] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };

static
USHORT
VWifiGetBe16(
    IN const UCHAR *Bytes)
{
    return (USHORT)((Bytes[0] << 8) | Bytes[1]);
}

static
VOID
VWifiPutBe16(
    OUT PUCHAR Bytes,
    IN USHORT Value)
{
    Bytes[0] = (UCHAR)(Value >> 8);
    Bytes[1] = (UCHAR)Value;
}

static
ULONGLONG
VWifiGetBe64(
    IN const UCHAR *Bytes)
{
    ULONGLONG Value = 0;
    ULONG Index;

    for (Index = 0; Index < 8; Index++)
        Value = (Value << 8) | Bytes[Index];
    return Value;
}

static
VOID
VWifiPutBe64(
    OUT PUCHAR Bytes,
    IN ULONGLONG Value)
{
    ULONG Index;

    for (Index = 0; Index < 8; Index++)
        Bytes[Index] = (UCHAR)(Value >> (56 - 8 * Index));
}

static
PVWIFI_FAKE_BSS
VWifiAuthBssLocked(
    IN PVWIFI_ADAPTER Adapter)
{
    PVWIFI_FAKE_BSS Bss;

    if (Adapter->ConnState != VWifiConnected || Adapter->ConnectedBssIndex >= Adapter->BssCount)
        return NULL;
    Bss = &Adapter->Bss[Adapter->ConnectedBssIndex];
    return Bss->Passphrase != NULL ? Bss : NULL;
}

VOID
VWifiAuthInitBss(
    IN OUT PVWIFI_FAKE_BSS Bss)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Bss->Passphrase != NULL);
    ASSERT(Bss->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK);

    Bss->PmkValid = RsnaPbkdf2Sha1((const rsna_u8 *)Bss->Passphrase, strlen(Bss->Passphrase),
                                   Bss->Ssid, Bss->SsidLength, 4096, Bss->Pmk, sizeof(Bss->Pmk)) == 0;
    ASSERT(Bss->PmkValid);
}

/* Builds an EAPOL-Key frame; with a KCK, the MIC is computed over the frame with the MIC field zeroed. */
static
ULONG
VWifiAuthBuildKey(
    OUT PUCHAR Eapol,
    IN USHORT KeyInfo,
    IN ULONGLONG Replay,
    IN const UCHAR *Nonce,
    IN const UCHAR *KeyData,
    IN ULONG KeyDataLength,
    IN const UCHAR *Kck)
{
    ULONG BodyLength = KEYDESC_FIXED_LEN + KeyDataLength;
    ULONG Length = EAPOL_HDR_LEN + BodyLength;
    PUCHAR Body = Eapol + EAPOL_HDR_LEN;
    UCHAR Mic[RSNA_SHA1_DIGEST_LEN];

    ASSERT(Length <= VWIFI_MAX_EAPOL);

    RtlZeroMemory(Eapol, Length);
    Eapol[EAPOL_OFF_VERSION] = EAPOL_VERSION_2004;
    Eapol[EAPOL_OFF_TYPE] = EAPOL_TYPE_KEY;
    VWifiPutBe16(&Eapol[EAPOL_OFF_LENGTH], (USHORT)BodyLength);
    Body[KEYDESC_OFF_TYPE] = EAPOL_KEY_DESC_RSN;
    VWifiPutBe16(&Body[KEYDESC_OFF_KEYINFO], KeyInfo);
    VWifiPutBe16(&Body[KEYDESC_OFF_KEYLEN], RSNA_TK_CCMP_LEN);
    VWifiPutBe64(&Body[KEYDESC_OFF_REPLAY], Replay);
    RtlCopyMemory(&Body[KEYDESC_OFF_NONCE], Nonce, KEYDESC_NONCE_LEN);
    VWifiPutBe16(&Body[KEYDESC_OFF_DATALEN], (USHORT)KeyDataLength);
    if (KeyDataLength != 0)
        RtlCopyMemory(&Body[KEYDESC_OFF_DATA], KeyData, KeyDataLength);

    if (Kck != NULL)
    {
        ASSERT(KeyInfo & KEYINFO_KEY_MIC);
        RsnaHmacSha1(Kck, RSNA_KCK_LEN, Eapol, Length, Mic);
        RtlCopyMemory(&Body[KEYDESC_OFF_MIC], Mic, KEYDESC_MIC_LEN);
        RtlSecureZeroMemory(Mic, sizeof(Mic));
    }
    return Length;
}

static
BOOLEAN
VWifiAuthMicValid(
    IN const UCHAR *Kck,
    IN const UCHAR *Eapol,
    IN ULONG Length)
{
    UCHAR Copy[VWIFI_MAX_EAPOL];
    UCHAR Mic[RSNA_SHA1_DIGEST_LEN];
    BOOLEAN Valid;

    ASSERT(Length >= EAPOL_KEY_FRAME_FIXED_LEN);
    ASSERT(Length <= sizeof(Copy));

    RtlCopyMemory(Copy, Eapol, Length);
    RtlZeroMemory(&Copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], KEYDESC_MIC_LEN);
    RsnaHmacSha1(Kck, RSNA_KCK_LEN, Copy, Length, Mic);
    Valid = RtlCompareMemory(Mic, &Eapol[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], KEYDESC_MIC_LEN) == KEYDESC_MIC_LEN;
    RtlSecureZeroMemory(Mic, sizeof(Mic));
    RtlSecureZeroMemory(Copy, sizeof(Copy));
    return Valid;
}

/* Hands an EAPOL frame from the access point to the station as a received FromDS data frame. */
static
VOID
VWifiAuthIndicate(
    IN PVWIFI_ADAPTER Adapter,
    IN PVWIFI_FAKE_BSS Bss,
    IN const UCHAR *Eapol,
    IN ULONG Length)
{
    DOT11_EXTSTA_RECV_CONTEXT RecvContext;
    ULONG FrameLength = VWIFI_EAPOL_OFFSET + Length;
    PNET_BUFFER_LIST Nbl;
    PUCHAR Frame;
    PMDL Mdl;
    USHORT Sequence;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Length <= VWIFI_MAX_EAPOL);

    if (!Adapter->DataPathRunning || Adapter->Halting || Adapter->RxNblPool == NULL)
        return;

    Frame = VWifiAllocate(FrameLength);
    if (Frame == NULL)
        return;

    Frame[0] = VWIFI_FC0_DATA;
    Frame[1] = VWIFI_FC1_FROMDS;
    RtlCopyMemory(&Frame[4], Adapter->CurrentAddress, VWIFI_ADDRESS_LENGTH);
    RtlCopyMemory(&Frame[10], Bss->Bssid, VWIFI_ADDRESS_LENGTH);
    RtlCopyMemory(&Frame[16], Bss->Bssid, VWIFI_ADDRESS_LENGTH);
    Sequence = (USHORT)((Adapter->Auth.Sequence++ & 0x0FFF) << 4);
    Frame[22] = (UCHAR)Sequence;
    Frame[23] = (UCHAR)(Sequence >> 8);
    RtlCopyMemory(&Frame[VWIFI_80211_HEADER_LENGTH], VWifiEapolSnap, VWIFI_SNAP_LENGTH);
    RtlCopyMemory(&Frame[VWIFI_EAPOL_OFFSET], Eapol, Length);

    Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Frame, FrameLength);
    if (Mdl == NULL)
    {
        VWifiFree(Frame);
        return;
    }
    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Mdl, 0, FrameLength);
    if (Nbl == NULL)
    {
        NdisFreeMdl(Mdl);
        VWifiFree(Frame);
        return;
    }

    RtlZeroMemory(&RecvContext, sizeof(RecvContext));
    RecvContext.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    RecvContext.Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    RecvContext.Header.Size = sizeof(RecvContext);
    RecvContext.uPhyId = 0;
    RecvContext.uChCenterFrequency = Bss->ChCenterFrequency;
    RecvContext.lRSSI = Bss->Rssi;
    RecvContext.ucDataRate = 108;
    RecvContext.ullTimestamp = KeQueryInterruptTime();
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = &RecvContext;
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;

    Adapter->RxOkCount++;
    Adapter->RxBytes += FrameLength;
    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle, Nbl, NDIS_DEFAULT_PORT_NUMBER, 1,
                                       NDIS_RECEIVE_FLAGS_RESOURCES);

    NdisFreeNetBufferList(Nbl);
    NdisFreeMdl(Mdl);
    VWifiFree(Frame);
}

static
VOID
VWifiAuthArmRetry(
    IN PVWIFI_ADAPTER Adapter)
{
    LARGE_INTEGER Due;

    Due.QuadPart = Int32x32To64(VWIFI_AUTH_RETRY_MS, -10000);
    NdisSetTimerObject(Adapter->AuthTimer, Due, 0, NULL);
}

VOID
VWifiAuthStart(
    IN PVWIFI_ADAPTER Adapter)
{
    PVWIFI_FAKE_BSS Bss;
    LARGE_INTEGER Counter;
    UCHAR Seed[VWIFI_ADDRESS_LENGTH + sizeof(Counter) + sizeof(ULONGLONG)];

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Bss = VWifiAuthBssLocked(Adapter);
    if (Bss != NULL)
    {
        ASSERT(Bss->PmkValid);
        Adapter->Auth.State = VWifiAuthWaitM2;
        Adapter->Auth.M1Count = 0;
        Adapter->Auth.StaEapolLength = 0;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (Bss == NULL)
        return;

    Counter = KeQueryPerformanceCounter(NULL);
    RtlCopyMemory(Seed, Adapter->CurrentAddress, VWIFI_ADDRESS_LENGTH);
    RtlCopyMemory(Seed + VWIFI_ADDRESS_LENGTH, &Counter, sizeof(Counter));
    RtlCopyMemory(Seed + VWIFI_ADDRESS_LENGTH + sizeof(Counter), &Adapter->Auth.ReplayCounter, sizeof(ULONGLONG));
    RsnaPrf(Bss->Pmk, sizeof(Bss->Pmk), "Init Counter", Seed, sizeof(Seed), Adapter->Auth.ANonce,
            VWIFI_NONCE_LENGTH * 8);

    VWifiAuthSendM1(Adapter);
}

VOID
VWifiAuthStop(
    IN PVWIFI_ADAPTER Adapter)
{
    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Auth.State = VWifiAuthIdle;
    Adapter->Auth.StaEapolLength = 0;
    NdisReleaseSpinLock(&Adapter->Lock);
    if (Adapter->AuthTimer != NULL)
        NdisCancelTimerObject(Adapter->AuthTimer);
    RtlSecureZeroMemory(Adapter->Auth.Ptk, sizeof(Adapter->Auth.Ptk));
}

/* Sends message 1, or deauthenticates the station once every message 1 went unanswered by a valid message 2. */
VOID
VWifiAuthSendM1(
    IN PVWIFI_ADAPTER Adapter)
{
    UCHAR Eapol[EAPOL_KEY_FRAME_FIXED_LEN];
    PVWIFI_FAKE_BSS Bss;
    ULONGLONG Replay = 0;
    BOOLEAN GiveUp = FALSE;
    ULONG Length;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Bss = VWifiAuthBssLocked(Adapter);
    if (Bss == NULL || Adapter->Auth.State != VWifiAuthWaitM2)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return;
    }
    ASSERT(Adapter->Auth.M1Count <= VWIFI_AUTH_M1_ATTEMPTS);
    if (Adapter->Auth.M1Count == VWIFI_AUTH_M1_ATTEMPTS)
    {
        Adapter->Auth.State = VWifiAuthIdle;
        GiveUp = TRUE;
    }
    else
    {
        Adapter->Auth.M1Count++;
        Replay = ++Adapter->Auth.ReplayCounter;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (GiveUp)
    {
        DPRINT1("VWIFI: no valid message 2, deauthenticating the station\n");
        VWifiIndicateDisassociation(Adapter, DOT11_ASSOC_STATUS_PEER_DEAUTHENTICATED_START + VWIFI_REASON_4WAY_TIMEOUT);
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ConnState = VWifiDisconnected;
        NdisReleaseSpinLock(&Adapter->Lock);
        VWifiIndicateLinkState(Adapter, FALSE);
        return;
    }

    Length = VWifiAuthBuildKey(Eapol, KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK, Replay,
                               Adapter->Auth.ANonce, NULL, 0, NULL);
    ASSERT(Length == sizeof(Eapol));
    VWifiAuthIndicate(Adapter, Bss, Eapol, Length);
    VWifiAuthArmRetry(Adapter);
}

static
VOID
VWifiAuthSendM3(
    IN PVWIFI_ADAPTER Adapter,
    IN PVWIFI_FAKE_BSS Bss,
    IN ULONGLONG Replay)
{
    UCHAR Plain[VWIFI_KEY_DATA_PLAIN];
    UCHAR Wrapped[VWIFI_KEY_DATA_PLAIN + 8];
    UCHAR Gtk[VWIFI_GTK_LENGTH];
    UCHAR Eapol[VWIFI_MAX_EAPOL];
    ULONG Offset = 0;
    ULONG Length;

    RsnaPrf(Bss->Pmk, sizeof(Bss->Pmk), "Group key expansion", Adapter->Auth.ANonce, VWIFI_NONCE_LENGTH, Gtk,
            sizeof(Gtk) * 8);

    RtlZeroMemory(Plain, sizeof(Plain));
    Plain[Offset++] = VWIFI_IE_RSN;
    Plain[Offset++] = VWIFI_RSN_IE_BODY_LENGTH;
    RtlCopyMemory(&Plain[Offset], VWifiRsnPskCcmp, VWIFI_RSN_IE_BODY_LENGTH);
    Offset += VWIFI_RSN_IE_BODY_LENGTH;
    Plain[Offset++] = KDE_IE_VENDOR;
    Plain[Offset++] = 4 + GTK_KDE_HDR_LEN + VWIFI_GTK_LENGTH;
    Plain[Offset++] = KDE_OUI_0;
    Plain[Offset++] = KDE_OUI_1;
    Plain[Offset++] = KDE_OUI_2;
    Plain[Offset++] = KDE_TYPE_GTK;
    Plain[Offset++] = VWIFI_GTK_KEY_ID;
    Plain[Offset++] = 0;
    RtlCopyMemory(&Plain[Offset], Gtk, sizeof(Gtk));
    Offset += sizeof(Gtk);
    ASSERT(Offset < sizeof(Plain));
    Plain[Offset] = KDE_IE_VENDOR;

    if (RsnaAesWrap(Adapter->Auth.Ptk + RSNA_KCK_LEN, 128, Plain, sizeof(Plain) / 8, Wrapped) != 0)
    {
        DPRINT1("VWIFI: wrapping the group key failed\n");
        RtlSecureZeroMemory(Plain, sizeof(Plain));
        RtlSecureZeroMemory(Gtk, sizeof(Gtk));
        return;
    }

    Length = VWifiAuthBuildKey(Eapol,
                               KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_INSTALL | KEYINFO_KEY_ACK |
                               KEYINFO_KEY_MIC | KEYINFO_SECURE | KEYINFO_ENCRYPTED_DATA,
                               Replay, Adapter->Auth.ANonce, Wrapped, sizeof(Wrapped), Adapter->Auth.Ptk);
    VWifiAuthIndicate(Adapter, Bss, Eapol, Length);

    RtlSecureZeroMemory(Plain, sizeof(Plain));
    RtlSecureZeroMemory(Gtk, sizeof(Gtk));
    RtlSecureZeroMemory(Eapol, sizeof(Eapol));
}

/* Runs the access point on the EAPOL-Key frame the station sent last: message 2 or message 4. */
VOID
VWifiAuthProcessEapol(
    IN PVWIFI_ADAPTER Adapter)
{
    UCHAR Eapol[VWIFI_MAX_EAPOL];
    UCHAR Ptk[VWIFI_PTK_LENGTH];
    VWIFI_AUTH_STATE State;
    PVWIFI_FAKE_BSS Bss;
    ULONGLONG Replay;
    ULONG Length;
    ULONG BodyLength;
    PUCHAR Body;
    USHORT KeyInfo;
    BOOLEAN SendM3 = FALSE;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Bss = VWifiAuthBssLocked(Adapter);
    Length = Adapter->Auth.StaEapolLength;
    ASSERT(Length <= sizeof(Eapol));
    RtlCopyMemory(Eapol, Adapter->Auth.StaEapol, Length);
    Adapter->Auth.StaEapolLength = 0;
    State = Adapter->Auth.State;
    Replay = Adapter->Auth.ReplayCounter;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Bss == NULL || Length < EAPOL_KEY_FRAME_FIXED_LEN || Eapol[EAPOL_OFF_TYPE] != EAPOL_TYPE_KEY)
        return;
    BodyLength = VWifiGetBe16(&Eapol[EAPOL_OFF_LENGTH]);
    if (BodyLength < KEYDESC_FIXED_LEN || BodyLength > Length - EAPOL_HDR_LEN)
        return;
    Length = EAPOL_HDR_LEN + BodyLength;
    Body = Eapol + EAPOL_HDR_LEN;
    KeyInfo = VWifiGetBe16(&Body[KEYDESC_OFF_KEYINFO]);
    if (Body[KEYDESC_OFF_TYPE] != EAPOL_KEY_DESC_RSN ||
        (KeyInfo & KEYINFO_VERSION_MASK) != KEYINFO_VER_HMAC_SHA1_AES ||
        (KeyInfo & (KEYINFO_KEY_TYPE | KEYINFO_KEY_MIC | KEYINFO_KEY_ACK)) != (KEYINFO_KEY_TYPE | KEYINFO_KEY_MIC) ||
        VWifiGetBe64(&Body[KEYDESC_OFF_REPLAY]) != Replay ||
        KEYDESC_FIXED_LEN + (ULONG)VWifiGetBe16(&Body[KEYDESC_OFF_DATALEN]) > BodyLength)
    {
        return;
    }

    if (State == VWifiAuthWaitM2 && !(KeyInfo & KEYINFO_SECURE))
    {
        RsnaDerivePtk(Bss->Pmk, Bss->Bssid, Adapter->CurrentAddress, Adapter->Auth.ANonce, &Body[KEYDESC_OFF_NONCE],
                      Ptk, sizeof(Ptk));
        if (!VWifiAuthMicValid(Ptk, Eapol, Length))
        {
            DPRINT1("VWIFI: message 2 MIC mismatch, discarded\n");
            RtlSecureZeroMemory(Ptk, sizeof(Ptk));
            return;
        }

        NdisAcquireSpinLock(&Adapter->Lock);
        if (Adapter->Auth.State == VWifiAuthWaitM2 && Adapter->Auth.ReplayCounter == Replay)
        {
            Adapter->Auth.State = VWifiAuthWaitM4;
            Replay = ++Adapter->Auth.ReplayCounter;
            RtlCopyMemory(Adapter->Auth.Ptk, Ptk, sizeof(Ptk));
            SendM3 = TRUE;
        }
        NdisReleaseSpinLock(&Adapter->Lock);
        RtlSecureZeroMemory(Ptk, sizeof(Ptk));

        if (SendM3)
        {
            NdisCancelTimerObject(Adapter->AuthTimer);
            VWifiAuthSendM3(Adapter, Bss, Replay);
        }
    }
    else if (State == VWifiAuthWaitM4 && (KeyInfo & KEYINFO_SECURE))
    {
        if (!VWifiAuthMicValid(Adapter->Auth.Ptk, Eapol, Length))
        {
            DPRINT1("VWIFI: message 4 MIC mismatch, discarded\n");
            return;
        }
        NdisAcquireSpinLock(&Adapter->Lock);
        if (Adapter->Auth.State == VWifiAuthWaitM4)
            Adapter->Auth.State = VWifiAuthDone;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("VWIFI: 4-way handshake complete\n");
    }
}

/* Send path: keeps a copy of an EAPOL frame the station addressed to the access point for the engine. */
BOOLEAN
VWifiAuthCaptureTx(
    IN PVWIFI_ADAPTER Adapter,
    IN PNET_BUFFER NetBuffer)
{
    UCHAR Storage[VWIFI_EAPOL_OFFSET + VWIFI_MAX_EAPOL];
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    PVWIFI_FAKE_BSS Bss;
    PUCHAR Frame;
    BOOLEAN Captured = FALSE;

    if (Length < VWIFI_EAPOL_OFFSET + EAPOL_KEY_FRAME_FIXED_LEN || Length > sizeof(Storage))
        return FALSE;
    Frame = NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0);
    if (Frame == NULL ||
        Frame[0] != VWIFI_FC0_DATA ||
        (Frame[1] & VWIFI_FC1_DIR_MASK) != VWIFI_FC1_TODS ||
        RtlCompareMemory(&Frame[VWIFI_80211_HEADER_LENGTH], VWifiEapolSnap, VWIFI_SNAP_LENGTH) != VWIFI_SNAP_LENGTH)
    {
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Bss = VWifiAuthBssLocked(Adapter);
    if (Bss != NULL &&
        (Adapter->Auth.State == VWifiAuthWaitM2 || Adapter->Auth.State == VWifiAuthWaitM4) &&
        RtlCompareMemory(&Frame[4], Bss->Bssid, VWIFI_ADDRESS_LENGTH) == VWIFI_ADDRESS_LENGTH &&
        RtlCompareMemory(&Frame[10], Adapter->CurrentAddress, VWIFI_ADDRESS_LENGTH) == VWIFI_ADDRESS_LENGTH)
    {
        Adapter->Auth.StaEapolLength = Length - VWIFI_EAPOL_OFFSET;
        RtlCopyMemory(Adapter->Auth.StaEapol, &Frame[VWIFI_EAPOL_OFFSET], Adapter->Auth.StaEapolLength);
        Captured = TRUE;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Captured)
        VWifiScheduleJob(Adapter, VWifiJobAuthRx, 0);
    return Captured;
}

VOID
NTAPI
VWifiAuthTimerDpc(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (!Adapter->Halting)
        VWifiScheduleJob(Adapter, VWifiJobAuthM1, 0);
}
