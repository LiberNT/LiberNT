/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise pairwise and group rekey routing in the NWIFI wrapper
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <nwifi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned Failures, PairwiseInstalls, GroupInstalls, LinkUps, Frames, Notifications;
static NDIS_IO_WORKITEM_ROUTINE Worker;
static PVOID WorkerContext;
static UCHAR LastFrame[512];
static ULONG LastFrameLength;

static void Check(const char *Name, BOOLEAN Passed)
{
    printf("%s %s\n", Passed ? "PASS" : "FAIL", Name);
    if (!Passed)
        ++Failures;
}

static VOID TestAcquire(PNDIS_SPIN_LOCK Lock) { (void)Lock; }
static VOID TestRelease(PNDIS_SPIN_LOCK Lock) { (void)Lock; }
static PVOID TestAllocate(SIZE_T Size) { return calloc(1, Size); }
static VOID TestFree(PVOID Buffer) { free(Buffer); }

static BOOLEAN TestQueue(PNWIFI_MSM Msm, NDIS_IO_WORKITEM_ROUTINE Routine, PVOID Context)
{
    (void)Msm;
    Check("worker.single_queued", Worker == NULL);
    if (Worker != NULL)
        return FALSE;
    Worker = Routine;
    WorkerContext = Context;
    return TRUE;
}

static VOID TestComplete(PNWIFI_MSM Msm, NDIS_HANDLE Item) { (void)Msm; (void)Item; }
static VOID TestLinkUp(PNWIFI_MSM Msm, BOOLEAN Secure) { (void)Msm; Check("link.secure", Secure); ++LinkUps; }
BOOLEAN NwifiMsmBssUsesSaeH2e(PNWIFI_MSM Msm, const UCHAR *Bssid)
{
    (void)Msm; (void)Bssid;
    Check("sae.unexpected_path", FALSE);
    return FALSE;
}
static VOID TestNotify(ULONG Index, NWIFI_NOTIFICATION_CODE Code, NDIS_STATUS Status)
{
    (void)Index; (void)Code; (void)Status;
    ++Notifications;
}

static NDIS_STATUS TestRequest(PNWIFI_ADAPTER Adapter, NDIS_REQUEST_TYPE Type,
                              NDIS_OID Oid, PVOID Buffer, ULONG Length, PULONG Processed)
{
    (void)Adapter; (void)Buffer; (void)Length; (void)Processed;
    Check("key.set_request", Type == NdisRequestSetInformation);
    if (Oid == OID_DOT11_CIPHER_KEY_MAPPING_KEY)
        ++PairwiseInstalls;
    else if (Oid == OID_DOT11_CIPHER_DEFAULT_KEY)
        ++GroupInstalls;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS TestInject(PNWIFI_ADAPTER Adapter, PUCHAR DestMac, USHORT EtherType,
                             PUCHAR Payload, ULONG Length, BOOLEAN Unencrypted)
{
    (void)Adapter; (void)DestMac; (void)Unencrypted;
    Check("frame.eapol", EtherType == 0x888E && Length <= sizeof(LastFrame));
    if (Length <= sizeof(LastFrame))
    {
        memcpy(LastFrame, Payload, Length);
        LastFrameLength = Length;
    }
    ++Frames;
    return NDIS_STATUS_SUCCESS;
}

#undef NdisAcquireSpinLock
#undef NdisReleaseSpinLock
#define NdisAcquireSpinLock TestAcquire
#define NdisReleaseSpinLock TestRelease
#define NwifiAllocate TestAllocate
#define NwifiFree TestFree
#define NwifiMsmQueueWorkItem TestQueue
#define NwifiMsmCompleteWorkItem TestComplete
#define NwifiMsmLinkUp TestLinkUp
#define NwifiQueueNotification TestNotify
#define NwifiProtocolDoRequest TestRequest
#define NwifiInjectL2Frame TestInject
#include "../../../../drivers/network/nwifi/supplicant.c"

#include <rsna_eapol.h>
#include <rsna_hmac.h>
#include <rsna_keywrap.h>

static void DrainWorker(void)
{
    NDIS_IO_WORKITEM_ROUTINE Routine = Worker;
    PVOID Context = WorkerContext;
    Worker = NULL;
    WorkerContext = NULL;
    if (Routine != NULL)
        Routine(Context, NULL);
}

static ULONG BuildFrame(UCHAR *Frame, USHORT Info, UCHAR Replay, const UCHAR *Nonce,
                        const UCHAR *Kck, const UCHAR *Data, ULONG DataLength)
{
    ULONG BodyLength = KEYDESC_FIXED_LEN + DataLength;
    ULONG Length = EAPOL_HDR_LEN + BodyLength;
    PUCHAR Body = Frame + EAPOL_HDR_LEN;
    UCHAR Mic[RSNA_SHA1_DIGEST_LEN];

    memset(Frame, 0, Length);
    Frame[EAPOL_OFF_VERSION] = EAPOL_VERSION_2004;
    Frame[EAPOL_OFF_TYPE] = EAPOL_TYPE_KEY;
    Frame[EAPOL_OFF_LENGTH] = (UCHAR)(BodyLength >> 8);
    Frame[EAPOL_OFF_LENGTH + 1] = (UCHAR)BodyLength;
    Body[KEYDESC_OFF_TYPE] = EAPOL_KEY_DESC_RSN;
    Body[KEYDESC_OFF_KEYINFO] = (UCHAR)(Info >> 8);
    Body[KEYDESC_OFF_KEYINFO + 1] = (UCHAR)Info;
    Body[KEYDESC_OFF_KEYLEN + 1] = 16;
    Body[KEYDESC_OFF_REPLAY + 7] = Replay;
    if (Nonce != NULL)
        memcpy(Body + KEYDESC_OFF_NONCE, Nonce, RSNA_NONCE_LEN);
    Body[KEYDESC_OFF_DATALEN] = (UCHAR)(DataLength >> 8);
    Body[KEYDESC_OFF_DATALEN + 1] = (UCHAR)DataLength;
    if (DataLength != 0)
        memcpy(Body + KEYDESC_OFF_DATA, Data, DataLength);
    if (Kck != NULL)
    {
        RsnaHmacSha1(Kck, RSNA_KCK_LEN, Frame, Length, Mic);
        memcpy(Body + KEYDESC_OFF_MIC, Mic, KEYDESC_MIC_LEN);
    }
    return Length;
}

static ULONG BuildMessage3(UCHAR *Frame, UCHAR Replay, UCHAR KeyByte,
                          const UCHAR *Nonce, const UCHAR *Ptk, BOOLEAN Pairwise)
{
    UCHAR Kde[24] = {KDE_IE_VENDOR, 22, KDE_OUI_0, KDE_OUI_1, KDE_OUI_2, KDE_TYPE_GTK, 1, 0};
    UCHAR Wrapped[32];
    USHORT Info = KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_ACK | KEYINFO_KEY_MIC |
                  KEYINFO_SECURE | KEYINFO_ENCRYPTED_DATA;

    memset(Kde + 8, KeyByte, 16);
    RsnaAesWrap(Ptk + RSNA_KCK_LEN, 128, Kde, sizeof(Kde) / 8, Wrapped);
    if (Pairwise)
        Info |= KEYINFO_KEY_TYPE | KEYINFO_INSTALL;
    return BuildFrame(Frame, Info, Replay, Nonce, Ptk, Wrapped, sizeof(Wrapped));
}

int main(void)
{
    NWIFI_ADAPTER Adapter = {0};
    NWIFI_MSM Msm = {0};
    NWIFI_SUPPLICANT Sup = {0};
    UCHAR Ap[6] = {2, 0, 0, 0, 0, 1};
    UCHAR Sta[6] = {2, 0, 0, 0, 0, 2};
    UCHAR Pmk[RSNA_PMK_LEN] = {1};
    UCHAR Nonce[RSNA_NONCE_LEN], Ptk[RSNA_PTK_CCMP_LEN], Snonce[RSNA_NONCE_LEN];
    UCHAR Frame[512], Message3[512];
    ULONG Length, M3Length, Generation, BeforeFrames;

    Msm.Adapter = &Adapter;
    Msm.Supplicant = &Sup;
    memcpy(Msm.CurrentBssid, Ap, sizeof(Ap));
    Sup.Initialised = TRUE;
    Sup.Generation = 1;
    memset(Sup.RandomPool, 0x42, sizeof(Sup.RandomPool));
    Sup.RandomAvailable = sizeof(Sup.RandomPool);
    Check("rsna.init", RsnaInitWithPmk(&Sup.Ctx, (const UCHAR *)"TestNet", 7, Pmk) == RSNA_OK);
    RsnaSetStaAddr(&Sup.Ctx, Sta);
    RsnaSetRandom(&Sup.Ctx, NwifiSupplicantRandom, &Sup);
    NwifiSupplicantSetAssociation(&Msm, Ap, NULL, 0);

    memset(Nonce, 0x10, sizeof(Nonce));
    Length = BuildFrame(Frame, KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                        1, Nonce, NULL, NULL, 0);
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("first.message2", LastFrameLength >= EAPOL_KEY_FRAME_FIXED_LEN);
    RsnaDerivePtk(Pmk, Ap, Sta, Nonce, LastFrame + EAPOL_HDR_LEN + KEYDESC_OFF_NONCE, Ptk, sizeof(Ptk));
    M3Length = BuildMessage3(Message3, 2, 0xA0, Nonce, Ptk, TRUE);
    BeforeFrames = Frames;
    NwifiSupplicantRxEapol(&Msm, Message3, M3Length, Ap, Sta);
    Check("first.full_worker", Worker == NwifiSupplicantInstallKeysWorker);
    Check("first.message4_deferred", Frames == BeforeFrames);
    DrainWorker();
    Check("first.keys_and_link", PairwiseInstalls == 1 && GroupInstalls == 1 && LinkUps == 1 && Sup.KeysInstalled);

    Generation = Sup.Generation;
    NwifiSupplicantSetAssociation(&Msm, Ap, NULL, 0);
    Check("association.invalidates_installed_keys", !Sup.KeysInstalled);
    Check("association.new_generation", Sup.Generation != Generation);
    Generation = Sup.Generation;
    memset(Nonce, 0x50, sizeof(Nonce));
    Length = BuildFrame(Frame, KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                        3, Nonce, NULL, NULL, 0);
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("rekey.invalidates_installed_keys", !Sup.KeysInstalled);
    Check("rekey.same_association_generation", Sup.Generation == Generation);
    Generation = Sup.Generation;
    memcpy(Snonce, LastFrame + EAPOL_HDR_LEN + KEYDESC_OFF_NONCE, sizeof(Snonce));
    Frame[EAPOL_HDR_LEN + KEYDESC_OFF_REPLAY + 7] = 4;
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("retransmit.same_generation", Sup.Generation == Generation);
    Check("retransmit.same_snonce", !memcmp(Snonce, LastFrame + EAPOL_HDR_LEN + KEYDESC_OFF_NONCE, sizeof(Snonce)));
    RsnaDerivePtk(Pmk, Ap, Sta, Nonce, Snonce, Ptk, sizeof(Ptk));
    M3Length = BuildMessage3(Message3, 5, 0xB0, Nonce, Ptk, TRUE);
    BeforeFrames = Frames;
    NwifiSupplicantRxEapol(&Msm, Message3, M3Length, Ap, Sta);
    Check("rekey.full_worker", Worker == NwifiSupplicantInstallKeysWorker);
    Check("rekey.message4_deferred", Frames == BeforeFrames);
    DrainWorker();
    Check("rekey.keys_and_link", PairwiseInstalls == 2 && GroupInstalls == 2 && LinkUps == 2 && Sup.KeysInstalled);

    NwifiSupplicantRxEapol(&Msm, Message3, M3Length, Ap, Sta);
    Check("message3_retransmit.no_reinstall", Worker == NULL && PairwiseInstalls == 2);
    Length = BuildMessage3(Frame, 6, 0xC0, Nonce, Ptk, FALSE);
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("group.group_worker", Worker == NwifiSupplicantInstallGroupKeysWorker);
    Check("group.keeps_pairwise_state", Sup.KeysInstalled && Sup.Generation == Generation);
    DrainWorker();
    Check("group.only_group_installed", PairwiseInstalls == 2 && GroupInstalls == 3 && LinkUps == 2);

    Length = BuildMessage3(Frame, 7, 0xD0, Nonce, Ptk, FALSE);
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("stale.group_queued", Worker == NwifiSupplicantInstallGroupKeysWorker);
    NwifiSupplicantSetAssociation(&Msm, Ap, NULL, 0);
    DrainWorker();
    Check("stale.old_worker_does_not_install", PairwiseInstalls == 2 && GroupInstalls == 3 && LinkUps == 2);

    Length = BuildFrame(Frame, KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                        4, Nonce, NULL, NULL, 0);
    NwifiSupplicantRxEapol(&Msm, Frame, Length, Ap, Sta);
    Check("association.fresh_snonce", memcmp(Snonce, LastFrame + EAPOL_HDR_LEN + KEYDESC_OFF_NONCE, sizeof(Snonce)) != 0);
    NwifiSupplicantRxEapol(&Msm, Message3, M3Length, Ap, Sta);
    Check("association.old_message3_rejected", RsnaLastError(&Sup.Ctx) == RSNA_ERR_MIC && Worker == NULL);
    Check("association.old_keys_not_reinstalled", PairwiseInstalls == 2 && GroupInstalls == 3 && LinkUps == 2);
    Check("notifications.no_failure", Notifications == 0);
    printf("RESULT failures=%u\n", Failures);
    return Failures ? 1 : 0;
}
