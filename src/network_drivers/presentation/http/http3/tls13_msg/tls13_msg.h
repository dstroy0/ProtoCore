// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_msg.h
 * @brief TLS 1.3 handshake messages for the QUIC handshake (RFC 8446 sec 4).
 *
 * The wire formats of the handshake messages a QUIC server exchanges: it parses the client's
 * ClientHello and builds its own flight (ServerHello, then the encrypted EncryptedExtensions,
 * Certificate, CertificateVerify, and Finished). Every message is emitted whole, including the
 * 4-byte handshake header (msg_type + 24-bit length), because that is what both the CRYPTO stream
 * carries and the transcript hash covers.
 *
 * This server is deliberately a single, spec-valid profile: cipher suite TLS_AES_128_GCM_SHA256,
 * key share X25519, and an Ed25519 certificate (the only signature scheme we produce - the in-tree
 * crypto has Ed25519 but no ECDSA P-256 or RSA-PSS). A ClientHello that offers none of these is a
 * handshake failure, decided by the state machine that drives this module. QUIC transport parameters
 * ride in the quic_transport_parameters extension (codepoint 0x39, RFC 9001 sec 8.2).
 *
 * Pure, zero heap, host-tested against the RFC 8448 sec 3 ServerHello / Certificate / Finished bytes
 * and by ClientHello field extraction + an Ed25519 CertificateVerify sign/verify round-trip.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS13_MSG_H
#define PROTOCORE_TLS13_MSG_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)

PROTOCORE_BEGIN_DECLS

// Shared by the HTTP/3 (QUIC) handshake and the DTLS 1.3 handshake: both carry the same TLS 1.3
// messages, so this module compiles for either. The DTLS-specific additions (HelloRetryRequest, the
// cookie extension, the sec 4.4.1 message_hash) are used by the DTLS handshake but are valid TLS 1.3.
// The RFC 7250 RawPublicKey credential a Certificate may carry instead of an X.509 chain is
// tls13_rpk's.
//
// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief TLS handshake message types (RFC 8446 sec 4). */
#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_ENCRYPTED_EXTENSIONS 8
#define TLS_HS_CERTIFICATE 11
#define TLS_HS_CERTIFICATE_VERIFY 15
#define TLS_HS_FINISHED 20

// The two RFC 8446 sec B.4 mandatory-to-implement suites, as their IANA code points. These are the
// numbers on the wire; ::TlsCipher names the same two for the record layer and takes its values here.
#define PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256 0x1301 ///< AEAD_AES_128_GCM with a SHA-256 schedule
#define PROTOCORE_TLS_SUITE_AES_256_GCM_SHA384 0x1302 ///< AEAD_AES_256_GCM with a SHA-384 schedule
#define TLS_GROUP_X25519 0x001d                       ///< the classical key-exchange group we support
#define TLS_X25519_SHARE_LEN 32         ///< an X25519 key_share, and the shared secret it produces (RFC 7748 sec 6.1)
#define TLS_GROUP_X25519MLKEM768 0x11ec ///< PQ/T hybrid group (ML-KEM-768 + X25519), when PROTOCORE_ENABLE_PQC_KEX
#define TLS_SIG_ED25519 0x0807          ///< the one signature scheme we produce

// SignatureScheme code points a CertificateVerify may carry (RFC 8446 sec 4.2.3). The
// RSASSA-PKCS1-v1_5 values are deliberately absent: sec 4.2.3 says they "refer solely to signatures
// which appear in certificates ... and are not defined for use in signed TLS handshake messages".
#define TLS_SIG_ECDSA_SECP256R1_SHA256 0x0403 ///< ECDSA over P-256 with SHA-256
#define TLS_SIG_RSA_PSS_RSAE_SHA256 0x0804    ///< RSASSA-PSS with an rsaEncryption key
#define TLS_CERT_TYPE_X509 0                  ///< RFC 7250 CertificateType: X.509 (IANA "TLS Certificate Types" 0)
#define TLS_CERT_TYPE_RAW_PUBLIC_KEY                                                                                   \
    2                          ///< RFC 7250 CertificateType: RawPublicKey (IANA 2), when PROTOCORE_ENABLE_TLS_RPK
#define TLS_VERSION_1_3 0x0304 ///< supported_versions selected value (TLS 1.3)
#define PROTOCORE_TLS_VERSION_DTLS_1_3 0xFEFC    ///< supported_versions selected value (DTLS 1.3, RFC 9147)
#define PROTOCORE_TLS_LEGACY_VERSION_DTLS 0xFEFD ///< legacy_version on the wire for DTLS (DTLS 1.2)
#define TLS_EXT_QUIC_TRANSPORT_PARAMS 0x0039     ///< quic_transport_parameters (RFC 9001 sec 8.2)

/** @brief What the state machine needs out of a parsed ClientHello (pointers alias the input). */
typedef struct
{
    const uint8_t *session_id; ///< legacy_session_id (echoed back in ServerHello)
    uint8_t session_id_len;
    uint8_t client_x25519[32]; ///< the client's X25519 key_share (valid iff has_key_share or has_hybrid_share)
    proto_bool has_key_share;
#if PROTOCORE_ENABLE_PQC_KEX
    proto_bool offers_x25519mlkem768; ///< supported_groups contains X25519MLKEM768
    proto_bool has_hybrid_share;      ///< key_share carried an X25519MLKEM768 entry
    const uint8_t *client_mlkem_ek;   ///< the client's ML-KEM-768 encapsulation key (1184 B, aliases input)
#endif
    proto_bool offers_tls13;            ///< supported_versions contains 0x0304
    proto_bool offers_aes128gcm_sha256; ///< cipher_suites contains TLS_AES_128_GCM_SHA256
    proto_bool offers_aes256gcm_sha384; ///< cipher_suites contains TLS_AES_256_GCM_SHA384
    proto_bool offers_x25519;           ///< supported_groups contains x25519
    proto_bool offers_ed25519;          ///< signature_algorithms contains ed25519
    proto_bool has_server_cert_type;    ///< a server_certificate_type extension (RFC 7250) was present
    proto_bool offers_x509_server_cert; ///< that extension's list contained X509(0)
#if PROTOCORE_ENABLE_TLS_RPK
    proto_bool offers_rpk_server_cert; ///< server_certificate_type (RFC 7250) offered RawPublicKey(2)
#endif
    proto_bool offers_h3_alpn; ///< ALPN contains "h3"
    const uint8_t *alpn_list;  ///< the ProtocolNameList body (aliases input), or NULL when absent
    size_t alpn_list_len;      ///< how many bytes of it there are
    const uint8_t *quic_tp;    ///< raw quic_transport_parameters extension body (or NULL)
    size_t quic_tp_len;
    const uint8_t *sni; ///< first server_name host_name (or NULL), not NUL-terminated
    size_t sni_len;
    const uint8_t *cookie; ///< cookie extension body echoed after a HelloRetryRequest (or NULL); DTLS §5.1
    size_t cookie_len;
    proto_bool has_conn_id; ///< the connection_id extension was present (RFC 9146 / RFC 9147 §9)
    const uint8_t *conn_id; ///< the CID the client wants the server to use in records sent to it (may be empty)
    size_t conn_id_len;     ///< length of @c conn_id (0..255; 0 = the client wants a zero-length CID)
} Tls13ClientHello;

/** @brief A parsed ServerHello (RFC 8446 sec 4.1.3). Pointer fields alias the input, not copied. */
typedef struct
{
    const uint8_t *random;     ///< the 32-byte Random (aliases input)
    const uint8_t *session_id; ///< legacy_session_id_echo (aliases input)
    uint8_t session_id_len;
    uint16_t cipher_suite;     ///< the suite the server selected
    proto_bool selected_tls13; ///< supported_versions carried the 1.3 code point
    proto_bool is_hrr;         ///< the Random is the fixed HelloRetryRequest value (sec 4.1.3)
    proto_bool has_key_share;  ///< a key_share extension was present
    uint16_t group;            ///< its NamedGroup: the server's share, or a HelloRetryRequest's selected_group
    const uint8_t *share;      ///< the server's key_exchange (aliases input); NULL in a HelloRetryRequest
    size_t share_len;
    const uint8_t *cookie; ///< cookie to echo in the retried ClientHello (aliases input), or NULL
    size_t cookie_len;
    proto_bool has_conn_id; ///< a connection_id extension was present (RFC 9146 / RFC 9147 §9)
    const uint8_t *conn_id; ///< the CID to place in records sent to the server (aliases input)
    size_t conn_id_len;
} Tls13ServerHello;

/** @brief The fixed HelloRetryRequest random - SHA-256("HelloRetryRequest"), RFC 8446 §4.1.3. A
 *  ServerHello carrying this random _is_ a HelloRetryRequest. 32 bytes. */
extern const uint8_t protocore_tls13_hrr_random[32];

/** @brief What parse_client_hello takes: msg, len, out, dtls. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Tls13ClientHello *out;
    proto_bool dtls; ///< true for a DTLS ClientHello (RFC 9147 §5.3), which carries an extra legacy_cookie field ...
} Tls13MsgParseClientHelloArgs;

/** @brief What parse_server_hello takes: msg, len, out, dtls. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Tls13ServerHello *out;
    proto_bool dtls; ///< true for DTLS, whose supported_versions selection is 0xFEFC (RFC 9147 §5.3)
} Tls13MsgParseServerHelloArgs;

/** @brief What build_client_hello takes: out, cap, random, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *random; ///< 32 bytes.
    const uint8_t *session_id;
    uint8_t session_id_len;
    const uint8_t *share;
    size_t share_len;
    uint16_t group;
    uint16_t suite; ///< the one cipher suite offered, as its IANA code point
    const char *sni;
    const char *alpn;
    const uint8_t *cookie;
    size_t cookie_len;
    proto_bool rpk_server_cert;
    proto_bool dtls;
} Tls13MsgBuildClientHelloArgs;

/** @brief What parse_certificate takes: msg, len, cert, cert_len. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    const uint8_t **cert;
    size_t *cert_len;
} Tls13MsgParseCertificateArgs;

/** @brief What parse_cert_verify takes: msg, len, scheme, sig, sig_len. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    uint16_t *scheme;
    const uint8_t **sig;
    size_t *sig_len;
} Tls13MsgParseCertVerifyArgs;

/** @brief What parse_finished takes: msg, len, vd, verify_len. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    const uint8_t **vd;
    size_t verify_len; ///< the suite's Hash.length, which sec 4.4.4 makes the body's exact size
} Tls13MsgParseFinishedArgs;

/** @brief What build_server_hello takes: out, cap, random, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *random;     ///< 32-byte server random 32 bytes.
    const uint8_t *session_id; ///< legacy_session_id_echo (the client's, echoed verbatim; may be NULL if len 0)
    uint8_t session_id_len;    ///< echoed session-id length (0..32)
    const uint8_t
        *share;       ///< the server's key_share (X25519 pub for the classical group, or the ciphertext || X25519 ...
    size_t share_len; ///< length of share (32 for X25519, 1120 for the hybrid)
    uint16_t group;   ///< the selected named group (TLS_GROUP_X25519 or TLS_GROUP_X25519MLKEM768)
    uint16_t suite;   ///< the selected cipher suite, as its IANA code point
    proto_bool dtls;  ///< true to emit the DTLS 1.3 version codepoints (RFC 9147 §5.3), false for TLS 1.3
    const uint8_t *conn_id; ///< when non-NULL, emit a connection_id extension (RFC 9146 / RFC 9147 §9) carrying the ...
    size_t conn_id_len;     ///< length of conn_id (0..255)
} Tls13MsgBuildServerHelloArgs;

/** @brief What build_encrypted_extensions takes: out, cap, quic_tp, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *quic_tp;
    size_t quic_tp_len;
    proto_bool rpk_server_cert; ///< when true (PROTOCORE_ENABLE_TLS_RPK), also emit the negotiated
                                ///< server_certificate_type = ...
} Tls13MsgBuildEncryptedExtensionsArgs;

/** @brief What build_certificate takes: out, cap, cert_der, cert_len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *cert_der;
    size_t cert_len;
} Tls13MsgBuildCertificateArgs;

/** @brief What build_cert_verify takes: sign_work, out, cap, ... */
typedef struct
{
    uint8_t *sign_work;
    uint8_t *out;
    size_t cap;
    const uint8_t *transcript_hash; ///< Transcript-Hash through the Certificate message
    size_t hash_len;                ///< its length: the suite's Hash.length, at most PROTOCORE_TLS13_SECRET_MAX
    const uint8_t *seed;            ///< 32-byte Ed25519 private seed 32 bytes.
} Tls13MsgBuildCertVerifyArgs;

/** @brief What build_finished takes: out, cap, verify_data, verify_len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *verify_data;
    size_t verify_len;
} Tls13MsgBuildFinishedArgs;

/** @brief What cert_verify_content takes: out, cap, transcript_hash, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *transcript_hash;
    size_t hash_len;
    proto_bool is_server;
} Tls13MsgCertVerifyContentArgs;

/** @brief What build_hello_retry_request takes: out, cap, session_id, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *session_id; ///< legacy_session_id_echo (the client's, echoed verbatim; may be NULL if len 0)
    uint8_t session_id_len;
    uint16_t selected_group; ///< the NamedGroup the server wants the client's key_share for
    uint16_t suite; ///< the selected cipher suite, as its IANA code point; §4.1.4 makes the retried ServerHello ...
    const uint8_t *cookie; ///< the return-routability cookie the client must echo (may be NULL if len 0)
    size_t cookie_len;
    proto_bool dtls; ///< true to emit the DTLS 1.3 version codepoints (0xFEFD / 0xFEFC, RFC 9147 §5.3); false for ...
} Tls13MsgBuildHelloRetryRequestArgs;

/** @brief What build_encrypted_extensions_empty takes: out, cap, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    proto_bool rpk_server_cert;
    const char *alpn;
} Tls13MsgBuildEncryptedExtensionsEmptyArgs;

/** @brief What build_message_hash takes: out, cap, ch1_hash. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *ch1_hash; ///< 32 bytes.
} Tls13MsgBuildMessageHashArgs;

/**
 * @brief TLS 1.3 handshake messages for the QUIC handshake (RFC 8446 sec 4).
 *
 * A caller sets the members a call takes, invokes it through ::Tls13Msg with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Tls13Msg.parse_client_hello_args.msg = ...;
 *   Tls13Msg.parse_client_hello_args.len = ...;
 *   Tls13Msg.parse_client_hello_args.out = ...;
 *   Tls13Msg.parse_client_hello_args.dtls = ...;
 *   Tls13Msg.parse_client_hello(work);
 *   // Tls13Msg.ok is what the call reports
 *
 * @var Tls13MsgNs::parse_client_hello_args  what parse_client_hello takes: msg, len, out, dtls
 * @var Tls13MsgNs::parse_server_hello_args  what parse_server_hello takes: msg, len, out, dtls
 * @var Tls13MsgNs::build_client_hello_args  what build_client_hello takes: out, cap, random,
 * @var Tls13MsgNs::parse_certificate_args  what parse_certificate takes: msg, len, cert, cert_len
 * @var Tls13MsgNs::parse_cert_verify_args  what parse_cert_verify takes: msg, len, scheme, sig, sig_len
 * @var Tls13MsgNs::parse_finished_args  what parse_finished takes: msg, len, vd, verify_len
 * @var Tls13MsgNs::build_server_hello_args  what build_server_hello takes: out, cap, random,
 * @var Tls13MsgNs::build_encrypted_extensions_args  what build_encrypted_extensions takes: out, cap, quic_tp,
 * @var Tls13MsgNs::build_certificate_args  what build_certificate takes: out, cap, cert_der, cert_len
 * @var Tls13MsgNs::build_cert_verify_args  what build_cert_verify takes: sign_work, out, cap,
 * @var Tls13MsgNs::build_finished_args  what build_finished takes: out, cap, verify_data, verify_len
 * @var Tls13MsgNs::cert_verify_content_args  what cert_verify_content takes: out, cap, transcript_hash,
 * @var Tls13MsgNs::build_hello_retry_request_args  what build_hello_retry_request takes: out, cap, session_id,
 * @var Tls13MsgNs::build_encrypted_extensions_empty_args  what build_encrypted_extensions_empty takes: out, cap,
 * @var Tls13MsgNs::build_message_hash_args  what build_message_hash takes: out, cap, ch1_hash
 * @var Tls13MsgNs::ok  false if it is not a well-formed ClientHello. Missing/!supported ...
 * @var Tls13MsgNs::n  bytes written, or 0 on overflow
 * @var Tls13MsgNs::parse_client_hello  parse a ClientHello handshake message (msg includes the 4-byte ...
 * @var Tls13MsgNs::parse_server_hello  parse a ServerHello handshake message (msg includes the 4-byte ...
 * @var Tls13MsgNs::build_client_hello  build a ClientHello (RFC 8446 sec 4.1.2) offering TLS 1.3, suite, ...
 * @var Tls13MsgNs::parse_certificate  the first CertificateEntry's cert_data in a Certificate message ...
 * @var Tls13MsgNs::parse_cert_verify  the algorithm and signature of a CertificateVerify (RFC 8446 sec ...
 * @var Tls13MsgNs::parse_finished  the verify_data of a Finished (RFC 8446 sec 4.4.4). vd points into ...
 * @var Tls13MsgNs::build_server_hello  build a ServerHello (RFC 8446 sec 4.1.3) selecting TLS 1.3 / ...
 * @var Tls13MsgNs::build_encrypted_extensions  build EncryptedExtensions (RFC 8446 sec 4.3.1) carrying ALPN "h3" ...
 * @var Tls13MsgNs::build_certificate  build a Certificate message (RFC 8446 sec 4.4.2) with an empty ...
 * @var Tls13MsgNs::build_cert_verify  build a CertificateVerify (RFC 8446 sec 4.4.3) with an Ed25519 ...
 * @var Tls13MsgNs::build_finished  build a Finished message (RFC 8446 sec 4.4.4) carrying verify_data ...
 * @var Tls13MsgNs::cert_verify_content  assemble the sec 4.4.3 signed content into out (64*0x20 || context ...
 * @var Tls13MsgNs::build_hello_retry_request  build a HelloRetryRequest (RFC 8446 §4.1.4): a ServerHello whose ...
 * @var Tls13MsgNs::build_encrypted_extensions_empty  build an EncryptedExtensions (RFC 8446 §4.3.1) for the DTLS ...
 * @var Tls13MsgNs::build_message_hash  write the synthetic message_hash handshake message that replaces ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Tls13MsgParseClientHelloArgs parse_client_hello_args;
    Tls13MsgParseServerHelloArgs parse_server_hello_args;
    Tls13MsgBuildClientHelloArgs build_client_hello_args;
    Tls13MsgParseCertificateArgs parse_certificate_args;
    Tls13MsgParseCertVerifyArgs parse_cert_verify_args;
    Tls13MsgParseFinishedArgs parse_finished_args;
    Tls13MsgBuildServerHelloArgs build_server_hello_args;
    Tls13MsgBuildEncryptedExtensionsArgs build_encrypted_extensions_args;
    Tls13MsgBuildCertificateArgs build_certificate_args;
    Tls13MsgBuildCertVerifyArgs build_cert_verify_args;
    Tls13MsgBuildFinishedArgs build_finished_args;
    Tls13MsgCertVerifyContentArgs cert_verify_content_args;
    Tls13MsgBuildHelloRetryRequestArgs build_hello_retry_request_args;
    Tls13MsgBuildEncryptedExtensionsEmptyArgs build_encrypted_extensions_empty_args;
    Tls13MsgBuildMessageHashArgs build_message_hash_args;
    proto_bool ok;
    size_t n;
} Tls13MsgVars;

/** @brief The operands and the outcome. */
extern Tls13MsgVars Tls13MsgV;

/** @brief The entries. */
typedef struct
{
    void (*const parse_client_hello)(uint8_t *restrict work);
    void (*const parse_server_hello)(uint8_t *restrict work);
    void (*const build_client_hello)(uint8_t *restrict work);
    void (*const parse_certificate)(uint8_t *restrict work);
    void (*const parse_cert_verify)(uint8_t *restrict work);
    void (*const parse_finished)(uint8_t *restrict work);
    void (*const build_server_hello)(uint8_t *restrict work);
    void (*const build_encrypted_extensions)(uint8_t *restrict work);
    void (*const build_certificate)(uint8_t *restrict work);
    void (*const build_cert_verify)(uint8_t *restrict work);
    void (*const build_finished)(uint8_t *restrict work);
    void (*const cert_verify_content)(uint8_t *restrict work);
    void (*const build_hello_retry_request)(uint8_t *restrict work);
    void (*const build_encrypted_extensions_empty)(uint8_t *restrict work);
    void (*const build_message_hash)(uint8_t *restrict work);
} Tls13MsgNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Tls13MsgV or a region of the borrow at a fixed offset.
void protocore_tls13_msg_parse_client_hello(uint8_t *restrict work);
void protocore_tls13_msg_parse_server_hello(uint8_t *restrict work);
void protocore_tls13_msg_build_client_hello(uint8_t *restrict work);
void protocore_tls13_msg_parse_certificate(uint8_t *restrict work);
void protocore_tls13_msg_parse_cert_verify(uint8_t *restrict work);
void protocore_tls13_msg_parse_finished(uint8_t *restrict work);
void protocore_tls13_msg_build_server_hello(uint8_t *restrict work);
void protocore_tls13_msg_build_encrypted_extensions(uint8_t *restrict work);
void protocore_tls13_msg_build_certificate(uint8_t *restrict work);
void protocore_tls13_msg_build_cert_verify(uint8_t *restrict work);
void protocore_tls13_msg_build_finished(uint8_t *restrict work);
void protocore_tls13_msg_cert_verify_content(uint8_t *restrict work);
void protocore_tls13_msg_build_hello_retry_request(uint8_t *restrict work);
void protocore_tls13_msg_build_encrypted_extensions_empty(uint8_t *restrict work);
void protocore_tls13_msg_build_message_hash(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Tls13Msg.parse_client_hello(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Tls13MsgNs Tls13Msg __attribute__((unused)) = {
    .parse_client_hello = protocore_tls13_msg_parse_client_hello,
    .parse_server_hello = protocore_tls13_msg_parse_server_hello,
    .build_client_hello = protocore_tls13_msg_build_client_hello,
    .parse_certificate = protocore_tls13_msg_parse_certificate,
    .parse_cert_verify = protocore_tls13_msg_parse_cert_verify,
    .parse_finished = protocore_tls13_msg_parse_finished,
    .build_server_hello = protocore_tls13_msg_build_server_hello,
    .build_encrypted_extensions = protocore_tls13_msg_build_encrypted_extensions,
    .build_certificate = protocore_tls13_msg_build_certificate,
    .build_cert_verify = protocore_tls13_msg_build_cert_verify,
    .build_finished = protocore_tls13_msg_build_finished,
    .cert_verify_content = protocore_tls13_msg_cert_verify_content,
    .build_hello_retry_request = protocore_tls13_msg_build_hello_retry_request,
    .build_encrypted_extensions_empty = protocore_tls13_msg_build_encrypted_extensions_empty,
    .build_message_hash = protocore_tls13_msg_build_message_hash,
};

PROTOCORE_END_DECLS

#endif // (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)

#endif // PROTOCORE_TLS13_MSG_H
