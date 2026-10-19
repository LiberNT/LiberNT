/*
 * Lightweight mbedTLS-based implementation of the schannel (SSL/TLS) provider.
 *
 * Copyright 2015 Peter Hater
 * Copyright 2015 Ismael Ferreras Morezuelas <swyterzone+ros@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <wine/config.h>
#include "precomp.h"
#include <errno.h>

WINE_DEFAULT_DEBUG_CHANNEL(schannel);

#if defined(SONAME_LIBMBEDTLS) && !defined(HAVE_SECURITY_SECURITY_H) && !defined(SONAME_LIBGNUTLS)

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <psa/crypto.h>

typedef struct
{
    mbedtls_x509_crt   cert;
    mbedtls_pk_context key;
} MBEDTLS_CREDENTIALS;

typedef struct
{
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    struct schan_transport  *transport;
    char                   **alpn_protocols;
    MBEDTLS_CREDENTIALS     *credentials;
} MBEDTLS_SESSION, *PMBEDTLS_SESSION;

typedef MBEDTLS_SESSION *schan_imp_session;

static const uint16_t small_key_sig_algs[] =
{
    MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256,
    MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256,
    MBEDTLS_TLS1_3_SIG_ECDSA_SHA1,
    MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA1,
    MBEDTLS_TLS1_3_SIG_NONE
};

struct schan_buffers
{
    SIZE_T offset;
    SIZE_T limit;
    const SecBufferDesc *desc;
    int current_buffer_idx;
};

struct schan_transport
{
    struct schan_buffers in;
    struct schan_buffers out;
};

static void init_schan_buffers(struct schan_buffers *buffers, const SecBufferDesc *desc)
{
    buffers->offset = 0;
    buffers->limit = ~(SIZE_T)0;
    buffers->desc = desc;
    buffers->current_buffer_idx = -1;
}

static char *schan_get_buffer(struct schan_buffers *buffers, SIZE_T *count)
{
    SIZE_T available;
    SecBuffer *buffer;

    if (!buffers->desc || !buffers->desc->cBuffers)
        return NULL;

    if (buffers->current_buffer_idx == -1)
        buffers->current_buffer_idx = 0;

    for (;;)
    {
        if ((ULONG)buffers->current_buffer_idx >= buffers->desc->cBuffers)
            return NULL;

        buffer = &buffers->desc->pBuffers[buffers->current_buffer_idx];
        available = buffer->cbBuffer - buffers->offset;
        if (buffers->limit != ~(SIZE_T)0 && buffers->limit < available)
            available = buffers->limit;
        if (available)
            break;

        /* Preserve the consumed offset when the final buffer is exhausted.
         * The caller uses it to report unconsumed SECBUFFER_EXTRA bytes. */
        if ((ULONG)(buffers->current_buffer_idx + 1) >= buffers->desc->cBuffers)
            return NULL;

        buffers->current_buffer_idx++;
        buffers->offset = 0;
    }

    if (*count > available)
        *count = available;
    if (buffers->limit != ~(SIZE_T)0)
        buffers->limit -= *count;
    return (char *)buffer->pvBuffer + buffers->offset;
}

static int schan_pull(struct schan_transport *transport, void *buffer, size_t *length)
{
    SIZE_T count = *length;
    char *source;

    *length = 0;
    if (!(source = schan_get_buffer(&transport->in, &count)))
        return EAGAIN;

    memcpy(buffer, source, count);
    transport->in.offset += count;
    *length = count;
    return 0;
}

static int schan_push(struct schan_transport *transport, const void *buffer, size_t *length)
{
    SIZE_T count = *length;
    char *destination;

    *length = 0;
    if (!(destination = schan_get_buffer(&transport->out, &count)))
        return EAGAIN;

    memcpy(destination, buffer, count);
    transport->out.offset += count;
    *length = count;
    return 0;
}

/* custom `net_recv` callback adapter, mbedTLS uses it in mbedtls_ssl_read for
   pulling data from the underlying win32 net stack */
static int schan_pull_adapter(void *session, unsigned char *buff, size_t buff_len)
{
    MBEDTLS_SESSION *s = session;
    int status;

    TRACE("MBEDTLS schan_pull_adapter: (%p/%p, %p, %u)\n", s, s->transport, buff, buff_len);

    status = schan_pull(s->transport, buff, &buff_len);

    TRACE("MBEDTLS schan_pull_adapter: (%p/%p, %p, %u) status: %#x\n", s, s->transport, buff, buff_len, status);

    if (status == NO_ERROR)
    {
        TRACE("Pulled %u bytes\n", buff_len);
        return buff_len;
    }
    else if (status == EAGAIN)
    {
        TRACE("Would block before being able to pull anything\n");
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    else
    {
        ERR("Unknown status code from schan_pull: %d\n", status);
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    /* this should be unreachable */
    return MBEDTLS_ERR_NET_CONNECT_FAILED;
}

/* custom `net_send` callback adapter, mbedTLS uses it in mbedtls_ssl_write for
   pushing data to the underlying win32 net stack */
static int schan_push_adapter(void *session, const unsigned char *buff, size_t buff_len)
{
    MBEDTLS_SESSION *s = session;
    int status;

    TRACE("MBEDTLS schan_push_adapter: (%p/%p, %p, %u)\n", s, s->transport, buff, buff_len);

    status = schan_push(s->transport, buff, &buff_len);

    TRACE("MBEDTLS schan_push_adapter: (%p/%p, %p, %u) status: %#x\n", s, s->transport, buff, buff_len, status);

    if (status == NO_ERROR)
    {
        TRACE("Pushed %u bytes\n", buff_len);
        return buff_len;
    }
    else if (status == EAGAIN)
    {
        TRACE("Would block before being able to push anything\n");
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    else
    {
        ERR("Unknown status code from schan_push: %d\n", status);
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    /* this should be unreachable */
    return MBEDTLS_ERR_NET_CONNECT_FAILED;
}

DWORD schan_imp_enabled_protocols(void)
{
    /* NOTE: No support for SSL 2.0 */
    TRACE("MBEDTLS schan_imp_enabled_protocols()\n");

    return 0
#ifdef MBEDTLS_SSL_PROTO_TLS1_2
        | SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_2_SERVER
#endif
#ifdef MBEDTLS_SSL_PROTO_TLS1_3
        | SP_PROT_TLS1_3_CLIENT | SP_PROT_TLS1_3_SERVER
#endif
        ;
}

static void schan_imp_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    WARN("MBEDTLS schan_imp_debug: %s:%04d: %s\n", file, line, str);
}

BOOL schan_imp_create_session(schan_imp_session *session, schan_credentials *cred)
{
    MBEDTLS_SESSION *s;
    BOOL server = !!(cred->credential_use & SECPKG_CRED_INBOUND);
    DWORD tls12 = server ? SP_PROT_TLS1_2_SERVER : SP_PROT_TLS1_2_CLIENT;
    DWORD tls13 = server ? SP_PROT_TLS1_3_SERVER : SP_PROT_TLS1_3_CLIENT;

    if (psa_crypto_init() != PSA_SUCCESS)
    {
        ERR("Failed to initialize the PSA crypto core\n");
        *session = NULL;
        return FALSE;
    }

    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MBEDTLS_SESSION));

    WARN("MBEDTLS schan_imp_create_session: %p %p %p\n", session, *session, cred);

    if (!(*session = (schan_imp_session)s))
    {
        ERR("Not enough memory to create session\n");
        return FALSE;
    }

    TRACE("MBEDTLS init entropy\n");
    mbedtls_entropy_init(&s->entropy);

    TRACE("MBEDTLS init random - change static entropy private data\n");
    mbedtls_ctr_drbg_init(&s->ctr_drbg);
    mbedtls_ctr_drbg_seed(&s->ctr_drbg, mbedtls_entropy_func, &s->entropy, NULL, 0);

    WARN("MBEDTLS init ssl\n");
    mbedtls_ssl_init(&s->ssl);

    WARN("MBEDTLS init conf\n");
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);

    TRACE("MBEDTLS set BIO callbacks\n");
    mbedtls_ssl_set_bio(&s->ssl, s, schan_push_adapter, schan_pull_adapter, NULL);

    TRACE("MBEDTLS set endpoint to %s\n", (cred->credential_use & SECPKG_CRED_INBOUND) ? "server" : "client");
    mbedtls_ssl_conf_endpoint(&s->conf,   (cred->credential_use & SECPKG_CRED_INBOUND) ? MBEDTLS_SSL_IS_SERVER :
                                                                                         MBEDTLS_SSL_IS_CLIENT);

    if (cred->enabled_protocols & (tls12 | tls13))
    {
        mbedtls_ssl_conf_min_tls_version(&s->conf, (cred->enabled_protocols & tls12) ? MBEDTLS_SSL_VERSION_TLS1_2 :
                                                                                       MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&s->conf, (cred->enabled_protocols & tls13) ? MBEDTLS_SSL_VERSION_TLS1_3 :
                                                                                       MBEDTLS_SSL_VERSION_TLS1_2);
    }

    TRACE("MBEDTLS set authmode\n");
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);

    TRACE("MBEDTLS set rng\n");
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->ctr_drbg);

    TRACE("MBEDTLS set dbg\n");
    mbedtls_ssl_conf_dbg(&s->conf, schan_imp_debug, stdout);

    if (cred->credentials)
    {
        MBEDTLS_CREDENTIALS *creds = (MBEDTLS_CREDENTIALS *)(ULONG_PTR)cred->credentials;

        if (mbedtls_ssl_conf_own_cert(&s->conf, &creds->cert, &creds->key))
        {
            ERR("Failed to set the session certificate\n");
            mbedtls_ssl_free(&s->ssl);
            mbedtls_ssl_config_free(&s->conf);
            mbedtls_ctr_drbg_free(&s->ctr_drbg);
            mbedtls_entropy_free(&s->entropy);
            HeapFree(GetProcessHeap(), 0, s);
            *session = NULL;
            return FALSE;
        }

        if (mbedtls_pk_get_bitlen(&creds->key) < 1024)
            mbedtls_ssl_conf_sig_algs(&s->conf, small_key_sig_algs);

        s->credentials = creds;
    }

    TRACE("MBEDTLS setup\n");
    mbedtls_ssl_setup(&s->ssl, &s->conf);

    TRACE("MBEDTLS schan_imp_create_session END!\n");
    return TRUE;
}

void schan_imp_dispose_session(schan_imp_session session)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;
    unsigned int i;
    WARN("MBEDTLS schan_imp_dispose_session: %p\n", session);

    /* tell the other peer (a server) that we are going away */
    //ssl_close_notify(&s->ssl);

    mbedtls_ssl_free(&s->ssl);
    mbedtls_ctr_drbg_free(&s->ctr_drbg);
    mbedtls_entropy_free(&s->entropy);
    mbedtls_ssl_config_free(&s->conf);

    if (s->alpn_protocols)
    {
        for (i = 0; s->alpn_protocols[i]; i++)
            HeapFree(GetProcessHeap(), 0, s->alpn_protocols[i]);
        HeapFree(GetProcessHeap(), 0, s->alpn_protocols);
    }

    /* safely overwrite the freed context with zeroes */
    HeapFree(GetProcessHeap(), HEAP_ZERO_MEMORY, s);
}

void schan_imp_set_session_transport(schan_imp_session session,
                                     struct schan_transport *t)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    TRACE("MBEDTLS schan_imp_set_session_transport: %p %p\n", session, t);

    s->transport = t;
}

void schan_imp_set_session_target(schan_imp_session session, const char *target)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    TRACE("MBEDTLS schan_imp_set_session_target: sess: %p hostname: %s\n", session, target);

    /* FIXME: WINE tests do not pass when we set the hostname because in the test cases
     * contacting 'www.winehq.org' the hostname is defined as 'localhost' so the server
     * sends a non-fatal alert which preemptively forces mbedTLS to close connection. */

    mbedtls_ssl_set_hostname(&s->ssl, target);
}

SECURITY_STATUS schan_imp_handshake(schan_imp_session session)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    int err = mbedtls_ssl_handshake(&s->ssl);

    TRACE("MBEDTLS schan_imp_handshake: %p  err: %#x \n", session, err);

    if (err == MBEDTLS_ERR_SSL_WANT_READ || err == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        TRACE("Received ERR_NET_WANT_READ/WRITE... let's try again!\n");
        return SEC_I_CONTINUE_NEEDED;
    }
    else if (err == MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE)
    {
        ERR("schan_imp_handshake: SSL Feature unavailable...\n");
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
    else if (err != 0)
    {
        ERR("schan_imp_handshake: Oops! mbedtls_ssl_handshake returned the following error code: -%#x...\n", -err);
        return SEC_E_INTERNAL_ERROR;
    }

    WARN("schan_imp_handshake: Handshake completed!\n");
    WARN("schan_imp_handshake: Protocol is %s, Cipher suite is %s\n", mbedtls_ssl_get_version(&s->ssl),
                                                                      mbedtls_ssl_get_ciphersuite(&s->ssl));
    return SEC_E_OK;
}

static unsigned int schannel_get_cipher_key_size(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *ssl_cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);
    const mbedtls_cipher_info_t          *cipher_info = mbedtls_cipher_info_from_type(ssl_cipher_suite->cipher);

    unsigned int key_bitlen = mbedtls_cipher_info_get_key_bitlen(cipher_info);

    TRACE("MBEDTLS schannel_get_cipher_key_size: Unknown cipher %#x, returning %u\n", ciphersuite_id, key_bitlen);

    return key_bitlen;
}

static BOOL schannel_is_aead(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *ssl_cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);
    const mbedtls_cipher_info_t          *cipher_info = mbedtls_cipher_info_from_type(ssl_cipher_suite->cipher);

    return cipher_info && (mbedtls_cipher_info_get_mode(cipher_info) == MBEDTLS_MODE_GCM ||
                           mbedtls_cipher_info_get_mode(cipher_info) == MBEDTLS_MODE_CCM ||
                           mbedtls_cipher_info_get_mode(cipher_info) == MBEDTLS_MODE_CHACHAPOLY);
}

static unsigned int schannel_get_mac_key_size(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *ssl_cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);
    const mbedtls_md_info_t                  *md_info = mbedtls_md_info_from_type(ssl_cipher_suite->mac);
    int md_size;

    if (schannel_is_aead(ciphersuite_id))
        return 0;

    md_size = mbedtls_md_get_size(md_info) * CHAR_BIT; /* return the size in bits, as the secur32:schannel winetest shows */

    TRACE("MBEDTLS schannel_get_mac_key_size: returning %i\n", md_size);

    return md_size;
}

static unsigned int schannel_get_kx_key_size(const MBEDTLS_SESSION *s, int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *ssl_ciphersuite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);

    /* if we are the server take ca_chain, if we are the client take the proper x509 peer certificate */
    const mbedtls_x509_crt *server_cert = (mbedtls_ssl_conf_get_endpoint(&s->conf) == MBEDTLS_SSL_IS_SERVER) ?
                                          (s->credentials ? &s->credentials->cert : NULL) : mbedtls_ssl_get_peer_cert(&s->ssl);

    if (server_cert && ssl_ciphersuite->key_exchange != MBEDTLS_KEY_EXCHANGE_NONE)
        return mbedtls_pk_get_len(&(server_cert->pk));

    TRACE("MBEDTLS schannel_get_kx_key_size: Unknown kx %#x, returning 0\n", ssl_ciphersuite->key_exchange);

    return 0;
}

static DWORD schannel_get_protocol(const mbedtls_ssl_context *ssl, const mbedtls_ssl_config *conf)
{
    /* FIXME: currently schannel only implements client connections, but
     * there's no reason it couldn't be used for servers as well. The
     * context doesn't tell us which it is, so decide based on ssl endpoint value. */

    switch (mbedtls_ssl_get_version_number(ssl))
    {
        case MBEDTLS_SSL_VERSION_TLS1_2:
            return (mbedtls_ssl_conf_get_endpoint(conf) == MBEDTLS_SSL_IS_CLIENT) ? SP_PROT_TLS1_2_CLIENT :
                                                                                    SP_PROT_TLS1_2_SERVER;

        case MBEDTLS_SSL_VERSION_TLS1_3:
            return (mbedtls_ssl_conf_get_endpoint(conf) == MBEDTLS_SSL_IS_CLIENT) ? SP_PROT_TLS1_3_CLIENT :
                                                                                    SP_PROT_TLS1_3_SERVER;

        default:
        {
            FIXME("MBEDTLS schannel_get_protocol: unknown protocol %#x\n", mbedtls_ssl_get_version_number(ssl));
            return 0;
        }
    }
}

static ALG_ID schannel_get_cipher_algid(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);

    switch (cipher_suite->cipher)
    {
        case MBEDTLS_CIPHER_NONE:
        case MBEDTLS_CIPHER_NULL:
            return 0;

#ifdef MBEDTLS_DES_C
        /* DES */
        case MBEDTLS_CIPHER_DES_ECB:
        case MBEDTLS_CIPHER_DES_CBC:
        case MBEDTLS_CIPHER_DES_EDE_ECB:
        case MBEDTLS_CIPHER_DES_EDE_CBC:
            return CALG_DES;

        case MBEDTLS_CIPHER_DES_EDE3_ECB:
        case MBEDTLS_CIPHER_DES_EDE3_CBC:
            return CALG_3DES;
#endif

#ifdef MBEDTLS_CAMELLIA_C
        /* CAMELLIA */
        case MBEDTLS_CIPHER_CAMELLIA_128_ECB:
        case MBEDTLS_CIPHER_CAMELLIA_192_ECB:
        case MBEDTLS_CIPHER_CAMELLIA_256_ECB:
        case MBEDTLS_CIPHER_CAMELLIA_128_CBC:
        case MBEDTLS_CIPHER_CAMELLIA_192_CBC:
        case MBEDTLS_CIPHER_CAMELLIA_256_CBC:
        case MBEDTLS_CIPHER_CAMELLIA_128_CFB128:
        case MBEDTLS_CIPHER_CAMELLIA_192_CFB128:
        case MBEDTLS_CIPHER_CAMELLIA_256_CFB128:
        case MBEDTLS_CIPHER_CAMELLIA_128_CTR:
        case MBEDTLS_CIPHER_CAMELLIA_192_CTR:
        case MBEDTLS_CIPHER_CAMELLIA_256_CTR:
        case MBEDTLS_CIPHER_CAMELLIA_128_GCM:
        case MBEDTLS_CIPHER_CAMELLIA_192_GCM:
        case MBEDTLS_CIPHER_CAMELLIA_256_GCM:
            return CALG_AES_256;  // (as schannel does not support it fake it as AES, which has a
                                  //  similar profile, offering modern high security) CALG_CAMELLIA;
#endif

#ifdef MBEDTLS_AES_C
        /* AES 128 */
        case MBEDTLS_CIPHER_AES_128_ECB:
        case MBEDTLS_CIPHER_AES_128_CBC:
        case MBEDTLS_CIPHER_AES_128_CFB128:
        case MBEDTLS_CIPHER_AES_128_CTR:
        case MBEDTLS_CIPHER_AES_128_GCM:
    #ifdef MBEDTLS_CCM_C
        case MBEDTLS_CIPHER_AES_128_CCM:
    #endif
            return CALG_AES_128;

        case MBEDTLS_CIPHER_AES_192_ECB:
        case MBEDTLS_CIPHER_AES_192_CBC:
        case MBEDTLS_CIPHER_AES_192_CFB128:
        case MBEDTLS_CIPHER_AES_192_CTR:
        case MBEDTLS_CIPHER_AES_192_GCM:
    #ifdef MBEDTLS_CCM_C
        case MBEDTLS_CIPHER_AES_192_CCM:
    #endif
            return CALG_AES_192;

        case MBEDTLS_CIPHER_AES_256_ECB:
        case MBEDTLS_CIPHER_AES_256_CBC:
        case MBEDTLS_CIPHER_AES_256_CFB128:
        case MBEDTLS_CIPHER_AES_256_CTR:
        case MBEDTLS_CIPHER_AES_256_GCM:
    #ifdef MBEDTLS_CCM_C
        case MBEDTLS_CIPHER_AES_256_CCM:
    #endif
            return CALG_AES_256;
#endif

        /* nothing to show? fall through */
        default:
        {
            FIXME("MBEDTLS schannel_get_cipher_algid: unknown algorithm %d\n", ciphersuite_id);
            return 0;
        }
    }
}

static ALG_ID schannel_get_mac_algid(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);

    switch (cipher_suite->mac)
    {
        case MBEDTLS_MD_NONE:      return 0;
        case MBEDTLS_MD_MD5:       return CALG_MD5;
        case MBEDTLS_MD_SHA1:      return CALG_SHA1;
        case MBEDTLS_MD_SHA224:    return CALG_SHA;
        case MBEDTLS_MD_SHA256:    return CALG_SHA_256;
        case MBEDTLS_MD_SHA384:    return CALG_SHA_384;
        case MBEDTLS_MD_SHA512:    return CALG_SHA_512;
        case MBEDTLS_MD_RIPEMD160: return (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_RIPEMD160); /* there's no CALG_RIPEMD or CALG_RIPEMD160 defined in <wincrypt.h> yet */

        default:
        {
            FIXME("MBEDTLS schannel_get_mac_algid: unknown algorithm %d\n", cipher_suite->mac);
            return 0;
        }
    }
}

static ALG_ID schannel_get_kx_algid(int ciphersuite_id)
{
    const mbedtls_ssl_ciphersuite_t *cipher_suite = mbedtls_ssl_ciphersuite_from_id(ciphersuite_id);

    switch (cipher_suite->key_exchange)
    {
        case MBEDTLS_KEY_EXCHANGE_NONE:
        case MBEDTLS_KEY_EXCHANGE_PSK: /* the original implementation does not support    */
            return 0;                  /* any PSK, and does not define any `CALG_PSK` :)  */

        case MBEDTLS_KEY_EXCHANGE_RSA:
        case MBEDTLS_KEY_EXCHANGE_RSA_PSK:
            return CALG_RSA_KEYX;

        case MBEDTLS_KEY_EXCHANGE_DHE_RSA:
        case MBEDTLS_KEY_EXCHANGE_DHE_PSK:
            return CALG_DH_EPHEM;

        case MBEDTLS_KEY_EXCHANGE_ECDH_RSA:
        case MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA:
            return CALG_ECDH;

        case MBEDTLS_KEY_EXCHANGE_ECDHE_RSA:
        case MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA:
        case MBEDTLS_KEY_EXCHANGE_ECDHE_PSK:
            return CALG_ECDH_EPHEM;

        default:
        {
            FIXME("MBEDTLS schannel_get_kx_algid: unknown algorithm %d\n", cipher_suite->key_exchange);
            return 0;
        }
    }
}

unsigned int schan_imp_get_session_cipher_block_size(schan_imp_session session)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    const mbedtls_ssl_ciphersuite_t *ssl_cipher_suite = mbedtls_ssl_ciphersuite_from_id(mbedtls_ssl_get_ciphersuite_id_from_ssl(&s->ssl));
    const mbedtls_cipher_info_t *cipher_info = ssl_cipher_suite ? mbedtls_cipher_info_from_type(ssl_cipher_suite->cipher) : NULL;
    unsigned int cipher_block_size = cipher_info ? mbedtls_cipher_info_get_block_size(cipher_info) : 0;

    TRACE("MBEDTLS schan_imp_get_session_cipher_block_size %p returning %u.\n", session, cipher_block_size);

    return cipher_block_size;
}

unsigned int schan_imp_get_max_message_size(schan_imp_session session)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    int max_payload = mbedtls_ssl_get_max_out_record_payload(&s->ssl);
    unsigned int max_frag_len = max_payload > 0 ? max_payload : 0;

    TRACE("MBEDTLS schan_imp_get_max_message_size %p returning %u.\n", session, max_frag_len);

    return max_frag_len;
}

SECURITY_STATUS schan_imp_get_connection_info(schan_imp_session session,
                                              SecPkgContext_ConnectionInfo *info)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;

    int ciphersuite_id = mbedtls_ssl_get_ciphersuite_id(mbedtls_ssl_get_ciphersuite(&s->ssl));

    TRACE("MBEDTLS schan_imp_get_connection_info %p %p.\n", session, info);

    info->dwProtocol       = schannel_get_protocol(&s->ssl, &s->conf);
    info->aiCipher         = schannel_get_cipher_algid(ciphersuite_id);
    info->dwCipherStrength = schannel_get_cipher_key_size(ciphersuite_id);
    info->aiHash           = schannel_get_mac_algid(ciphersuite_id);
    info->dwHashStrength   = schannel_get_mac_key_size(ciphersuite_id);
    info->aiExch           = schannel_get_kx_algid(ciphersuite_id);
    info->dwExchStrength   = schannel_get_kx_key_size(s, ciphersuite_id);

    return SEC_E_OK;
}

SECURITY_STATUS schan_imp_get_session_peer_certificate(schan_imp_session session, HCERTSTORE store,
                                                       PCCERT_CONTEXT *ret)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;
    PCCERT_CONTEXT cert_context = NULL;

    const mbedtls_x509_crt *next_cert;
    const mbedtls_x509_crt *peer_cert = mbedtls_ssl_get_peer_cert(&s->ssl);

    TRACE("MBEDTLS schan_imp_get_session_peer_certificate %p %p %p %p.\n", session, store, ret, ret != NULL ? *ret : NULL);

    if (!peer_cert)
        return SEC_E_INTERNAL_ERROR;

    for (next_cert = peer_cert; next_cert != NULL; next_cert = next_cert->next)
    {
        if (!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, next_cert->raw.p, next_cert->raw.len,
            CERT_STORE_ADD_REPLACE_EXISTING, (next_cert != peer_cert) ? NULL : &cert_context))
        {
            if (next_cert != peer_cert)
                CertFreeCertificateContext(cert_context);
            return GetLastError();
        }
    }

    *ret = cert_context;
    return SEC_E_OK;
}

SECURITY_STATUS schan_imp_send(schan_imp_session session, const void *buffer,
                               SIZE_T *length)
{
    MBEDTLS_SESSION *s = (MBEDTLS_SESSION *)session;
    int ret;

    ret = mbedtls_ssl_write(&s->ssl, (unsigned char *)buffer, *length);

    TRACE("MBEDTLS schan_imp_send: (%p, %p, %p/%lu)\n", s, buffer, length, *length);

    if (ret >= 0)
    {
        TRACE("MBEDTLS schan_imp_send: ret=%i.\n", ret);

        *length = ret;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        *length = 0;
        TRACE("MBEDTLS schan_imp_send: operation would block\n");
        return SEC_I_CONTINUE_NEEDED;
    }
    else
    {
        ERR("MBEDTLS schan_imp_send: mbedtls_ssl_write failed with -%x\n", -ret);
        return SEC_E_INTERNAL_ERROR;
    }

    return SEC_E_OK;
}

SECURITY_STATUS schan_imp_recv(schan_imp_session session, void *buffer,
                               SIZE_T *length)
{
    PMBEDTLS_SESSION s = (PMBEDTLS_SESSION)session;
    int ret;

    TRACE("MBEDTLS schan_imp_recv: (%p, %p, %p/%lu)\n", s, buffer, length, *length);

    ret = mbedtls_ssl_read(&s->ssl, (unsigned char *)buffer, *length);

    TRACE("MBEDTLS schan_imp_recv: (%p, %p, %p/%lu) ret= %#x\n", s, buffer, length, *length, ret);

    if (ret >= 0)
    {
        TRACE("MBEDTLS schan_imp_recv: ret == %i.\n", ret);

        *length = ret;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_READ && s->ssl.in_msgtype == MBEDTLS_SSL_MSG_HANDSHAKE &&
             mbedtls_ssl_is_handshake_over(&s->ssl))
    {
        *length = 0;
        TRACE("MBEDTLS schan_imp_recv: post-handshake message\n");
        return SEC_I_RENEGOTIATE;
    }
    else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        *length = 0;
        TRACE("MBEDTLS schan_imp_recv: operation would block\n");
        return SEC_I_CONTINUE_NEEDED;
    }
    else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
    {
        *length = 0;
        TRACE("MBEDTLS schan_imp_recv: ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY -> SEC_E_OK\n");
        return SEC_E_OK;
    }
    else
    {
        ERR("MBEDTLS schan_imp_recv: mbedtls_ssl_read failed with -%x\n", -ret);
        return SEC_E_INTERNAL_ERROR;
    }

    return SEC_E_OK;
}

BOOL schan_imp_allocate_certificate_credentials(schan_credentials *c)
{
    TRACE("MBEDTLS schan_imp_allocate_certificate_credentials %p %p %d\n", c, c->credentials, c->credential_use);

    /* in our case credentials aren't really used for anything, so just stub them */
    c->credentials = 0;
    return TRUE;
}

void schan_imp_free_certificate_credentials(schan_credentials *c)
{
    MBEDTLS_CREDENTIALS *creds = (MBEDTLS_CREDENTIALS *)(ULONG_PTR)c->credentials;

    TRACE("MBEDTLS schan_imp_free_certificate_credentials %p %p %d\n", c, c->credentials, c->credential_use);

    if (creds)
    {
        mbedtls_x509_crt_free(&creds->cert);
        mbedtls_pk_free(&creds->key);
        HeapFree(GetProcessHeap(), 0, creds);
        c->credentials = 0;
    }
}

BOOL schan_imp_init(void)
{
    TRACE("Schannel MBEDTLS schan_imp_init\n");
    return TRUE;
}

void schan_imp_deinit(void)
{
    WARN("Schannel MBEDTLS schan_imp_deinit\n");
}

static inline schan_imp_session session_from_handle(schan_session handle)
{
    return (schan_imp_session)(ULONG_PTR)handle;
}

static BOOL schan_import_rsa_key(mbedtls_pk_context *key, const BYTE *blob, ULONG size)
{
    const BCRYPT_RSAKEY_BLOB *hdr = (const BCRYPT_RSAKEY_BLOB *)blob;
    const BYTE *exponent, *modulus, *prime1, *prime2, *private_exponent;
    mbedtls_rsa_context *rsa;

    if (!blob || size < sizeof(*hdr) || hdr->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC)
        return FALSE;
    if (size - sizeof(*hdr) < (ULONGLONG)hdr->cbPublicExp + hdr->cbModulus * 2ULL +
                              hdr->cbPrime1 * 3ULL + hdr->cbPrime2 * 2ULL)
        return FALSE;

    exponent = (const BYTE *)(hdr + 1);
    modulus = exponent + hdr->cbPublicExp;
    prime1 = modulus + hdr->cbModulus;
    prime2 = prime1 + hdr->cbPrime1;
    private_exponent = prime2 + hdr->cbPrime2 + hdr->cbPrime1 + hdr->cbPrime2 + hdr->cbPrime1;

    if (mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)))
        return FALSE;

    rsa = mbedtls_pk_rsa(*key);
    if (mbedtls_rsa_import_raw(rsa, modulus, hdr->cbModulus, prime1, hdr->cbPrime1, prime2, hdr->cbPrime2,
                               private_exponent, hdr->cbModulus, exponent, hdr->cbPublicExp))
        return FALSE;

    return mbedtls_rsa_complete(rsa) == 0;
}

static NTSTATUS backend_allocate_certificate_credentials(void *args)
{
    const struct allocate_certificate_credentials_params *params = args;
    MBEDTLS_CREDENTIALS *creds;

    if (!params->cert_blob)
        return schan_imp_allocate_certificate_credentials(params->c) ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;

    if (!(creds = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*creds))))
        return STATUS_NO_MEMORY;

    mbedtls_x509_crt_init(&creds->cert);
    mbedtls_pk_init(&creds->key);
    if (mbedtls_x509_crt_parse_der(&creds->cert, params->cert_blob, params->cert_size) ||
        !schan_import_rsa_key(&creds->key, params->key_blob, params->key_size))
    {
        ERR("Failed to import the certificate or its private key\n");
        mbedtls_x509_crt_free(&creds->cert);
        mbedtls_pk_free(&creds->key);
        HeapFree(GetProcessHeap(), 0, creds);
        return STATUS_INTERNAL_ERROR;
    }

    params->c->credentials = (ULONG_PTR)creds;
    return STATUS_SUCCESS;
}

static NTSTATUS backend_create_session(void *args)
{
    const struct create_session_params *params = args;
    schan_imp_session session = NULL;

    *params->session = 0;
    if (!schan_imp_create_session(&session, params->cred))
        return STATUS_INTERNAL_ERROR;
    *params->session = (ULONG_PTR)session;
    return STATUS_SUCCESS;
}

static NTSTATUS backend_dispose_session(void *args)
{
    const struct session_params *params = args;

    schan_imp_dispose_session(session_from_handle(params->session));
    return STATUS_SUCCESS;
}

static NTSTATUS backend_free_certificate_credentials(void *args)
{
    const struct free_certificate_credentials_params *params = args;

    schan_imp_free_certificate_credentials(params->c);
    return STATUS_SUCCESS;
}

static NTSTATUS backend_get_application_protocol(void *args)
{
    const struct get_application_protocol_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    SecPkgContext_ApplicationProtocol *protocol = params->protocol;
    const char *selected = mbedtls_ssl_get_alpn_protocol(&session->ssl);
    SIZE_T length;

    memset(protocol, 0, sizeof(*protocol));
    if (!selected)
        return SEC_E_OK;

    length = strlen(selected);
    if (length > sizeof(protocol->ProtocolId))
        return SEC_E_INTERNAL_ERROR;

    protocol->ProtoNegoStatus = SecApplicationProtocolNegotiationStatus_Success;
    protocol->ProtoNegoExt = SecApplicationProtocolNegotiationExt_ALPN;
    protocol->ProtocolIdSize = length;
    memcpy(protocol->ProtocolId, selected, length);
    return SEC_E_OK;
}

static NTSTATUS backend_get_cipher_info(void *args)
{
    const struct get_cipher_info_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    SecPkgContext_CipherInfo *info = params->info;
    const char *name = mbedtls_ssl_get_ciphersuite(&session->ssl);
    int id = mbedtls_ssl_get_ciphersuite_id(name);
    const mbedtls_ssl_ciphersuite_t *suite = mbedtls_ssl_ciphersuite_from_id(id);
    const mbedtls_cipher_info_t *cipher = suite ? mbedtls_cipher_info_from_type(suite->cipher) : NULL;
    WCHAR *p;

    memset(info, 0, sizeof(*info));
    info->dwVersion = SECPKGCONTEXT_CIPHERINFO_V1;
    info->dwProtocol = mbedtls_ssl_get_version_number(&session->ssl);
    info->dwCipherSuite = id;
    info->dwBaseCipherSuite = id;
    info->dwCipherLen = schannel_get_cipher_key_size(id);
    info->dwCipherBlockLen = schan_imp_get_session_cipher_block_size(session);
    info->dwHashLen = schannel_get_mac_key_size(id);
    info->dwMinExchangeLen = schannel_get_kx_key_size(session, id);
    info->dwMaxExchangeLen = info->dwMinExchangeLen;
    MultiByteToWideChar(CP_ACP, 0, name, -1, info->szCipherSuite, ARRAY_SIZE(info->szCipherSuite));
    for (p = info->szCipherSuite; *p; p++)
        if (*p == '-') *p = '_';

    if (cipher && !strncmp(mbedtls_cipher_info_get_name(cipher), "AES-", 4))
        lstrcpyW(info->szCipher, BCRYPT_AES_ALGORITHM);

    if (suite)
    {
        switch (suite->key_exchange)
        {
            case MBEDTLS_KEY_EXCHANGE_ECDHE_RSA:
            case MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA:
                lstrcpyW(info->szExchange, BCRYPT_ECDH_ALGORITHM);
                info->dwMinExchangeLen = 0;
                info->dwMaxExchangeLen = 65536;
                break;
            case MBEDTLS_KEY_EXCHANGE_DHE_RSA:
                lstrcpyW(info->szExchange, BCRYPT_DH_ALGORITHM);
                break;
            default:
                break;
        }

        if (suite->key_exchange == MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA ||
            suite->key_exchange == MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA)
        {
            lstrcpyW(info->szCertificate, BCRYPT_ECDSA_ALGORITHM);
        }
    }
    return SEC_E_OK;
}

static NTSTATUS backend_get_connection_info(void *args)
{
    const struct get_connection_info_params *params = args;

    return schan_imp_get_connection_info(session_from_handle(params->session), params->info);
}

static NTSTATUS backend_get_key_signature_algorithm(void *args)
{
    const struct session_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    const char *name = mbedtls_ssl_get_ciphersuite(&session->ssl);
    const mbedtls_ssl_ciphersuite_t *suite = mbedtls_ssl_ciphersuite_from_id(
            mbedtls_ssl_get_ciphersuite_id(name));

    switch (suite->key_exchange)
    {
    case MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA:
    case MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA:
        return CALG_ECDSA;
    case MBEDTLS_KEY_EXCHANGE_RSA:
    case MBEDTLS_KEY_EXCHANGE_RSA_PSK:
    case MBEDTLS_KEY_EXCHANGE_DHE_RSA:
    case MBEDTLS_KEY_EXCHANGE_ECDHE_RSA:
        return CALG_RSA_SIGN;
    default:
        return 0;
    }
}

static NTSTATUS backend_get_unique_channel_binding(void *args)
{
    const struct get_unique_channel_binding_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    const char *verify_data = (mbedtls_ssl_conf_get_endpoint(&session->conf) == MBEDTLS_SSL_IS_CLIENT) ?
                              session->ssl.own_verify_data : session->ssl.peer_verify_data;
    ULONG size = session->ssl.verify_data_len;

    if (!size)
        return SEC_E_INTERNAL_ERROR;

    if (!params->buffer || *params->bufsize < size)
    {
        *params->bufsize = size;
        return SEC_E_BUFFER_TOO_SMALL;
    }

    memcpy(params->buffer, verify_data, size);
    *params->bufsize = size;
    return SEC_E_OK;
}

static NTSTATUS backend_get_session_peer_certificate(void *args)
{
    const struct get_session_peer_certificate_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    const mbedtls_x509_crt *certificate, *cursor;
    ULONG count = 0, size = 0, *sizes;
    BYTE *data;

    if (!(certificate = mbedtls_ssl_get_peer_cert(&session->ssl)))
        return SEC_E_INTERNAL_ERROR;

    for (cursor = certificate; cursor; cursor = cursor->next)
    {
        count++;
        size += cursor->raw.len;
    }
    size += count * sizeof(*sizes);

    if (!params->buffer || *params->bufsize < size)
    {
        *params->bufsize = size;
        return SEC_E_BUFFER_TOO_SMALL;
    }

    sizes = (ULONG *)params->buffer;
    data = params->buffer + count * sizeof(*sizes);
    count = 0;
    for (cursor = certificate; cursor; cursor = cursor->next)
    {
        sizes[count++] = cursor->raw.len;
        memcpy(data, cursor->raw.p, cursor->raw.len);
        data += cursor->raw.len;
    }

    *params->bufsize = size;
    *params->retcount = count;
    return SEC_E_OK;
}

static NTSTATUS backend_handshake(void *args)
{
    const struct handshake_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    struct schan_transport transport;
    SECURITY_STATUS status;

    init_schan_buffers(&transport.in, params->input);
    transport.in.limit = params->input_size;
    init_schan_buffers(&transport.out, params->output);
    session->transport = &transport;

    if (params->control_token == CONTROL_TOKEN_SHUTDOWN || params->control_token == CONTROL_TOKEN_ALERT)
    {
        int ret = mbedtls_ssl_send_alert_message(&session->ssl, (unsigned char)params->alert_type,
                                                 (unsigned char)params->alert_number);
        status = !ret || ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE
                ? SEC_E_OK : SEC_E_INTERNAL_ERROR;
    }
    else if (params->control_token)
    {
        status = SEC_E_UNSUPPORTED_FUNCTION;
    }
    else
    {
        status = schan_imp_handshake(session);
    }

    session->transport = NULL;
    *params->input_offset = transport.in.offset;
    *params->output_buffer_idx = transport.out.current_buffer_idx;
    *params->output_offset = transport.out.offset;
    return status;
}

static NTSTATUS backend_recv(void *args)
{
    const struct recv_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    struct schan_transport transport;
    SIZE_T length = *params->length;
    SECURITY_STATUS status;

    init_schan_buffers(&transport.in, params->input);
    transport.in.limit = params->input_size;
    init_schan_buffers(&transport.out, NULL);
    session->transport = &transport;
    status = schan_imp_recv(session, params->buffer, &length);
    session->transport = NULL;
    *params->length = length;
    return status;
}

static NTSTATUS backend_send(void *args)
{
    const struct send_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    struct schan_transport transport;
    SIZE_T length = params->length;
    SECURITY_STATUS status;

    init_schan_buffers(&transport.in, NULL);
    init_schan_buffers(&transport.out, params->output);
    session->transport = &transport;
    status = schan_imp_send(session, params->buffer, &length);
    session->transport = NULL;
    *params->output_buffer_idx = transport.out.current_buffer_idx;
    *params->output_offset = transport.out.offset;
    return status;
}

static NTSTATUS backend_set_application_protocols(void *args)
{
    const struct set_application_protocols_params *params = args;
    MBEDTLS_SESSION *session = session_from_handle(params->session);
    unsigned int extension_length, extension, offset = 0, list_length, count = 0, i;
    char **protocols;

    if (params->buflen < 2 * sizeof(ULONG) + sizeof(USHORT))
        return STATUS_INVALID_PARAMETER;

    extension_length = *(ULONG *)(params->buffer + offset);
    offset += sizeof(ULONG);
    extension = *(ULONG *)(params->buffer + offset);
    offset += sizeof(ULONG);
    if (extension != SecApplicationProtocolNegotiationExt_ALPN ||
        extension_length > params->buflen - sizeof(ULONG))
        return STATUS_NOT_SUPPORTED;

    list_length = *(USHORT *)(params->buffer + offset);
    offset += sizeof(USHORT);
    if (list_length > params->buflen - offset)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < list_length;)
    {
        unsigned int length = params->buffer[offset + i++];
        if (!length || length > list_length - i)
            return STATUS_INVALID_PARAMETER;
        i += length;
        count++;
    }

    if (!(protocols = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                (count + 1) * sizeof(*protocols))))
        return STATUS_NO_MEMORY;

    for (i = 0, count = 0; i < list_length; count++)
    {
        unsigned int length = params->buffer[offset + i++];
        if (!(protocols[count] = HeapAlloc(GetProcessHeap(), 0, length + 1)))
            goto failed;
        memcpy(protocols[count], params->buffer + offset + i, length);
        protocols[count][length] = 0;
        i += length;
    }

    if (mbedtls_ssl_conf_alpn_protocols(&session->conf, (const char **)protocols))
        goto failed;
    session->alpn_protocols = protocols;
    return STATUS_SUCCESS;

failed:
    for (i = 0; protocols[i]; i++)
        HeapFree(GetProcessHeap(), 0, protocols[i]);
    HeapFree(GetProcessHeap(), 0, protocols);
    return STATUS_INTERNAL_ERROR;
}

NTSTATUS schan_backend_call(enum schan_funcs func, void *params)
{
    switch (func)
    {
    case unix_process_attach:
        return schan_imp_init() ? STATUS_SUCCESS : STATUS_DLL_NOT_FOUND;
    case unix_process_detach:
        schan_imp_deinit();
        return STATUS_SUCCESS;
    case unix_allocate_certificate_credentials:
        return backend_allocate_certificate_credentials(params);
    case unix_create_session:
        return backend_create_session(params);
    case unix_dispose_session:
        return backend_dispose_session(params);
    case unix_free_certificate_credentials:
        return backend_free_certificate_credentials(params);
    case unix_get_application_protocol:
        return backend_get_application_protocol(params);
    case unix_get_cipher_info:
        return backend_get_cipher_info(params);
    case unix_get_connection_info:
        return backend_get_connection_info(params);
    case unix_get_enabled_protocols:
        return schan_imp_enabled_protocols();
    case unix_get_key_signature_algorithm:
        return backend_get_key_signature_algorithm(params);
    case unix_get_max_message_size:
        return schan_imp_get_max_message_size(session_from_handle(((struct session_params *)params)->session));
    case unix_get_session_cipher_block_size:
        return schan_imp_get_session_cipher_block_size(session_from_handle(((struct session_params *)params)->session));
    case unix_get_session_peer_certificate:
        return backend_get_session_peer_certificate(params);
    case unix_get_unique_channel_binding:
        return backend_get_unique_channel_binding(params);
    case unix_handshake:
        return backend_handshake(params);
    case unix_recv:
        return backend_recv(params);
    case unix_send:
        return backend_send(params);
    case unix_set_application_protocols:
        return backend_set_application_protocols(params);
    case unix_set_dtls_mtu:
    case unix_set_dtls_timeouts:
        return SEC_E_UNSUPPORTED_FUNCTION;
    case unix_set_session_target:
    {
        const struct set_session_target_params *target = params;
        schan_imp_set_session_target(session_from_handle(target->session), target->target);
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}

#endif /* SONAME_LIBMBEDTLS && !HAVE_SECURITY_SECURITY_H && !SONAME_LIBGNUTLS */
