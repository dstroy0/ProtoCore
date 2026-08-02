// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_tls13_msg.h
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
 * ride in the pc_quic_transport_parameters extension (codepoint 0x39, RFC 9001 sec 8.2).
 *
 * Pure, zero heap, host-tested against the RFC 8448 sec 3 ServerHello / Certificate / Finished bytes
 * and by ClientHello field extraction + an Ed25519 CertificateVerify sign/verify round-trip.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS13_MSG_H
#define PROTOCORE_TLS13_MSG_H

#include "protocore_config.h"

// Shared by the HTTP/3 (QUIC) handshake and the DTLS 1.3 handshake: both carry the same TLS 1.3
// messages, so this module compiles for either. The DTLS-specific additions (HelloRetryRequest, the
// cookie extension, the sec 4.4.1 message_hash) are used by the DTLS handshake but are valid TLS 1.3.
#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS)

/** @brief TLS handshake message types (RFC 8446 sec 4). */
#define O 1
#define O 2
#define S 8
#define E 11
#define Y 15
#define D 20

#define TLS_CIPHER_AES_128_GCM_SHA256 0x1301 ///< the one cipher suite we support
#define TLS_GROUP_X25519 0x001d              ///< the classical key-exchange group we support
#define TLS_GROUP_X25519MLKEM768 0x11ec      ///< PQ/T hybrid group (ML-KEM-768 + X25519), when PC_ENABLE_PQC_KEX
#define TLS_SIG_ED25519 0x0807               ///< the one signature scheme we produce
#define TLS_CERT_TYPE_X509 0                 ///< RFC 7250 CertificateType: X.509 (IANA "TLS Certificate Types" 0)
#define TLS_CERT_TYPE_RAW_PUBLIC_KEY 2    ///< RFC 7250 CertificateType: RawPublicKey (IANA 2), when PC_ENABLE_TLS_RPK
#define TLS_VERSION_1_3 0x0304            ///< supported_versions selected value (TLS 1.3)
#define PC_TLS_VERSION_DTLS_1_3 0xFEFC    ///< supported_versions selected value (DTLS 1.3, RFC 9147)
#define PC_TLS_LEGACY_VERSION_DTLS 0xFEFD ///< legacy_version on the wire for DTLS (DTLS 1.2)
#define TLS_EXT_QUIC_TRANSPORT_PARAMS 0x0039 ///< pc_quic_transport_parameters (RFC 9001 sec 8.2)

/** @brief What the state machine needs out of a parsed ClientHello (pointers alias the input). */
typedef struct
{
    const uint8_t *session_id; ///< legacy_session_id (echoed back in ServerHello)
    uint8_t session_id_len;
    uint8_t client_x25519[32]; ///< the client's X25519 key_share (valid iff has_key_share or has_hybrid_share)
    proto_bool has_key_share;
#if PC_ENABLE_PQC_KEX
    proto_bool offers_x25519mlkem768; ///< supported_groups contains X25519MLKEM768
    proto_bool has_hybrid_share;      ///< key_share carried an X25519MLKEM768 entry
    const uint8_t *client_mlkem_ek;   ///< the client's ML-KEM-768 encapsulation key (1184 B, aliases input)
#endif
    proto_bool offers_tls13;   ///< supported_versions contains 0x0304
    proto_bool offers_x25519;  ///< supported_groups contains x25519
    proto_bool offers_ed25519; ///< signature_algorithms contains ed25519
#if PC_ENABLE_TLS_RPK
    proto_bool offers_rpk_server_cert; ///< server_certificate_type (RFC 7250) offered RawPublicKey(2)
#endif
    proto_bool offers_h3_alpn; ///< ALPN contains "h3"
    const uint8_t *pc_quic_tp; ///< raw pc_quic_transport_parameters extension body (or NULL)
    size_t pc_quic_tp_len;
    const uint8_t *sni; ///< first server_name host_name (or NULL), not NUL-terminated
    size_t sni_len;
    const uint8_t *cookie; ///< cookie extension body echoed after a HelloRetryRequest (or NULL); DTLS §5.1
    size_t cookie_len;
    proto_bool has_conn_id; ///< the connection_id extension was present (RFC 9146 / RFC 9147 §9)
    const uint8_t *conn_id; ///< the CID the client wants the server to use in records sent to it (may be empty)
    size_t conn_id_len;     ///< length of @c conn_id (0..255; 0 = the client wants a zero-length CID)
} Tls13ClientHello;

/**
 * @brief Parse a ClientHello handshake message (@p msg includes the 4-byte handshake header).
 *
 * @param dtls  true for a DTLS ClientHello (RFC 9147 §5.3), which carries an extra @c legacy_cookie
 *              field between @c legacy_session_id and @c cipher_suites; false for TLS/QUIC.
 * @return false if it is not a well-formed ClientHello. Missing/!supported extensions are reported
 * through the offers_* flags rather than failing the parse, so the caller can send the right alert.
 */
proto_bool pc_tls13_parse_client_hello(const uint8_t *msg, size_t len, Tls13ClientHello *out,
                                       proto_bool dtls = PROTO_FALSE);

/**
 * @brief Build a ServerHello (RFC 8446 sec 4.1.3) selecting TLS 1.3 / AES-128-GCM-SHA256 and a
 * key_share for @p group.
 *
 * @param random          32-byte server random.
 * @param session_id      legacy_session_id_echo (the client's, echoed verbatim; may be NULL if len 0).
 * @param session_id_len  echoed session-id length (0..32).
 * @param share           the server's key_share (X25519 pub for the classical group, or the
 *                        ciphertext || X25519 concatenation for X25519MLKEM768).
 * @param share_len       length of @p share (32 for X25519, 1120 for the hybrid).
 * @param group           the selected named group (default TLS_GROUP_X25519).
 * @param conn_id          when non-NULL, emit a connection_id extension (RFC 9146 / RFC 9147 §9)
 *                         carrying the server's CID (the id the client must place in records it sends).
 * @param conn_id_len      length of @p conn_id (0..255).
 * @return bytes written, or 0 on overflow.
 */
size_t pc_tls13_build_server_hello(uint8_t *out, size_t cap, const uint8_t random[32], const uint8_t *session_id,
                                   uint8_t session_id_len, const uint8_t *share, size_t share_len = 32,
                                   uint16_t group = TLS_GROUP_X25519, proto_bool dtls = PROTO_FALSE,
                                   const uint8_t *conn_id = NULL, size_t conn_id_len = 0);

/**
 * @brief Build EncryptedExtensions (RFC 8446 sec 4.3.1) carrying ALPN "h3" and the server's
 * pc_quic_transport_parameters (@p pc_quic_tp, from pc_quic_tp_encode).
 *
 * @param rpk_server_cert  when true (PC_ENABLE_TLS_RPK), also emit the negotiated
 *                         server_certificate_type = RawPublicKey extension (RFC 7250 sec 4.2).
 * @return bytes written, or 0.
 */
size_t pc_tls13_build_encrypted_extensions(uint8_t *out, size_t cap, const uint8_t *pc_quic_tp, size_t pc_quic_tp_len,
                                           proto_bool rpk_server_cert = PROTO_FALSE);

/**
 * @brief Build a Certificate message (RFC 8446 sec 4.4.2) with an empty request context and one
 * CertificateEntry wrapping @p cert_der (DER X.509) with no entry extensions. @return bytes written.
 */
size_t pc_tls13_build_certificate(uint8_t *out, size_t cap, const uint8_t *cert_der, size_t cert_len);

#if PC_ENABLE_TLS_RPK
#define PC_TLS13_ED25519_SPKI_LEN 44 ///< DER SubjectPublicKeyInfo for an Ed25519 key (RFC 8410 sec 4)

/**
 * @brief Write the 44-byte DER @c SubjectPublicKeyInfo for the Ed25519 key @p pub (RFC 8410 sec 4):
 * the fixed AlgorithmIdentifier (id-Ed25519, OID 1.3.101.112) wrapping the 32-byte key in a BIT STRING.
 * This is the RFC 7250 RawPublicKey credential. @return PC_TLS13_ED25519_SPKI_LEN, or 0 on overflow.
 */
size_t pc_tls13_ed25519_spki(uint8_t *out, size_t cap, const uint8_t pub[32]);

/**
 * @brief Build a Certificate message (RFC 8446 sec 4.4.2) carrying an RFC 7250 RawPublicKey: one
 * CertificateEntry whose cert_data is the Ed25519 @c SubjectPublicKeyInfo of @p ed25519_pub (no X.509).
 * The peer must have negotiated server_certificate_type = RawPublicKey. @return bytes written, or 0.
 */
size_t pc_tls13_build_certificate_rpk(uint8_t *out, size_t cap, const uint8_t ed25519_pub[32]);
#endif

/**
 * @brief Build a CertificateVerify (RFC 8446 sec 4.4.3) with an Ed25519 signature.
 *
 * Signs the sec 4.4.3 content - 64 * 0x20, the context string "TLS 1.3, server CertificateVerify",
 * a 0x00 separator, then @p transcript_hash (Transcript-Hash of ClientHello..Certificate) - with the
 * Ed25519 key @p seed, and emits algorithm=ed25519, the 64-byte signature.
 *
 * @param transcript_hash  32-byte Transcript-Hash through the Certificate message.
 * @param seed             32-byte Ed25519 private seed.
 * @return bytes written, or 0 on overflow.
 */
size_t pc_tls13_build_cert_verify(uint8_t *out, size_t cap, const uint8_t transcript_hash[32], const uint8_t seed[32]);

/**
 * @brief Build a Finished message (RFC 8446 sec 4.4.4) carrying @p verify_data (from
 * pc_tls13_finished_mac). @return bytes written, or 0 on overflow.
 */
size_t pc_tls13_build_finished(uint8_t *out, size_t cap, const uint8_t verify_data[32]);

/**
 * @brief Assemble the sec 4.4.3 signed content into @p out (64*0x20 || context || 0x00 || hash).
 *
 * Exposed so the state machine can also verify a client's CertificateVerify if client auth is ever
 * added, and so it is directly unit-testable. @p is_server picks the "server"/"client" context word.
 * @return content length written (always 98 + 32), or 0 on overflow.
 */
size_t pc_tls13_cert_verify_content(uint8_t *out, size_t cap, const uint8_t transcript_hash[32], proto_bool is_server);

// ---------------------------------------------------------------------------
// HelloRetryRequest + cookie (RFC 8446 §4.1.4), used by the DTLS 1.3 handshake
// ---------------------------------------------------------------------------

/** @brief The fixed HelloRetryRequest random - SHA-256("HelloRetryRequest"), RFC 8446 §4.1.3. A
 *  ServerHello carrying this random _is_ a HelloRetryRequest. 32 bytes. */
extern const uint8_t pc_tls13_hrr_random[32];

/**
 * @brief Build a HelloRetryRequest (RFC 8446 §4.1.4): a ServerHello whose random is
 * @ref pc_tls13_hrr_random, selecting TLS 1.3 / AES-128-GCM-SHA256, asking the client to retry with a
 * key_share for @p selected_group, and echoing @p cookie in the cookie extension (§4.2.2).
 *
 * @param session_id      legacy_session_id_echo (the client's, echoed verbatim; may be NULL if len 0).
 * @param selected_group  the NamedGroup the server wants the client's key_share for.
 * @param cookie          the return-routability cookie the client must echo (may be NULL if len 0).
 * @param dtls            true to emit the DTLS 1.3 version codepoints (0xFEFD / 0xFEFC, RFC 9147 §5.3);
 *                        false for the TLS 1.3 ones (0x0303 / 0x0304).
 * @return bytes written, or 0 on overflow.
 */
size_t pc_tls13_build_hello_retry_request(uint8_t *out, size_t cap, const uint8_t *session_id, uint8_t session_id_len,
                                          uint16_t selected_group, const uint8_t *cookie, size_t cookie_len,
                                          proto_bool dtls = PROTO_FALSE);

/**
 * @brief Build an EncryptedExtensions (RFC 8446 §4.3.1) for the DTLS profile, which carries no ALPN or
 * transport parameters. With @p rpk_server_cert (PC_ENABLE_TLS_RPK) it carries the negotiated
 * server_certificate_type = RawPublicKey extension (RFC 7250 sec 4.2); otherwise the list is empty.
 * @return bytes written, or 0 on overflow.
 */
size_t pc_tls13_build_encrypted_extensions_empty(uint8_t *out, size_t cap, proto_bool rpk_server_cert = PROTO_FALSE);

/**
 * @brief Write the synthetic @c message_hash handshake message that replaces ClientHello1 in the
 * transcript when a HelloRetryRequest is used (RFC 8446 §4.4.1): @c message_hash (254), a 24-bit
 * length of 32, then @p ch1_hash. @return bytes written (36), or 0 on overflow.
 */
size_t pc_tls13_build_message_hash(uint8_t *out, size_t cap, const uint8_t ch1_hash[32]);

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS
#endif // PROTOCORE_TLS13_MSG_H
