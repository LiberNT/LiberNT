/*
 * PROJECT:     LiberNT Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host unit-test harness for the RSNA supplicant library.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Not built by CMake; build and run on the host:
 *     clang -std=c99 -D_RSNA_HOST_TEST -Wall -Wextra \
 *         rsna_sha1.c rsna_sha256.c rsna_md5.c rsna_hmac.c rsna_kdf.c rsna_aes.c \
 *         rsna_keywrap.c rsna_rc4.c rsna_cmac.c rsna_ccmp.c rsna_p256.c rsna_sae.c \
 *         rsna_supplicant.c test_rsna.c \
 *         -o test_rsna && ./test_rsna
 */

#include <stdio.h>
#include <string.h>

#include "rsna_sha1.h"
#include "rsna_md5.h"
#include "rsna_hmac.h"
#include "rsna_kdf.h"
#include "rsna_aes.h"
#include "rsna_keywrap.h"
#include "rsna_rc4.h"
#include "rsna_cmac.h"
#include "rsna_ccmp.h"
#include "rsna_sha256.h"
#include "rsna_sae.h"
#include "rsna_eapol.h"
#include "rsna_supplicant.h"

static int g_pass = 0;
static int g_fail = 0;

static int HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a hex string into bytes.  Returns number of bytes. */
static rsna_size FromHex(const char *hex, rsna_u8 *out, rsna_size outCap)
{
    rsna_size n = 0;
    while (hex[0] && hex[1] && n < outCap)
    {
        int hi, lo;
        while (*hex == ' ' || *hex == '\n') hex++;
        if (!hex[0] || !hex[1]) break;
        hi = HexNibble(hex[0]);
        lo = HexNibble(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (rsna_u8)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

static void PrintHex(const char *label, const rsna_u8 *data, rsna_size len)
{
    rsna_size i;
    printf("    %s: ", label);
    for (i = 0; i < len; i++)
        printf("%02x", data[i]);
    printf("\n");
}

static void Check(const char *name, int ok,
                  const rsna_u8 *got, const rsna_u8 *exp, rsna_size len)
{
    if (ok)
    {
        printf("[ PASS ] %s\n", name);
        g_pass++;
    }
    else
    {
        printf("[ FAIL ] %s\n", name);
        if (got) PrintHex("got     ", got, len);
        if (exp) PrintHex("expected", exp, len);
        g_fail++;
    }
}

static int CheckBytes(const char *name, const rsna_u8 *got, const rsna_u8 *exp, rsna_size len)
{
    int ok = (memcmp(got, exp, len) == 0);
    Check(name, ok, got, exp, len);
    return ok;
}

/* ================================================================== */
/* primitive tests                                                    */
/* ================================================================== */

static void TestSha1(void)
{
    rsna_u8 dig[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 exp[RSNA_SHA1_DIGEST_LEN];

    /* FIPS 180: SHA1("abc") */
    RsnaSha1((const rsna_u8 *)"abc", 3, dig);
    FromHex("a9993e364706816aba3e25717850c26c9cd0d89d", exp, sizeof(exp));
    CheckBytes("SHA1(\"abc\")", dig, exp, sizeof(exp));

    /* SHA1 of the 448-bit message. */
    RsnaSha1((const rsna_u8 *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, dig);
    FromHex("84983e441c3bd26ebaae4aa1f95129e5e54670f1", exp, sizeof(exp));
    CheckBytes("SHA1(2-block message)", dig, exp, sizeof(exp));
}

static void TestMd5(void)
{
    rsna_u8 dig[RSNA_MD5_DIGEST_LEN];
    rsna_u8 exp[RSNA_MD5_DIGEST_LEN];

    /* RFC 1321: MD5("abc") */
    RsnaMd5((const rsna_u8 *)"abc", 3, dig);
    FromHex("900150983cd24fb0d6963f7d28e17f72", exp, sizeof(exp));
    CheckBytes("MD5(\"abc\")", dig, exp, sizeof(exp));

    /* RFC 1321: MD5("message digest") */
    RsnaMd5((const rsna_u8 *)"message digest", 14, dig);
    FromHex("f96b697d7cb7938d525a2f31aaf161d0", exp, sizeof(exp));
    CheckBytes("MD5(\"message digest\")", dig, exp, sizeof(exp));
}

static void TestHmac(void)
{
    rsna_u8 mac[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 exp[RSNA_SHA1_DIGEST_LEN];
    rsna_u8 key[80];

    /* RFC 2202 HMAC-SHA1 test case 1: key=0x0b x20, data="Hi There". */
    memset(key, 0x0b, 20);
    RsnaHmacSha1(key, 20, (const rsna_u8 *)"Hi There", 8, mac);
    FromHex("b617318655057264e28bc0b6fb378c8ef146be00", exp, sizeof(exp));
    CheckBytes("HMAC-SHA1 RFC2202 case 1", mac, exp, sizeof(exp));

    /* RFC 2202 HMAC-SHA1 test case 2: key="Jefe". */
    RsnaHmacSha1((const rsna_u8 *)"Jefe", 4,
                 (const rsna_u8 *)"what do ya want for nothing?", 28, mac);
    FromHex("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", exp, sizeof(exp));
    CheckBytes("HMAC-SHA1 RFC2202 case 2", mac, exp, sizeof(exp));

    /* RFC 2202 HMAC-MD5 test case 2: key="Jefe". */
    {
        rsna_u8 macm[RSNA_MD5_DIGEST_LEN];
        rsna_u8 expm[RSNA_MD5_DIGEST_LEN];
        RsnaHmacMd5((const rsna_u8 *)"Jefe", 4,
                    (const rsna_u8 *)"what do ya want for nothing?", 28, macm);
        FromHex("750c783e6ab0b503eaa86e310a5db738", expm, sizeof(expm));
        CheckBytes("HMAC-MD5 RFC2202 case 2", macm, expm, sizeof(expm));
    }
}

static void TestAes(void)
{
    RSNA_AES_KEY key;
    rsna_u8 ct[16], pt[16];
    rsna_u8 kbuf[32], inbuf[16], exp[16];

    /* FIPS 197 Appendix C.1 AES-128. */
    FromHex("000102030405060708090a0b0c0d0e0f", kbuf, sizeof(kbuf));
    FromHex("00112233445566778899aabbccddeeff", inbuf, sizeof(inbuf));
    FromHex("69c4e0d86a7b0430d8cdb78070b4c55a", exp, sizeof(exp));

    RsnaAesSetEncryptKey(&key, kbuf, 128);
    RsnaAesEncrypt(&key, inbuf, ct);
    CheckBytes("AES-128 encrypt (FIPS 197 C.1)", ct, exp, sizeof(exp));

    RsnaAesSetDecryptKey(&key, kbuf, 128);
    RsnaAesDecrypt(&key, ct, pt);
    CheckBytes("AES-128 decrypt (FIPS 197 C.1)", pt, inbuf, sizeof(inbuf));

    /* FIPS 197 Appendix C.3 AES-256. */
    FromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", kbuf, sizeof(kbuf));
    FromHex("00112233445566778899aabbccddeeff", inbuf, sizeof(inbuf));
    FromHex("8ea2b7ca516745bfeafc49904b496089", exp, sizeof(exp));

    RsnaAesSetEncryptKey(&key, kbuf, 256);
    RsnaAesEncrypt(&key, inbuf, ct);
    CheckBytes("AES-256 encrypt (FIPS 197 C.3)", ct, exp, sizeof(exp));
}

static void TestRc4(void)
{
    RSNA_RC4_CTX rc4;
    rsna_u8 out[9];
    rsna_u8 exp[9];

    /* Well-known RC4 vector: key="Key", plaintext="Plaintext". */
    RsnaRc4Init(&rc4, (const rsna_u8 *)"Key", 3);
    RsnaRc4Crypt(&rc4, (const rsna_u8 *)"Plaintext", out, 9);
    FromHex("bbf316e8d940af0ad3", exp, sizeof(exp));
    CheckBytes("RC4(\"Key\",\"Plaintext\")", out, exp, sizeof(exp));
}

static void TestAesKeyWrap(void)
{
    /* RFC 3394 section 4.1: 128-bit KEK, 128-bit key data. */
    rsna_u8 kek[16], data[16], wrapped[24], expWrap[24], unwrapped[16];

    FromHex("000102030405060708090a0b0c0d0e0f", kek, sizeof(kek));
    FromHex("00112233445566778899aabbccddeeff", data, sizeof(data));
    FromHex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", expWrap, sizeof(expWrap));

    RsnaAesWrap(kek, 128, data, 2, wrapped);
    CheckBytes("AES Key Wrap (RFC 3394 4.1)", wrapped, expWrap, sizeof(expWrap));

    if (RsnaAesUnwrap(kek, 128, wrapped, 2, unwrapped) == 0)
        CheckBytes("AES Key Unwrap (RFC 3394 4.1)", unwrapped, data, sizeof(data));
    else
        Check("AES Key Unwrap (RFC 3394 4.1)", 0, NULL, NULL, 0);

    /* RFC 3394 section 4.6: 256-bit KEK, 256-bit key data. */
    {
        rsna_u8 kek2[32], data2[32], wr2[40], exp2[40], un2[32];
        FromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", kek2, sizeof(kek2));
        FromHex("00112233445566778899aabbccddeeff000102030405060708090a0b0c0d0e0f", data2, sizeof(data2));
        FromHex("28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21", exp2, sizeof(exp2));
        RsnaAesWrap(kek2, 256, data2, 4, wr2);
        CheckBytes("AES Key Wrap (RFC 3394 4.6, 256-bit)", wr2, exp2, sizeof(exp2));
        if (RsnaAesUnwrap(kek2, 256, wr2, 4, un2) == 0)
            CheckBytes("AES Key Unwrap (RFC 3394 4.6, 256-bit)", un2, data2, sizeof(data2));
        else
            Check("AES Key Unwrap (RFC 3394 4.6, 256-bit)", 0, NULL, NULL, 0);

        /* A tampered ciphertext must fail the integrity check. */
        wr2[0] ^= 0x01;
        Check("AES Key Unwrap rejects tampering",
              RsnaAesUnwrap(kek2, 256, wr2, 4, un2) != 0, NULL, NULL, 0);
    }
}

/* ================================================================== */
/* PMK / PTK test vectors                                             */
/* ================================================================== */

static void TestCmac(void)
{
    static const char *Messages[] =
    {
        "",
        "6bc1bee22e409f96e93d7e117393172a",
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411",
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411"
        "e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710"
    };
    static const char *Macs[] =
    {
        "bb1d6929e95937287fa37d129b756746",
        "070a16b46b4d4144f79bdd9dd04a287c",
        "dfa66747de9ae63030ca32611497c827",
        "51f0bebf7e3b9d92fc49741779363cfe"
    };
    rsna_u8 key[16], msg[64], exp[16], mac[16];
    rsna_size msgLen, i;

    FromHex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key));
    for (i = 0; i < 4; i++)
    {
        msgLen = FromHex(Messages[i], msg, sizeof(msg));
        FromHex(Macs[i], exp, sizeof(exp));
        RsnaAesCmac(key, msg, msgLen, mac);
        CheckBytes("AES-CMAC RFC 4493", mac, exp, 16);
    }
}

static void TestCcmpBip(void)
{
    rsna_u8 tk[16], hdr[24], body[64], exp[64], out[64], plain[64], igtk[16], frame[64];
    rsna_size bodyLen, expLen, frameLen;
    rsna_u64 pn = 0;
    rsna_u16 keyId = 0;

    FromHex("404142434445464748494a4b4c4d4e4f", tk, sizeof(tk));
    FromHex("d00000000202020202020404040404040404040404043000", hdr, sizeof(hdr));
    bodyLen = FromHex("0800555500010203040506070809", body, sizeof(body));
    expLen = FromHex("06050020040302011d12456b5b9eb6dfc1d3f8e9c47ec6894fe19d6f84bf", exp, sizeof(exp));
    Check("CCMP management frame encrypt",
          RsnaCcmpEncrypt(tk, hdr, sizeof(hdr), 0x010203040506ULL, 0, body, bodyLen, out) == 0 &&
          expLen == bodyLen + 16 && memcmp(out, exp, expLen) == 0, NULL, NULL, 0);
    Check("CCMP management frame decrypt",
          RsnaCcmpDecrypt(tk, hdr, sizeof(hdr), exp, expLen, plain, &pn) == 0 &&
          pn == 0x010203040506ULL && memcmp(plain, body, bodyLen) == 0, NULL, NULL, 0);
    exp[expLen - 1] ^= 1;
    Check("CCMP rejects a modified MIC",
          RsnaCcmpDecrypt(tk, hdr, sizeof(hdr), exp, expLen, plain, &pn) != 0, NULL, NULL, 0);

    FromHex("606162636465666768696a6b6c6d6e6f", igtk, sizeof(igtk));
    frameLen = FromHex("c0000000ffffffffffff060606060606060606060606310007004c1004000f0e0d0c0b0a024402c64b57c24e",
                       frame, sizeof(frame));
    Check("BIP MMIE verifies",
          RsnaBipVerify(igtk, frame, frameLen, &keyId, &pn) == 0 &&
          keyId == 4 && pn == 0x0a0b0c0d0e0fULL, NULL, NULL, 0);
    frame[24] ^= 1;
    Check("BIP rejects a modified body",
          RsnaBipVerify(igtk, frame, frameLen, &keyId, &pn) != 0, NULL, NULL, 0);
}

static void TestPmk(void)
{
    rsna_u8 pmk[RSNA_PMK_LEN];
    rsna_u8 exp[RSNA_PMK_LEN];

    /* IEEE 802.11i Annex H.4: passphrase "password", SSID "IEEE". */
    RsnaPbkdf2Sha1((const rsna_u8 *)"password", 8,
                   (const rsna_u8 *)"IEEE", 4,
                   4096, pmk, RSNA_PMK_LEN);
    FromHex("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e",
            exp, sizeof(exp));
    CheckBytes("PMK PBKDF2 (IEEE 802.11i: \"password\"/\"IEEE\")", pmk, exp, sizeof(exp));

    /* RFC 6070 PBKDF2-HMAC-SHA1 vectors: P="password", S="salt", dkLen=20. */
    {
        rsna_u8 dk[20];
        rsna_u8 e2[20];
        RsnaPbkdf2Sha1((const rsna_u8 *)"password", 8,
                       (const rsna_u8 *)"salt", 4, 1, dk, sizeof(dk));
        FromHex("0c60c80f961f0e71f3a9b524af6012062fe037a6", e2, sizeof(e2));
        CheckBytes("PBKDF2 RFC 6070 (c=1)", dk, e2, sizeof(e2));

        /* RFC 6070: c=4096, dkLen=20 -> 4b007901b765489abead49d926f721d065a429c1 */
        RsnaPbkdf2Sha1((const rsna_u8 *)"password", 8,
                       (const rsna_u8 *)"salt", 4, 4096, dk, sizeof(dk));
        FromHex("4b007901b765489abead49d926f721d065a429c1", e2, sizeof(e2));
        CheckBytes("PBKDF2 RFC 6070 (c=4096)", dk, e2, sizeof(e2));
    }
}

/*
 * PTK / PRF-512 known-answer test: IEEE 802.11i 8.5.1.2 example identities,
 * expected PTK cross-checked against an independent HMAC-SHA1 reference.
 */
static void TestPtkVector(void)
{
    rsna_u8 pmk[RSNA_PMK_LEN];
    rsna_u8 aa[6], spa[6], anonce[32], snonce[32];
    rsna_u8 ptk[64];
    rsna_u8 exp[64];

    FromHex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af",
            pmk, sizeof(pmk));
    FromHex("a0a1a2a3a4a5", aa, sizeof(aa));
    FromHex("b0b1b2b3b4b5", spa, sizeof(spa));
    FromHex("e0e1e2e3e4e5e6e7e8e9f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405",
            anonce, sizeof(anonce));
    FromHex("c0c1c2c3c4c5c6c7c8c9d0d1d2d3d4d5d6d7d8d9dadbdcdddedf000102030405",
            snonce, sizeof(snonce));

    FromHex("b411b1e02f0dfe2655080f9830cebb43"
            "9fa0ce5cb427a4dbf4db63b845589aef"
            "c983ab8510cf2d7e9c6a3d77552dd2f2"
            "92820fb31b187a12e56893a14ca56c8a",
            exp, sizeof(exp));

    RsnaDerivePtk(pmk, aa, spa, anonce, snonce, ptk, sizeof(ptk));
    CheckBytes("PTK PRF-512 (cross-checked vs hashlib reference)", ptk, exp, sizeof(exp));
}

/* ================================================================== */
/* Full 4-Way Handshake simulation                                    */
/* ================================================================== */

/* Build an AP-side EAPOL-Key frame for the simulated authenticator. */
static rsna_size ApBuildKeyFrame(rsna_u8 *out,
                                 rsna_u16 keyInfo,
                                 const rsna_u8 replay[8],
                                 const rsna_u8 nonce[32],
                                 const rsna_u8 *kck /* NULL => no MIC */,
                                 const rsna_u8 *iv  /* 16 bytes or NULL */,
                                 const rsna_u8 *keyData, rsna_size keyDataLen)
{
    rsna_size bodyLen = KEYDESC_FIXED_LEN + keyDataLen;
    rsna_size frameLen = EAPOL_HDR_LEN + bodyLen;
    rsna_u8 *body = out + EAPOL_HDR_LEN;

    memset(out, 0, frameLen);
    out[EAPOL_OFF_VERSION] = EAPOL_VERSION_2004;
    out[EAPOL_OFF_TYPE]    = EAPOL_TYPE_KEY;
    out[EAPOL_OFF_LENGTH]  = (rsna_u8)((bodyLen >> 8) & 0xFF);
    out[EAPOL_OFF_LENGTH + 1] = (rsna_u8)(bodyLen & 0xFF);

    body[KEYDESC_OFF_TYPE] = EAPOL_KEY_DESC_RSN;
    body[KEYDESC_OFF_KEYINFO]     = (rsna_u8)((keyInfo >> 8) & 0xFF);
    body[KEYDESC_OFF_KEYINFO + 1] = (rsna_u8)(keyInfo & 0xFF);
    body[KEYDESC_OFF_KEYLEN]     = 0x00;
    body[KEYDESC_OFF_KEYLEN + 1] = 16;          /* CCMP TK length */

    memcpy(&body[KEYDESC_OFF_REPLAY], replay, 8);
    memcpy(&body[KEYDESC_OFF_NONCE], nonce, 32);
    if (iv) memcpy(&body[KEYDESC_OFF_IV], iv, 16);

    body[KEYDESC_OFF_DATALEN]     = (rsna_u8)((keyDataLen >> 8) & 0xFF);
    body[KEYDESC_OFF_DATALEN + 1] = (rsna_u8)(keyDataLen & 0xFF);
    if (keyDataLen) memcpy(&body[KEYDESC_OFF_DATA], keyData, keyDataLen);

    if (kck)
    {
        rsna_u8 full[RSNA_SHA1_DIGEST_LEN];
        RsnaHmacSha1(kck, RSNA_KCK_LEN, out, frameLen, full);
        memcpy(&body[KEYDESC_OFF_MIC], full, KEYDESC_MIC_LEN);
    }

    return frameLen;
}

static void TestHandshake(void)
{
    RSNA_CTX ctx;
    rsna_u8 ssid[] = { 'T','e','s','t','N','e','t' };
    rsna_u8 aa[6]  = { 0x02,0x00,0x00,0x00,0x00,0x01 };  /* AP  */
    rsna_u8 spa[6] = { 0x02,0x00,0x00,0x00,0x00,0x02 };  /* STA */
    rsna_u8 anonce[32], replay[8];
    rsna_u8 frame[512];
    rsna_u8 reply[512];
    rsna_size frameLen, replyLen;
    rsna_u8 apPtk[48];
    rsna_u8 pmk[RSNA_PMK_LEN];
    RSNA_STATE st;
    int i;

    printf("\n--- Simulated WPA2-PSK 4-Way Handshake ---\n");

    /* Supplicant init. */
    if (RsnaInit(&ctx, ssid, sizeof(ssid), "passphrase123", 13) != RSNA_OK)
    {
        Check("handshake: RsnaInit", 0, NULL, NULL, 0);
        return;
    }
    RsnaSetApAddr(&ctx, aa);
    RsnaSetStaAddr(&ctx, spa);
    {
        /* A minimal RSN IE (CCMP pairwise+group, PSK AKM) for msg2 Key Data. */
        rsna_u8 rsnIe[] = {
            0x30, 0x14, 0x01, 0x00,
            0x00, 0x0f, 0xac, 0x04,             /* group: CCMP   */
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, /* pairwise: CCMP*/
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, /* akm: PSK      */
            0x00, 0x00
        };
        RsnaSetRsnIe(&ctx, rsnIe, sizeof(rsnIe));
    }
    RsnaSeed(&ctx, spa, sizeof(spa));   /* mix some entropy into SNonce PRNG */

    RsnaGetPmk(&ctx, pmk);

    /* ---- AP sends msg1 (ANonce, Pairwise+Ack, no MIC) ---- */
    for (i = 0; i < 32; i++) anonce[i] = (rsna_u8)(0x10 + i);
    memset(replay, 0, 8);
    replay[7] = 1;

    frameLen = ApBuildKeyFrame(frame,
                               KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                               replay, anonce, NULL, NULL, NULL, 0);

    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("msg1 -> state PTK_NEGOTIATING", st == RSNA_STATE_PTK_NEGOTIATING, NULL, NULL, 0);
    Check("msg2 produced", replyLen >= EAPOL_KEY_FRAME_FIXED_LEN, NULL, NULL, 0);
    Check("RSN msg2 Key Length is zero",
          reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYLEN] == 0 &&
          reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYLEN + 1] == 0,
          NULL, NULL, 0);

    /* A retransmitted msg1 may carry a newer replay counter, but it remains
     * part of the same handshake and must not rotate the SNonce/PTK. */
    {
        rsna_u8 firstSnonce[RSNA_NONCE_LEN];

        memcpy(firstSnonce,
               &reply[EAPOL_HDR_LEN + KEYDESC_OFF_NONCE],
               sizeof(firstSnonce));
        replay[7] = 2;
        frameLen = ApBuildKeyFrame(frame,
                                   KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE |
                                   KEYINFO_KEY_ACK,
                                   replay, anonce, NULL, NULL, NULL, 0);
        replyLen = sizeof(reply);
        st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
        Check("msg1 retransmit stays PTK_NEGOTIATING",
              st == RSNA_STATE_PTK_NEGOTIATING, NULL, NULL, 0);
        CheckBytes("msg1 retransmit reuses SNonce",
                   &reply[EAPOL_HDR_LEN + KEYDESC_OFF_NONCE],
                   firstSnonce, sizeof(firstSnonce));
    }

    /* The AP now derives the same PTK using the SNonce echoed in msg2. */
    {
        const rsna_u8 *m2body = reply + EAPOL_HDR_LEN;
        const rsna_u8 *snonce = &m2body[KEYDESC_OFF_NONCE];
        rsna_u8 full[RSNA_SHA1_DIGEST_LEN];
        rsna_u8 rxMic[KEYDESC_MIC_LEN];
        rsna_u8 m2copy[512];

        RsnaDerivePtk(pmk, aa, spa, anonce, snonce, apPtk, sizeof(apPtk));

        /* Verify msg2 MIC with the AP's freshly derived KCK. */
        memcpy(m2copy, reply, replyLen);
        memcpy(rxMic, &m2copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], KEYDESC_MIC_LEN);
        memset(&m2copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], 0, KEYDESC_MIC_LEN);
        RsnaHmacSha1(apPtk, RSNA_KCK_LEN, m2copy, replyLen, full);
        Check("AP verifies msg2 MIC", memcmp(full, rxMic, KEYDESC_MIC_LEN) == 0, NULL, NULL, 0);
    }

    /* ---- AP builds msg3: GTK KDE, AES-wrapped with KEK, MIC with KCK ---- */
    {
        rsna_u8 gtk[16];
        rsna_u8 gtkKde[2 + 6 + 16];     /* id+len + OUI+type+keyidhdr + GTK */
        rsna_u8 plainKd[96];
        rsna_size plainKdLen;
        rsna_u8 wrapped[104];
        rsna_size n;
        const rsna_u8 *kck = apPtk;
        const rsna_u8 *kek = apPtk + 16;
        rsna_u16 m3info;

        for (i = 0; i < 16; i++) gtk[i] = (rsna_u8)(0xA0 + i);

        /* GTK KDE: 0xDD len 00-0F-AC 01 keyid(0x01) rsvd(0) GTK[16]. */
        gtkKde[0] = KDE_IE_VENDOR;
        gtkKde[1] = (rsna_u8)(4 + 2 + 16);   /* OUI(3)+type(1)+hdr(2)+gtk(16) */
        gtkKde[2] = KDE_OUI_0; gtkKde[3] = KDE_OUI_1; gtkKde[4] = KDE_OUI_2;
        gtkKde[5] = KDE_TYPE_GTK;
        gtkKde[6] = 0x01;                    /* KeyID=1, Tx=0 */
        gtkKde[7] = 0x00;
        memcpy(&gtkKde[8], gtk, 16);

        /* Key Data plaintext = the GTK KDE (could include padding). */
        memcpy(plainKd, gtkKde, sizeof(gtkKde));
        plainKdLen = sizeof(gtkKde);
        {
            static const rsna_u8 IgtkKde[] =
            {
                KDE_IE_VENDOR, 4 + 2 + 6 + 16, KDE_OUI_0, KDE_OUI_1, KDE_OUI_2, KDE_TYPE_IGTK,
                0x04, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
                0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF
            };
            memcpy(plainKd + plainKdLen, IgtkKde, sizeof(IgtkKde));
            plainKdLen += sizeof(IgtkKde);
            plainKd[plainKdLen++] = KDE_IE_VENDOR;
        }
        /* AES Key Wrap requires a plaintext length multiple of 8: zero-pad. */
        while ((plainKdLen % 8) != 0)
            plainKd[plainKdLen++] = 0x00;

        n = plainKdLen / 8;
        RsnaAesWrap(kek, 128, plainKd, n, wrapped);   /* -> (n+1)*8 bytes */

        replay[7] = 3;     /* strictly greater than msg1's counter */
        m3info = KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK |
                 KEYINFO_KEY_MIC | KEYINFO_INSTALL | KEYINFO_SECURE |
                 KEYINFO_ENCRYPTED_DATA;

        frameLen = ApBuildKeyFrame(frame, m3info, replay, anonce,
                                   kck, NULL, wrapped, (n + 1) * 8);

        replyLen = sizeof(reply);
        st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
        Check("msg3 -> state COMPLETED", st == RSNA_STATE_COMPLETED, NULL, NULL, 0);
        if (st != RSNA_STATE_COMPLETED)
            printf("    (RsnaLastError = %d)\n", (int)RsnaLastError(&ctx));
        Check("msg4 produced", replyLen >= EAPOL_KEY_FRAME_FIXED_LEN, NULL, NULL, 0);

        /* The extracted GTK must match the AP's GTK. */
        {
            RSNA_KEYS keys;
            if (RsnaGetKeys(&ctx, &keys) == RSNA_OK)
            {
                CheckBytes("GTK extracted matches AP GTK", keys.gtk, gtk, 16);
                Check("GTK length == 16", keys.gtkLen == 16, NULL, NULL, 0);
                Check("GTK key id == 1", keys.gtkKeyId == 1, NULL, NULL, 0);
                /* The supplicant TK must match the AP's derived TK. */
                CheckBytes("PTK TK matches AP TK", keys.tk, apPtk + 32, 16);
                /* KCK/KEK match too (whole PTK identical). */
                CheckBytes("Full PTK matches AP PTK", keys.ptk, apPtk, 48);
                Check("IGTK key id == 4", keys.igtkKeyId == 4, NULL, NULL, 0);
                Check("IGTK length == 16", keys.igtkLen == 16, NULL, NULL, 0);
                Check("IGTK IPN read", keys.igtkIpn[0] == 0x01 && keys.igtkIpn[5] == 0x06, NULL, NULL, 0);
                Check("IGTK value read", keys.igtk[0] == 0xB0 && keys.igtk[15] == 0xBF, NULL, NULL, 0);
            }
            else
            {
                Check("RsnaGetKeys after completion", 0, NULL, NULL, 0);
            }
        }

        /* The AP verifies msg4's MIC. */
        {
            rsna_u8 full[RSNA_SHA1_DIGEST_LEN];
            rsna_u8 rxMic[KEYDESC_MIC_LEN];
            rsna_u8 m4copy[512];
            memcpy(m4copy, reply, replyLen);
            memcpy(rxMic, &m4copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], KEYDESC_MIC_LEN);
            memset(&m4copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], 0, KEYDESC_MIC_LEN);
            RsnaHmacSha1(apPtk, RSNA_KCK_LEN, m4copy, replyLen, full);
            Check("AP verifies msg4 MIC", memcmp(full, rxMic, KEYDESC_MIC_LEN) == 0, NULL, NULL, 0);
        }

        /* msg3 retransmit (msg4 lost): same replay counter -> re-emit msg4. */
        {
            replyLen = sizeof(reply);
            st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
            Check("msg3 retransmit stays COMPLETED", st == RSNA_STATE_COMPLETED, NULL, NULL, 0);
            Check("msg3 retransmit re-emits msg4", replyLen >= EAPOL_KEY_FRAME_FIXED_LEN, NULL, NULL, 0);
        }

        {
            static const rsna_u8 GroupKd[] =
            {
                KDE_IE_VENDOR, 4 + 2 + 16, KDE_OUI_0, KDE_OUI_1, KDE_OUI_2, KDE_TYPE_GTK,
                0x02, 0x00,
                0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
                0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
                KDE_IE_VENDOR, 4 + 2 + 6 + 16, KDE_OUI_0, KDE_OUI_1, KDE_OUI_2, KDE_TYPE_IGTK,
                0x05, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
                0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
                0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
                KDE_IE_VENDOR, 0x00
            };
            rsna_u8 groupWrapped[80];
            rsna_u8 full[RSNA_SHA1_DIGEST_LEN];
            rsna_u8 rxMic[KEYDESC_MIC_LEN];
            RSNA_KEYS before, after;
            rsna_u16 groupInfo = KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_ACK |
                                 KEYINFO_KEY_MIC | KEYINFO_SECURE | KEYINFO_ENCRYPTED_DATA;

            RsnaGetKeys(&ctx, &before);
            RsnaAesWrap(kek, 128, GroupKd, sizeof(GroupKd) / 8, groupWrapped);
            replay[7] = 4;
            frameLen = ApBuildKeyFrame(frame, groupInfo, replay, anonce, kck, NULL,
                                       groupWrapped, sizeof(GroupKd) + 8);
            replyLen = sizeof(reply);
            st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
            RsnaGetKeys(&ctx, &after);
            Check("group msg1 keeps COMPLETED", st == RSNA_STATE_COMPLETED, NULL, NULL, 0);
            Check("group msg2 produced", replyLen >= EAPOL_KEY_FRAME_FIXED_LEN, NULL, NULL, 0);
            Check("group msg2 is a group, secure, MIC'd reply",
                  replyLen >= EAPOL_KEY_FRAME_FIXED_LEN &&
                  !(reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYINFO + 1] & KEYINFO_KEY_TYPE) &&
                  (reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYINFO] & (KEYINFO_SECURE >> 8)) &&
                  (reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYINFO] & (KEYINFO_KEY_MIC >> 8)), NULL, NULL, 0);
            memcpy(rxMic, &reply[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], KEYDESC_MIC_LEN);
            memset(&reply[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], 0, KEYDESC_MIC_LEN);
            RsnaHmacSha1(apPtk, RSNA_KCK_LEN, reply, replyLen, full);
            Check("AP verifies group msg2 MIC", memcmp(full, rxMic, KEYDESC_MIC_LEN) == 0, NULL, NULL, 0);
            Check("group rekey installs GTK id 2",
                  after.gtkKeyId == 2 && after.gtk[0] == 0xC0 && after.gtk[15] == 0xCF, NULL, NULL, 0);
            Check("group rekey installs IGTK id 5",
                  after.igtkKeyId == 5 && after.igtk[0] == 0xD0 && after.igtkIpn[0] == 0x10, NULL, NULL, 0);
            Check("group rekey bumps the group serial", after.groupSerial == before.groupSerial + 1, NULL, NULL, 0);
            CheckBytes("group rekey keeps the PTK", after.ptk, before.ptk, 48);

            replyLen = sizeof(reply);
            st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
            Check("replayed group msg1 is dropped",
                  st == RSNA_STATE_COMPLETED && replyLen == 0 &&
                  RsnaLastError(&ctx) == RSNA_ERR_REPLAY, NULL, NULL, 0);
        }
    }

    /* ---- negative tests ---- */
    {
        RSNA_CTX ctx2;
        rsna_u8 badReplay[8];

        RsnaInit(&ctx2, ssid, sizeof(ssid), "passphrase123", 13);
        RsnaSetApAddr(&ctx2, aa);
        RsnaSetStaAddr(&ctx2, spa);

        /* Feed msg1 to get to PTK_NEGOTIATING. */
        memset(badReplay, 0, 8); badReplay[7] = 5;
        frameLen = ApBuildKeyFrame(frame,
                                   KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                                   badReplay, anonce, NULL, NULL, NULL, 0);
        replyLen = sizeof(reply);
        RsnaRxEapol(&ctx2, frame, frameLen, reply, &replyLen);

        /* Now send a msg3 with a BAD MIC -> must fail with RSNA_ERR_MIC. */
        badReplay[7] = 6;
        frameLen = ApBuildKeyFrame(frame,
                                   KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE |
                                   KEYINFO_KEY_ACK | KEYINFO_KEY_MIC | KEYINFO_INSTALL |
                                   KEYINFO_SECURE | KEYINFO_ENCRYPTED_DATA,
                                   badReplay, anonce, NULL, NULL, NULL, 0);
        /* deliberately leave the MIC field zero (wrong) and no key data */
        {
            rsna_u8 *body = frame + EAPOL_HDR_LEN;
            body[KEYDESC_OFF_DATALEN] = 0;
            body[KEYDESC_OFF_DATALEN + 1] = 0;
        }
        replyLen = sizeof(reply);
        st = RsnaRxEapol(&ctx2, frame, frameLen, reply, &replyLen);
        Check("msg3 with bad MIC -> FAILED", st == RSNA_STATE_FAILED, NULL, NULL, 0);
        Check("bad MIC reports RSNA_ERR_MIC", RsnaLastError(&ctx2) == RSNA_ERR_MIC, NULL, NULL, 0);
    }
}

static int FixedRandom(void *context, rsna_u8 *buffer, rsna_size length)
{
    rsna_size i;

    if (context == NULL)
        return -1;
    for (i = 0; i < length; i++)
        buffer[i] = *(const rsna_u8 *)context;
    return 0;
}

static void TestRandomSource(void)
{
    static const rsna_u8 Ssid[] = { 'T','e','s','t','N','e','t' };
    static const rsna_u8 Aa[6] = { 0x02,0x00,0x00,0x00,0x00,0x01 };
    static const rsna_u8 Spa[6] = { 0x02,0x00,0x00,0x00,0x00,0x02 };
    rsna_u8 fill = 0x5A, anonce[32], replay[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    rsna_u8 frame[512], reply[512], expected[RSNA_NONCE_LEN];
    rsna_size frameLen, replyLen;
    RSNA_CTX ctx;
    RSNA_STATE st;

    printf("\n--- SNonce random source ---\n");
    memset(anonce, 0x11, sizeof(anonce));
    memset(expected, fill, sizeof(expected));
    frameLen = ApBuildKeyFrame(frame, KEYINFO_VER_HMAC_SHA1_AES | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                               replay, anonce, NULL, NULL, NULL, 0);

    RsnaInit(&ctx, Ssid, sizeof(Ssid), "passphrase123", 13);
    RsnaSetApAddr(&ctx, Aa);
    RsnaSetStaAddr(&ctx, Spa);
    RsnaSetRandom(&ctx, FixedRandom, &fill);
    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("SNonce comes from the random callback",
          st == RSNA_STATE_PTK_NEGOTIATING &&
          memcmp(&reply[EAPOL_HDR_LEN + KEYDESC_OFF_NONCE], expected, sizeof(expected)) == 0, NULL, NULL, 0);
    RsnaClear(&ctx);

    RsnaInit(&ctx, Ssid, sizeof(Ssid), "passphrase123", 13);
    RsnaSetApAddr(&ctx, Aa);
    RsnaSetStaAddr(&ctx, Spa);
    RsnaSetRandom(&ctx, FixedRandom, NULL);
    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("a failing random source fails the handshake",
          st == RSNA_STATE_FAILED && RsnaLastError(&ctx) == RSNA_ERR_RANDOM && replyLen == 0, NULL, NULL, 0);
    RsnaClear(&ctx);
}

/*
 * Legacy TKIP descriptor path: HMAC-MD5 Key MIC and RC4-encrypted Key Data
 * (key = EAPOL Key IV || KEK, first 256 keystream bytes dropped).
 */
static void TestHandshakeSae(void)
{
    static const rsna_u8 RsnIe[] =
    {
        0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x08, 0xc0, 0x00
    };
    rsna_u8 aa[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    rsna_u8 spa[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
    rsna_u8 pmk[RSNA_PMK_LEN], anonce[32], replay[8], frame[512], reply[512], copy[512];
    rsna_u8 apPtk[48], mic[16], kd[32], wrapped[40];
    rsna_size frameLen, replyLen;
    RSNA_KEYS keys;
    RSNA_CTX ctx;
    RSNA_STATE st;
    int i;

    printf("\n--- Simulated WPA3-SAE (AKM 8) 4-Way Handshake ---\n");

    for (i = 0; i < RSNA_PMK_LEN; i++)
        pmk[i] = (rsna_u8)(0x40 + i);
    if (RsnaInitWithPmk(&ctx, (const rsna_u8 *)"WPA3Net", 7, pmk) != RSNA_OK)
    {
        Check("SAE handshake: RsnaInitWithPmk", 0, NULL, NULL, 0);
        return;
    }
    RsnaSetApAddr(&ctx, aa);
    RsnaSetStaAddr(&ctx, spa);
    RsnaSetRsnIe(&ctx, RsnIe, sizeof(RsnIe));
    RsnaSeed(&ctx, spa, sizeof(spa));

    for (i = 0; i < 32; i++)
        anonce[i] = (rsna_u8)(0x60 + i);
    memset(replay, 0, sizeof(replay));
    replay[7] = 1;
    frameLen = ApBuildKeyFrame(frame, KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK, replay, anonce, NULL, NULL, NULL, 0);
    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("SAE msg1 -> PTK_NEGOTIATING", st == RSNA_STATE_PTK_NEGOTIATING, NULL, NULL, 0);
    Check("SAE msg2 descriptor version 0",
          (reply[EAPOL_HDR_LEN + KEYDESC_OFF_KEYINFO + 1] & KEYINFO_VERSION_MASK) == 0, NULL, NULL, 0);

    RsnaDerivePtkSha256(pmk, aa, spa, anonce, &reply[EAPOL_HDR_LEN + KEYDESC_OFF_NONCE], apPtk, sizeof(apPtk));
    memcpy(copy, reply, replyLen);
    memset(&copy[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], 0, KEYDESC_MIC_LEN);
    RsnaAesCmac(apPtk, copy, replyLen, mic);
    CheckBytes("AP verifies SAE msg2 AES-CMAC MIC", &reply[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], mic, 16);

    kd[0] = KDE_IE_VENDOR;
    kd[1] = 4 + 2 + 16;
    kd[2] = KDE_OUI_0; kd[3] = KDE_OUI_1; kd[4] = KDE_OUI_2;
    kd[5] = KDE_TYPE_GTK;
    kd[6] = 0x01;
    kd[7] = 0x00;
    for (i = 0; i < 16; i++)
        kd[8 + i] = (rsna_u8)(0xC0 + i);
    kd[24] = KDE_IE_VENDOR;
    for (i = 25; i < 32; i++)
        kd[i] = 0;
    RsnaAesWrap(apPtk + 16, 128, kd, 4, wrapped);

    replay[7] = 2;
    frameLen = ApBuildKeyFrame(frame, KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK | KEYINFO_KEY_MIC | KEYINFO_INSTALL |
                               KEYINFO_SECURE | KEYINFO_ENCRYPTED_DATA,
                               replay, anonce, NULL, NULL, wrapped, sizeof(wrapped));
    RsnaAesCmac(apPtk, frame, frameLen, mic);
    memcpy(&frame[EAPOL_HDR_LEN + KEYDESC_OFF_MIC], mic, KEYDESC_MIC_LEN);
    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("SAE msg3 -> COMPLETED", st == RSNA_STATE_COMPLETED, NULL, NULL, 0);
    RsnaGetKeys(&ctx, &keys);
    CheckBytes("SAE PTK from KDF-SHA-256", keys.ptk, apPtk, sizeof(apPtk));
    CheckBytes("SAE GTK extracted", keys.gtk, kd + 8, 16);
    RsnaClear(&ctx);
}

static void TestHandshakeTkip(void)
{
    RSNA_CTX ctx;
    rsna_u8 ssid[] = { 'T','k','i','p','N','e','t' };
    rsna_u8 aa[6]  = { 0x02,0x00,0x00,0x00,0x00,0x11 };
    rsna_u8 spa[6] = { 0x02,0x00,0x00,0x00,0x00,0x12 };
    rsna_u8 anonce[32], replay[8], iv[16];
    rsna_u8 frame[512], reply[512];
    rsna_size frameLen, replyLen;
    rsna_u8 apPtk[64];
    rsna_u8 pmk[RSNA_PMK_LEN];
    RSNA_STATE st;
    int i;

    printf("\n--- Simulated WPA(TKIP)-PSK 4-Way Handshake (HMAC-MD5 + RC4) ---\n");

    RsnaInit(&ctx, ssid, sizeof(ssid), "tkippassphrase", 14);
    RsnaSetApAddr(&ctx, aa);
    RsnaSetStaAddr(&ctx, spa);
    RsnaGetPmk(&ctx, pmk);

    /* msg1: TKIP key descriptor version (1), pairwise + ack, no MIC. */
    for (i = 0; i < 32; i++) anonce[i] = (rsna_u8)(0x40 + i);
    memset(replay, 0, 8); replay[7] = 1;

    frameLen = ApBuildKeyFrame(frame,
                               KEYINFO_VER_HMAC_MD5_RC4 | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK,
                               replay, anonce, NULL, NULL, NULL, 0);
    replyLen = sizeof(reply);
    st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
    Check("TKIP msg1 -> PTK_NEGOTIATING", st == RSNA_STATE_PTK_NEGOTIATING, NULL, NULL, 0);

    /* AP derives the 512-bit TKIP PTK from the SNonce echoed in msg2. */
    {
        const rsna_u8 *m2body = reply + EAPOL_HDR_LEN;
        RsnaDerivePtk(pmk, aa, spa, anonce, &m2body[KEYDESC_OFF_NONCE],
                      apPtk, sizeof(apPtk));
    }

    /* msg3: GTK KDE, RC4-encrypted Key Data, HMAC-MD5 MIC. */
    {
        rsna_u8 gtk[32];
        rsna_u8 gtkKde[2 + 6 + 32];
        rsna_u8 plainKd[64], encKd[64];
        rsna_size kdLen;
        const rsna_u8 *kck = apPtk;
        const rsna_u8 *kek = apPtk + 16;
        rsna_u16 m3info;
        RSNA_RC4_CTX rc4;
        rsna_u8 rc4key[16 + 16];
        rsna_u8 full[RSNA_MD5_DIGEST_LEN];
        rsna_u8 *body;

        for (i = 0; i < 32; i++) gtk[i] = (rsna_u8)(0x50 + i);
        for (i = 0; i < 16; i++) iv[i] = (rsna_u8)(0x90 + i);

        gtkKde[0] = KDE_IE_VENDOR;
        gtkKde[1] = (rsna_u8)(4 + 2 + 32);
        gtkKde[2] = KDE_OUI_0; gtkKde[3] = KDE_OUI_1; gtkKde[4] = KDE_OUI_2;
        gtkKde[5] = KDE_TYPE_GTK;
        gtkKde[6] = 0x02;            /* KeyID = 2 */
        gtkKde[7] = 0x00;
        memcpy(&gtkKde[8], gtk, 32);

        memcpy(plainKd, gtkKde, sizeof(gtkKde));
        kdLen = sizeof(gtkKde);

        /* RC4-encrypt with key = IV || KEK, dropping 256 keystream bytes. */
        memcpy(rc4key, iv, 16);
        memcpy(rc4key + 16, kek, 16);
        RsnaRc4Init(&rc4, rc4key, sizeof(rc4key));
        RsnaRc4Skip(&rc4, 256);
        RsnaRc4Crypt(&rc4, plainKd, encKd, kdLen);

        replay[7] = 2;
        m3info = KEYINFO_VER_HMAC_MD5_RC4 | KEYINFO_KEY_TYPE | KEYINFO_KEY_ACK |
                 KEYINFO_KEY_MIC | KEYINFO_INSTALL | KEYINFO_SECURE |
                 KEYINFO_ENCRYPTED_DATA;

        /* Build frame WITHOUT MIC first (kck=NULL), set IV, then HMAC-MD5 it. */
        frameLen = ApBuildKeyFrame(frame, m3info, replay, anonce,
                                   NULL, iv, encKd, kdLen);
        body = frame + EAPOL_HDR_LEN;
        RsnaHmacMd5(kck, RSNA_KCK_LEN, frame, frameLen, full);
        memcpy(&body[KEYDESC_OFF_MIC], full, KEYDESC_MIC_LEN);

        replyLen = sizeof(reply);
        st = RsnaRxEapol(&ctx, frame, frameLen, reply, &replyLen);
        Check("TKIP msg3 -> COMPLETED", st == RSNA_STATE_COMPLETED, NULL, NULL, 0);
        if (st != RSNA_STATE_COMPLETED)
            printf("    (RsnaLastError = %d)\n", (int)RsnaLastError(&ctx));

        {
            RSNA_KEYS keys;
            if (RsnaGetKeys(&ctx, &keys) == RSNA_OK)
            {
                CheckBytes("TKIP GTK extracted matches", keys.gtk, gtk, 32);
                Check("TKIP GTK length == 32", keys.gtkLen == 32, NULL, NULL, 0);
                Check("TKIP cipher == TKIP", keys.pairwiseCipher == RSNA_CIPHER_TKIP, NULL, NULL, 0);
                Check("TKIP PTK length == 64", keys.ptkLen == 64, NULL, NULL, 0);
            }
            else
            {
                Check("TKIP RsnaGetKeys", 0, NULL, NULL, 0);
            }
        }
    }
}

static void TestSha256(void)
{
    rsna_u8 out[48], exp[48];

    RsnaSha256("abc", 3, out);
    FromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", exp, sizeof(exp));
    CheckBytes("SHA-256 FIPS 180-4 abc", out, exp, 32);

    RsnaHmacSha256((const rsna_u8 *)"Jefe", 4, (const rsna_u8 *)"what do ya want for nothing?", 28, out);
    FromHex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", exp, sizeof(exp));
    CheckBytes("HMAC-SHA-256 RFC 4231 case 2", out, exp, 32);

    RsnaKdfSha256((const rsna_u8 *)"key", 3, "label", (const rsna_u8 *)"ctx", 3, out, 384);
    FromHex("ec355ff2dc2ac99e8df3d8a95eae4c1bc28237326547ed2f3140b2d5e6daa65e"
            "a93ad7782e2b84e3e5b1b396880f5163", exp, sizeof(exp));
    CheckBytes("KDF-SHA-256-384", out, exp, 48);
}

static void TestSae(void)
{
    rsna_u8 own[6], peer[6], rand[32], mask[32], commit[98], peerCommit[98], kck[32], pmk[32], pmkid[16];
    RSNA_SAE sae;

    FromHex("4d3f2fffe387", own, sizeof(own));
    FromHex("a5d8aa958e3c", peer, sizeof(peer));
    FromHex("992465fd3daa3c60aa6565b7f62a2a7f2e12dd12f198faf4fbed89d7ff1ace94", rand, sizeof(rand));
    FromHex("9507a90f777a044d6a0830b91ea3d5dd70bece44e1acffb86983b5e1bf9fb322", mask, sizeof(mask));
    FromHex("13002e2c0f0db52440ad146d967114ce005ce1eab0aa2c2e5c2871b774f6c2575c65d5ad9e00829707aa36ba8b85"
            "9738fc961d08243505f47c035376d7ac4bc8d7b95083bf43827d0fc31ed778dd3671fd21a46d1091d64b6f9a1e"
            "1272621325dbe1", commit, sizeof(commit));
    FromHex("1300591b96f3397fb945100848e7b550543b6720d88337ee93fc49fd6df7e08b5223e71b9bb048d3873f20556953"
            "a96c91536fd8ee6ca9b4a68a148b056a909be03e83ae208f60f8ef5537858074db06687032399862999b511e0a"
            "1552a5fea317c2", peerCommit, sizeof(peerCommit));
    FromHex("1e733f6d9bd53256287304338831b09a39406d121017073a5c30db36f36cb81a", kck, sizeof(kck));
    FromHex("4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59", pmk, sizeof(pmk));
    FromHex("8747a600eea3f9f22475df58ca1e5498", pmkid, sizeof(pmkid));

    memset(&sae, 0, sizeof(sae));
    Check("SAE PWE hunting and pecking",
          RsnaSaeDerivePwe(&sae, own, peer, (const rsna_u8 *)"mekmitasdigoat", 14), NULL, NULL, 0);
    Check("SAE commit", RsnaSaeSetCommit(&sae, rand, mask), NULL, NULL, 0);
    CheckBytes("SAE commit scalar (802.11-2020 J.10)", sae.Scalar, commit + 2, 32);
    CheckBytes("SAE commit element (802.11-2020 J.10)", sae.Element, commit + 34, 64);
    Check("SAE peer commit", RsnaSaeProcessCommit(&sae, peerCommit + 2, peerCommit + 34), NULL, NULL, 0);
    CheckBytes("SAE KCK (802.11-2020 J.10)", sae.Kck, kck, 32);
    CheckBytes("SAE PMK (802.11-2020 J.10)", sae.Pmk, pmk, 32);
    CheckBytes("SAE PMKID (802.11-2020 J.10)", sae.Pmkid, pmkid, 16);
    RsnaSaeClear(&sae);
}

/* ================================================================== */

int main(void)
{
    printf("=== ReactOS RSNA (WPA2-PSK) supplicant test harness ===\n\n");

    printf("--- Crypto primitive vectors ---\n");
    TestSha1();
    TestMd5();
    TestHmac();
    TestAes();
    TestRc4();
    TestAesKeyWrap();
    TestCmac();
    TestCcmpBip();
    TestSha256();
    TestSae();

    printf("\n--- Key derivation vectors ---\n");
    TestPmk();
    TestPtkVector();

    TestHandshake();
    TestHandshakeTkip();
    TestHandshakeSae();
    TestRandomSource();

    printf("\n=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
