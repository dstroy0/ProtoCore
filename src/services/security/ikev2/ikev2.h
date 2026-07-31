// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2.h
 * @brief IKEv2 (RFC 7296) message + payload codec (PC_ENABLE_IKEV2) - a zero-heap builder / parser for
 *        the Internet Key Exchange v2 wire format that negotiates IPsec security associations over UDP
 *        500 / 4500 (NAT-T). This is the "secure machine bridge over untrusted networks" southbound: the
 *        framing tier of an IKEv2/IPsec stack.
 *
 * Scope (tier 1 of the IPsec roadmap item): the pure wire codec only - build / parse into caller
 * buffers, no sockets and no crypto. It frames the 28-octet IKE header and the generic payload chain
 * (SA -> proposals -> transforms, KE, Ni/Nr, IDi/IDr, CERT/CERTREQ, AUTH, N notify, D delete, TSi/TSr,
 * and the SK encrypted-payload envelope). Tier 2's crypto lives here too: the SKEYSEED / SK_* key
 * derivation (RFC 7296 §2.13-2.14, prf+ over HMAC-SHA2-256), the SK-payload AEAD (RFC 5282
 * AES-256-GCM-16), the Diffie-Hellman shared secret (RFC 7296 §2.7, group 31 = X25519), PSK + ECDSA-P256
 * authentication (§2.15 / RFC 7427), the IKE_SA_INIT + IKE_AUTH message assembly (§1.2 / §3.14), and the
 * SA / Child-SA key schedules (§2.14 / §2.17). On top of those sits a full stateful handshake DRIVER -
 * both the initiator and responder complete IKE_SA_INIT + IKE_AUTH to IKE_ST_ESTABLISHED with mutual PSK
 * auth - plus the post-auth INFORMATIONAL (DPD / Delete / Notify) and CREATE_CHILD_SA exchanges over the
 * established SA. Remaining for tier 2: rekey/retransmit orchestration and RSA-signature auth. Tier 3 (the
 * ESP datapath, a network-layer transform that hooks lwIP) is a separate, later track. All the crypto
 * reuses primitives the library already ships.
 *
 * Wire framing (byte-exact, network byte order): the IKE header is 8-byte Initiator SPI, 8-byte
 * Responder SPI, 1-byte Next Payload, 1-byte Version (0x20 = MjVer 2 / MnVer 0), 1-byte Exchange Type,
 * 1-byte Flags, 4-byte Message ID, 4-byte Length (whole message). Every payload starts with the same
 * 4-byte generic header: Next Payload (the type of the FOLLOWING payload, so the chain is walked
 * forward from the header's Next Payload), a Critical bit + 7 reserved bits, and a 2-byte Payload Length
 * (this payload incl. the generic header). Multi-byte fields are big-endian throughout.
 *
 * Reference: RFC 7296 (IKEv2) + the IANA "Internet Key Exchange Version 2 (IKEv2) Parameters" registry.
 * Every structure was cross-checked byte-for-byte against scapy's IKEv2 contrib (the header, KE, Nonce,
 * Notify, Delete, and the SA -> proposal -> transform tree, including the key-length transform attribute
 * verified against scapy's decoder).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IKEV2_H
#define PROTOCORE_IKEV2_H

#include "protocore_config.h"

#if PC_ENABLE_IKEV2

#include <stddef.h>
#include <stdint.h>

/** @brief IKEv2 UDP port (IKE_SA_INIT / IKE_AUTH before NAT is detected). */
#define PC_IKEV2_PORT 500
/** @brief IKEv2 UDP port after NAT traversal is negotiated (NAT-T, RFC 3948). */
#define PC_IKEV2_NAT_PORT 4500
/** @brief Fixed IKE header size. */
#define PC_IKE_HDR_LEN 28
/** @brief IKE SPI size (Initiator / Responder). */
#define PC_IKE_SPI_LEN 8
/** @brief Generic payload header size (next-payload + critical/reserved + 2-byte length). */
#define PC_IKE_PAYLOAD_HDR_LEN 4
/** @brief IKE version byte value: major 2, minor 0. */
#define PC_IKE_VERSION 0x20
/** @brief Critical bit in a payload's second header byte. */
#define PC_IKE_CRITICAL 0x80

/** @brief IKE header flag bits (RFC 7296 3.1). */
#define PC_IKE_FLAG_INITIATOR 0x08 ///< set in messages from the original initiator
#define PC_IKE_FLAG_VERSION 0x10   ///< set if a higher minor version is supported
#define PC_IKE_FLAG_RESPONSE 0x20  ///< set in a response (clear in a request)

/** @brief Exchange types (RFC 7296 3.1). */
enum class IkeExchange : uint8_t
{
    IKE_SA_INIT = 34,
    IKE_AUTH = 35,
    IKE_CREATE_CHILD_SA = 36,
    IKE_INFORMATIONAL = 37,
};

/** @brief Payload types / Next Payload values (RFC 7296 3.2); 0 terminates a chain. */
enum class IkePayloadType : uint8_t
{
    IKE_PL_NONE = 0,
    IKE_PL_SA = 33,      ///< Security Association (proposals / transforms)
    IKE_PL_KE = 34,      ///< Key Exchange
    IKE_PL_IDI = 35,     ///< Identification - Initiator
    IKE_PL_IDR = 36,     ///< Identification - Responder
    IKE_PL_CERT = 37,    ///< Certificate
    IKE_PL_CERTREQ = 38, ///< Certificate Request
    IKE_PL_AUTH = 39,    ///< Authentication
    IKE_PL_NONCE = 40,   ///< Nonce (Ni / Nr)
    IKE_PL_NOTIFY = 41,  ///< Notify
    IKE_PL_DELETE = 42,  ///< Delete
    IKE_PL_VENDOR = 43,  ///< Vendor ID
    IKE_PL_TSI = 44,     ///< Traffic Selector - Initiator
    IKE_PL_TSR = 45,     ///< Traffic Selector - Responder
    IKE_PL_SK = 46,      ///< Encrypted and Authenticated
    IKE_PL_CP = 47,      ///< Configuration
    IKE_PL_EAP = 48,     ///< Extensible Authentication
    IKE_PL_SKF = 53,     ///< Encrypted and Authenticated Fragment (RFC 7383)
};

/** @brief Transform types (RFC 7296 3.3.2). */
enum class IkeTransformType : uint8_t
{
    IKE_TRANSFORM_ENCR = 1,  ///< Encryption Algorithm
    IKE_TRANSFORM_PRF = 2,   ///< Pseudo-random Function
    IKE_TRANSFORM_INTEG = 3, ///< Integrity Algorithm
    IKE_TRANSFORM_DH = 4,    ///< Diffie-Hellman / Key Exchange Method
    IKE_TRANSFORM_ESN = 5,   ///< Extended Sequence Numbers
};

/** @brief A few common Transform IDs (IANA), for convenience; any 16-bit id is accepted on the wire. */
#define IKE_ENCR_AES_CBC 12
#define IKE_ENCR_AES_GCM_16 20
#define IKE_ENCR_CHACHA20_POLY1305 28
#define IKE_PRF_HMAC_SHA2_256 5
#define IKE_INTEG_HMAC_SHA2_256_128 12
#define IKE_DH_MODP2048 14
#define IKE_DH_ECP256 19
#define IKE_DH_CURVE25519 31

/** @brief Transform attribute type: Key Length (RFC 7296 3.3.5), encoded TV (AF bit set). */
#define IKE_ATTR_KEY_LENGTH 14

/** @brief Protocol IDs (RFC 7296 3.3.1 / 3.10 / 3.11). */
enum class IkeProtocol : uint8_t
{
    IKE_PROTO_NONE = 0, ///< notify/delete not concerning an existing SA (RFC 7296 3.10)
    IKE_PROTO_IKE = 1,
    IKE_PROTO_AH = 2,
    IKE_PROTO_ESP = 3,
};

/** @brief Identification payload ID types (RFC 7296 3.5). */
enum class IkeIdType : uint8_t
{
    IKE_ID_RESERVED = 0, ///< IANA-reserved; the value an out-param holds before a parse succeeds
    IKE_ID_IPV4_ADDR = 1,
    IKE_ID_FQDN = 2,
    IKE_ID_RFC822_ADDR = 3,
    IKE_ID_IPV6_ADDR = 5,
    IKE_ID_KEY_ID = 11,
};

/** @brief Authentication methods (RFC 7296 3.8 / IANA). */
enum class IkeAuthMethod : uint8_t
{
    IKE_AUTH_RESERVED = 0,    ///< IANA-reserved; the value an out-param holds before a parse succeeds
    IKE_AUTH_RSA_SIG = 1,     ///< RSA Digital Signature
    IKE_AUTH_PSK = 2,         ///< Shared Key Message Integrity Code (pre-shared key)
    IKE_AUTH_DSS_SIG = 3,     ///< DSS Digital Signature
    IKE_AUTH_DIGITAL_SIG = 14 ///< Generic Digital Signature (RFC 7427)
};

/** @brief Traffic selector types (RFC 7296 3.13.1). */
enum class IkeTsType : uint8_t
{
    IKE_TS_IPV4_ADDR_RANGE = 7,
    IKE_TS_IPV6_ADDR_RANGE = 8,
};

/** @brief A decoded / to-be-encoded IKE header. */
struct IkeHeader
{
    uint8_t init_spi[PC_IKE_SPI_LEN];
    uint8_t resp_spi[PC_IKE_SPI_LEN];
    IkePayloadType next_payload; ///< type of the first payload in the message
    uint8_t version;             ///< 0x20 for IKEv2
    IkeExchange exchange;        ///< @ref IkeExchange
    uint8_t flags;               ///< OR of PC_IKE_FLAG_*
    uint32_t message_id;
    uint32_t length; ///< whole-message length (built value; on parse, the value read off the wire)
};

/** @brief A parsed generic payload: its type (from the chain), the following type, and the body slice
 *  (the bytes after the 4-byte generic header), pointing INTO the caller's buffer. */
struct IkePayload
{
    IkePayloadType type;         ///< this payload's type
    IkePayloadType next_payload; ///< type of the next payload (IKE_PL_NONE = last)
    bool critical;               ///< the Critical bit
    const uint8_t *body;
    size_t body_len;
};

/** @brief Forward-walks the payload chain of a message. */
struct IkePayloadIter
{
    const uint8_t *area;      ///< payload area (message + PC_IKE_HDR_LEN)
    size_t len;               ///< bytes remaining in the area
    size_t off;               ///< current offset into @ref area
    IkePayloadType next_type; ///< type of the payload at @ref off (IKE_PL_NONE = done)
};

/** @brief One transform to encode inside a proposal (@ref pc_ike_sa_build). */
struct IkeTransform
{
    IkeTransformType type; ///< which transform slot this fills
    uint16_t id;           ///< transform id (algorithm)
    int32_t key_length;    ///< key-length attribute in bits, or < 0 for none
};

/** @brief A parsed transform (from @ref pc_ike_transform_next). */
struct IkeTransformRef
{
    IkeTransformType type;
    uint16_t id;
    int32_t key_length; ///< decoded key-length attribute, or < 0 if absent
    bool last;          ///< true if this is the last transform in the proposal
};

/** @brief A parsed proposal (from @ref pc_ike_sa_first_proposal). */
struct IkeProposalRef
{
    uint8_t proposal_num;
    IkeProtocol protocol_id; ///< which SA this proposal is for
    uint8_t spi_size;
    uint8_t num_transforms;
    const uint8_t *spi; ///< SPI bytes (spi_size long), or nullptr
    const uint8_t *transforms;
    size_t transforms_len;
    bool last; ///< true if this is the only / last proposal
};

/** @brief Iterates the transforms within a proposal's transform area. */
struct IkeTransformIter
{
    const uint8_t *area;
    size_t len;
    size_t off;
};

/** @brief One traffic selector (RFC 7296 3.13.1). */
struct IkeTrafficSelector
{
    IkeTsType ts_type;   ///< selector address family
    uint8_t ip_protocol; ///< 0 = any
    uint16_t start_port;
    uint16_t end_port;
    const uint8_t *start_addr; ///< 4 bytes (IPv4) or 16 bytes (IPv6)
    const uint8_t *end_addr;
    size_t addr_len; ///< 4 or 16
};

/** @brief Configuration payload CFG Type (RFC 7296 3.15.1) - the exchange role of a CP payload. */
enum class IkeCfgType : uint8_t
{
    IKE_CFG_REQUEST = 1, ///< a request for configuration (e.g. an internal address)
    IKE_CFG_REPLY = 2,   ///< the reply granting it
    IKE_CFG_SET = 3,     ///< an unsolicited push of configuration
    IKE_CFG_ACK = 4,     ///< acknowledgement of a SET
};

// Common Configuration Attribute types (RFC 7296 3.15.1).
#define PC_IKE_CFG_INTERNAL_IP4_ADDRESS 1
#define PC_IKE_CFG_INTERNAL_IP4_NETMASK 2
#define PC_IKE_CFG_INTERNAL_IP4_DNS 3
#define PC_IKE_CFG_INTERNAL_IP4_NBNS 4
#define PC_IKE_CFG_INTERNAL_IP4_DHCP 6
#define PC_IKE_CFG_APPLICATION_VERSION 7
#define PC_IKE_CFG_INTERNAL_IP6_ADDRESS 8
#define PC_IKE_CFG_INTERNAL_IP6_DNS 10
#define PC_IKE_CFG_INTERNAL_IP6_DHCP 12
#define PC_IKE_CFG_INTERNAL_IP4_SUBNET 13
#define PC_IKE_CFG_INTERNAL_IP6_SUBNET 15

/** @brief One Configuration Attribute (RFC 7296 3.15.1): a 15-bit type and its value. */
struct IkeCfgAttr
{
    uint16_t type;        ///< attribute type (the reserved high bit is masked off)
    const uint8_t *value; ///< value bytes, or nullptr when @ref value_len is 0 (e.g. an empty request)
    uint16_t value_len;
};

/** @brief Iterates the attributes within a Configuration payload's attribute area. */
struct IkeCfgAttrIter
{
    const uint8_t *area;
    size_t len;
    size_t off;
};

// ── IKE header ──────────────────────────────────────────────────────────────────────────────────

/**
 * @brief Build the 28-byte IKE header from @p h (writing @p h->length verbatim - use
 *        @ref pc_ike_set_length to patch it once the payloads are appended).
 * @return PC_IKE_HDR_LEN (28) on success, or 0 on overflow / bad input.
 */
size_t pc_ike_hdr_build(uint8_t *buf, size_t cap, const IkeHeader *h);

/**
 * @brief Parse the 28-byte IKE header into @p out.
 * @return true if at least 28 bytes are present; false otherwise. (The version byte is surfaced in
 *         @p out->version but not rejected, so a caller can decide how to handle a mismatch.)
 */
bool pc_ike_hdr_parse(const uint8_t *buf, size_t len, IkeHeader *out);

/**
 * @brief Patch the 4-byte Length field of an already-built header (bytes 24..27) to @p total_len.
 * @return true if @p buf holds at least a header.
 */
bool pc_ike_set_length(uint8_t *buf, size_t buf_cap, uint32_t total_len);

// ── generic payload chain ─────────────────────────────────────────────────────────────────────

/**
 * @brief Start walking the payload chain: @p first_type is the header's Next Payload and @p area /
 *        @p area_len is the message body after the header (message + PC_IKE_HDR_LEN).
 */
void pc_ike_payload_iter_init(IkePayloadIter *it, IkePayloadType first_type, const uint8_t *area, size_t area_len);

/**
 * @brief Read the next payload's generic header + body slice into @p out and advance.
 * @return true if a well-formed payload was produced; false at end of chain or on a malformed length
 *         (payload length < 4 or running past the area).
 */
bool pc_ike_payload_next(IkePayloadIter *it, IkePayload *out);

/**
 * @brief Build a raw payload: the 4-byte generic header (@p next_payload + optional @p critical +
 *        length) followed by @p body. Most callers use the typed builders below instead.
 * @return total bytes written (4 + body_len), or 0 on overflow.
 */
size_t pc_ike_payload_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, bool critical, const uint8_t *body,
                            size_t body_len);

// ── typed payload builders (each writes a full payload incl. the generic header) ──────────────

/** @brief Build an SA payload carrying ONE proposal with @p num_transforms transforms. */
size_t pc_ike_sa_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint8_t proposal_num,
                       IkeProtocol protocol_id, const uint8_t *spi, uint8_t spi_size, const IkeTransform *transforms,
                       uint8_t num_transforms);

/** @brief Build a KE payload: DH group + key-exchange data. */
size_t pc_ike_ke_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint16_t dh_group, const uint8_t *data,
                       size_t data_len);

/** @brief Build a Nonce payload (Ni / Nr): the raw nonce bytes. */
size_t pc_ike_nonce_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *nonce,
                          size_t nonce_len);

/** @brief Build an Identification payload (IDi or IDr - the type is set by the previous Next Payload). */
size_t pc_ike_id_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeIdType id_type, const uint8_t *data,
                       size_t data_len);

/** @brief Build an AUTH payload: auth method + authentication data. */
size_t pc_ike_auth_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeAuthMethod auth_method,
                         const uint8_t *data, size_t data_len);

/** @brief Build a CERT or CERTREQ payload (same layout): cert encoding + data. The payload type is set
 *  by the previous Next Payload; this only writes the body. */
size_t pc_ike_cert_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint8_t cert_encoding,
                         const uint8_t *data, size_t data_len);

/** @brief Build a Notify payload: protocol + optional SPI + 16-bit type + notification data. */
size_t pc_ike_notify_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeProtocol protocol_id,
                           const uint8_t *spi, uint8_t spi_size, uint16_t notify_type, const uint8_t *data,
                           size_t data_len);

/** @brief Build a Delete payload: protocol + a list of @p num_spis SPIs each @p spi_size bytes. */
size_t pc_ike_delete_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeProtocol protocol_id,
                           uint8_t spi_size, const uint8_t *spis, uint16_t num_spis);

/** @brief Build a Traffic Selector payload (TSi or TSr) from @p num selectors. */
size_t pc_ike_ts_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const IkeTrafficSelector *sels,
                       uint8_t num);

/**
 * @brief Build a Configuration payload (CP): a CFG Type then @p num_attrs attributes (RFC 7296 3.15).
 *
 * Each attribute is written as its 15-bit type (the reserved high bit clear), a 2-byte value length, and
 * the value; a zero-length attribute (an empty request, e.g. INTERNAL_IP4_ADDRESS in a CFG_REQUEST) is
 * fine. @return the payload length written, or 0 on overflow / a bad argument.
 */
size_t pc_ike_cp_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeCfgType cfg_type,
                       const IkeCfgAttr *attrs, uint8_t num_attrs);

/**
 * @brief Frame an SK (encrypted) payload envelope: the generic header then @p iv, @p ciphertext, and
 *        @p icv laid end to end. The AEAD that produces @p ciphertext + @p icv is the caller's (a later
 *        tier) - this only lays out the bytes.
 * @return total bytes written, or 0 on overflow.
 */
size_t pc_ike_sk_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *iv, size_t iv_len,
                       const uint8_t *ciphertext, size_t ct_len, const uint8_t *icv, size_t icv_len);

// ── message fragmentation (RFC 7383) ──────────────────────────────────────────────────────────
//
// A large encrypted message is split into Encrypted Fragment (SKF) payloads. Support is negotiated in
// IKE_SA_INIT with the IKEV2_FRAGMENTATION_SUPPORTED notify; each fragment is its own AEAD (IV +
// ciphertext + ICV) carrying a slice of the original inner-payload plaintext, numbered 1..Total.

/** @brief IKEV2_FRAGMENTATION_SUPPORTED notify message type (RFC 7383 §2), zero-length data. */
#define PC_IKE_N_FRAGMENTATION_SUPPORTED 16430
/** @brief Largest Total Fragments this reassembler tracks. */
#define PC_IKE_FRAG_MAX 32

/**
 * @brief Frame an SKF (Encrypted Fragment) payload envelope (RFC 7383 §2.5): the generic header, the
 *        Fragment Number and Total Fragments (each 16-bit), then @p iv, @p ciphertext, and @p icv.
 *
 * Per §2.5 the generic header's Next Payload is the first inner payload type on fragment 1 and 0 on the
 * rest; the caller passes it as @p next_payload. The AEAD is the caller's - this only lays out the bytes.
 * @return total bytes written, or 0 on overflow / a bad argument (including @p frag_num 0 or > @p total).
 */
size_t pc_ike_skf_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint16_t frag_num, uint16_t total,
                        const uint8_t *iv, size_t iv_len, const uint8_t *ciphertext, size_t ct_len, const uint8_t *icv,
                        size_t icv_len);

/**
 * @brief Split an SKF payload body into its fragment counters and the IV / ciphertext / ICV slices.
 * @param iv_len / @p icv_len  the negotiated transform's sizes, needed to carve the fixed IV and ICV out.
 * @return false on a truncated body or a nonsensical fragment number (0, or > Total).
 */
bool pc_ike_skf_parse(const uint8_t *body, size_t body_len, uint16_t *frag_num, uint16_t *total, size_t iv_len,
                      size_t icv_len, const uint8_t **iv, const uint8_t **ct, size_t *ct_len, const uint8_t **icv);

/**
 * @brief Reassembly state for one fragmented message (RFC 7383 §2.5.3). Decrypted plaintext chunks are
 *        staged, in arrival order, into a caller-provided @c pool and concatenated 1..Total on completion.
 */
struct IkeFragReasm
{
    uint16_t total;                ///< Total Fragments (0 until the first fragment sets it)
    uint16_t count;                ///< distinct fragments stored so far
    bool present[PC_IKE_FRAG_MAX]; ///< present[i] = fragment (i+1) stored
    size_t off[PC_IKE_FRAG_MAX];   ///< pool offset of fragment (i+1)'s chunk
    size_t len[PC_IKE_FRAG_MAX];   ///< fragment (i+1)'s chunk length
    uint8_t *pool;                 ///< caller-provided staging buffer
    size_t pool_cap;
    size_t pool_used;
};

/** @brief Begin reassembly with a caller-owned staging buffer (must outlive the reassembler). */
void pc_ike_frag_reasm_init(IkeFragReasm *r, uint8_t *pool, size_t pool_cap);

/**
 * @brief Stage one decrypted fragment plaintext chunk (RFC 7383 §2.5.3).
 * @return false on a bad fragment number (0 / > Total / > @ref PC_IKE_FRAG_MAX), a Total that disagrees
 *         with an earlier fragment, a duplicate, or a pool overflow. A duplicate is rejected, not merged.
 */
bool pc_ike_frag_reasm_add(IkeFragReasm *r, uint16_t frag_num, uint16_t total, const uint8_t *chunk, size_t len);

/** @brief True once every one of Total fragments has been staged. */
bool pc_ike_frag_reasm_complete(const IkeFragReasm *r);

/**
 * @brief Concatenate the staged fragments in order into @p out (the reassembled inner-payload plaintext).
 * @return the assembled length, or 0 if reassembly is incomplete or @p out_cap is too small.
 */
size_t pc_ike_frag_reasm_assemble(const IkeFragReasm *r, uint8_t *out, size_t out_cap);

// ── anti-DoS COOKIE (RFC 7296 §2.6) ───────────────────────────────────────────────────────────
//
// Under an IKE_SA_INIT flood a responder can stay stateless: it replies with only a COOKIE notify and
// creates no SA. A legitimate initiator retries with the cookie as its first payload, proving it can
// receive at its claimed address before the responder spends any state or crypto. The cookie is a keyed
// hash of the initiator's first-message material, so the responder recomputes and checks it with no
// stored per-initiator state - only a small secret (versioned, rotated periodically).

/** @brief COOKIE notify message type (RFC 7296 §3.10.1). */
#define PC_IKE_N_COOKIE 16390
/** @brief COOKIE length: a 1-byte secret-version tag + a SHA-256 digest. */
#define PC_IKE_COOKIE_LEN 33

/**
 * @brief Compute an anti-DoS cookie: @p version | SHA-256( @p ni | @p ipi | @p spii | @p secret ).
 *
 * This is the RFC 7296 §2.6 stateless-cookie construction. @p ni is the initiator's nonce data, @p ipi
 * its source IP octets, @p spii its 8-byte SPI, and @p secret the responder's current rotating secret;
 * @p version tags which secret was used so a rotated secret can still verify in-flight cookies.
 * @return @ref PC_IKE_COOKIE_LEN on success, or 0 on a null argument / @p out_cap too small.
 */
size_t pc_ike_cookie_compute(uint8_t version, const uint8_t *secret, size_t secret_len, const uint8_t *ni,
                             size_t ni_len, const uint8_t *ipi, size_t ipi_len, const uint8_t spii[PC_IKE_SPI_LEN],
                             uint8_t *out, size_t out_cap);

/**
 * @brief Verify a received cookie against a recomputation (RFC 7296 §2.6), in constant time.
 *
 * The version tag is taken from @p cookie itself, so the caller supplies the @p secret matching that
 * version. @return true iff @p cookie is exactly @ref PC_IKE_COOKIE_LEN bytes and matches.
 */
bool pc_ike_cookie_verify(const uint8_t *cookie, size_t cookie_len, const uint8_t *secret, size_t secret_len,
                          const uint8_t *ni, size_t ni_len, const uint8_t *ipi, size_t ipi_len,
                          const uint8_t spii[PC_IKE_SPI_LEN]);

/** @brief Build a COOKIE Notify payload carrying @p cookie (RFC 7296 §2.6). */
size_t pc_ike_cookie_notify_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *cookie,
                                  size_t cookie_len);

// ── typed payload parsers (each takes a payload BODY from pc_ike_payload_next) ────────────────

/** @brief Decode a KE body into DH group + key data. */
bool pc_ike_ke_parse(const uint8_t *body, size_t body_len, uint16_t *dh_group, const uint8_t **data, size_t *data_len);

/** @brief Decode an Identification body into id type + data. */
bool pc_ike_id_parse(const uint8_t *body, size_t body_len, IkeIdType *id_type, const uint8_t **data, size_t *data_len);

/** @brief Decode an AUTH body into method + data. */
bool pc_ike_auth_parse(const uint8_t *body, size_t body_len, IkeAuthMethod *auth_method, const uint8_t **data,
                       size_t *data_len);

/** @brief Decode a Notify body into protocol, type, optional SPI, and notification data. */
bool pc_ike_notify_parse(const uint8_t *body, size_t body_len, IkeProtocol *protocol_id, uint16_t *notify_type,
                         const uint8_t **spi, uint8_t *spi_size, const uint8_t **data, size_t *data_len);

/** @brief Decode a Delete body into protocol, SPI size, count, and the SPI list. */
bool pc_ike_delete_parse(const uint8_t *body, size_t body_len, IkeProtocol *protocol_id, uint8_t *spi_size,
                         uint16_t *num_spis, const uint8_t **spis);

/**
 * @brief Slice an SK body into IV / ciphertext / ICV given the negotiated @p iv_len and @p icv_len.
 * @return true if the body is at least @p iv_len + @p icv_len bytes.
 */
bool pc_ike_sk_parse(const uint8_t *body, size_t body_len, size_t iv_len, size_t icv_len, const uint8_t **iv,
                     const uint8_t **ciphertext, size_t *ct_len, const uint8_t **icv);

// ── SA / proposal / transform parsing ─────────────────────────────────────────────────────────

/** @brief Read the first proposal out of an SA payload body. */
bool pc_ike_sa_first_proposal(const uint8_t *body, size_t body_len, IkeProposalRef *out);

/** @brief Start iterating the transforms of a parsed proposal. */
void pc_ike_transform_iter_init(IkeTransformIter *it, const IkeProposalRef *p);

/** @brief Read the next transform (decoding the key-length attribute if present). */
bool pc_ike_transform_next(IkeTransformIter *it, IkeTransformRef *out);

// ── traffic selector parsing ──────────────────────────────────────────────────────────────────

/** @brief The number of traffic selectors in a TS payload body (0 on malformed). */
uint8_t pc_ike_ts_count(const uint8_t *body, size_t body_len);

/** @brief Decode selector @p index (0-based) from a TS payload body. */
bool pc_ike_ts_get(const uint8_t *body, size_t body_len, uint8_t index, IkeTrafficSelector *out);

/**
 * @brief Decode a Configuration payload body into its CFG Type and its attribute area (RFC 7296 3.15).
 * @param attrs / @p attrs_len receive a pointer into @p body and the length of the attribute region;
 *        walk them with @ref pc_ike_cp_attr_iter_init + @ref pc_ike_cp_attr_next.
 * @return false on a truncated body.
 */
bool pc_ike_cp_parse(const uint8_t *body, size_t body_len, IkeCfgType *cfg_type, const uint8_t **attrs,
                     size_t *attrs_len);

/** @brief Begin iterating the attributes returned by @ref pc_ike_cp_parse. */
void pc_ike_cp_attr_iter_init(IkeCfgAttrIter *it, const uint8_t *attrs, size_t attrs_len);

/** @brief Decode the next Configuration Attribute; false at the end or on a truncated attribute. */
bool pc_ike_cp_attr_next(IkeCfgAttrIter *it, IkeCfgAttr *out);

// ── tier 2: SKEYSEED / SK_* key derivation (RFC 7296 §2.13-2.14) ───────────────────────────────
//
// The PRF is HMAC-SHA2-256 (transform id IKE_PRF_HMAC_SHA2_256), whose output/key length is 32 bytes.
// prf+ (§2.13) expands (key, seed) into arbitrary keying material; pc_ike_derive_keys runs the §2.14
// SKEYSEED + SK_* chain for an initial IKE SA. The AEAD that USES these keys is a later tier.

/** @brief PRF (HMAC-SHA2-256) output / key length, in bytes. */
#define PC_IKE_PRF_LEN 32
/** @brief Largest single SK_* key this build stores (an AES-256 key + a 4-byte AEAD salt, with margin). */
#define PC_IKE_SK_MAX 40
/** @brief Largest IKEv2 nonce (RFC 7296 §2.10: 16..256 octets). */
#define PC_IKE_NONCE_MAX 256

/** @brief Per-key lengths for the SK_* chain (algorithm-dependent; the caller sets them from the
 *  negotiated transforms). Each is in bytes. */
struct IkeKeyLengths
{
    size_t sk_d; ///< SK_d length = the PRF key length (32 for HMAC-SHA2-256).
    size_t sk_a; ///< SK_ai / SK_ar length = the integrity key length (32 for HMAC-SHA2-256-128).
    size_t sk_e; ///< SK_ei / SK_er length = the encryption key (+ any AEAD salt: 32, or 36 for AES-GCM).
    size_t sk_p; ///< SK_pi / SK_pr length = the PRF key length (32).
};

/** @brief The seven derived keys (RFC 7296 §2.14). Each key's valid length is the matching field below. */
struct IkeKeyMaterial
{
    uint8_t sk_d[PC_IKE_SK_MAX];
    uint8_t sk_ai[PC_IKE_SK_MAX];
    uint8_t sk_ar[PC_IKE_SK_MAX];
    uint8_t sk_ei[PC_IKE_SK_MAX];
    uint8_t sk_er[PC_IKE_SK_MAX];
    uint8_t sk_pi[PC_IKE_SK_MAX];
    uint8_t sk_pr[PC_IKE_SK_MAX];
    size_t sk_d_len; ///< valid bytes in sk_d
    size_t sk_a_len; ///< valid bytes in sk_ai / sk_ar
    size_t sk_e_len; ///< valid bytes in sk_ei / sk_er
    size_t sk_p_len; ///< valid bytes in sk_pi / sk_pr
};

/**
 * @brief prf+ (RFC 7296 §2.13) with HMAC-SHA2-256 as the PRF: expand (@p key, @p seed) into @p out_len
 *        bytes as T1 | T2 | ... where Ti = prf(key, Ti-1 | seed | i), i a 1-byte counter from 0x01, and
 *        T0 is empty.
 * @return false on a null argument or if @p out_len would need more than 255 blocks (the §2.13 cap).
 */
bool pc_ike_prf_plus(const uint8_t *key, size_t key_len, const uint8_t *seed, size_t seed_len, uint8_t *out,
                     size_t out_len);

/**
 * @brief Derive SKEYSEED and the seven SK_* keys (RFC 7296 §2.14) for an initial IKE SA:
 *          SKEYSEED = prf(Ni | Nr, g^ir)
 *          {SK_d | SK_ai | SK_ar | SK_ei | SK_er | SK_pi | SK_pr} = prf+(SKEYSEED, Ni | Nr | SPIi | SPIr)
 *        The PRF is HMAC-SHA2-256; @p dh_secret is the raw shared Diffie-Hellman secret g^ir.
 * @param spi_i / spi_r  the 8-byte initiator / responder IKE SPIs.
 * @param lens           per-key lengths (each 1..PC_IKE_SK_MAX; each nonce 1..PC_IKE_NONCE_MAX).
 * @return false on a null argument or an out-of-range length.
 */
bool pc_ike_derive_keys(const uint8_t *dh_secret, size_t dh_len, const uint8_t *ni, size_t ni_len, const uint8_t *nr,
                        size_t nr_len, const uint8_t *spi_i, const uint8_t *spi_r, const IkeKeyLengths *lens,
                        IkeKeyMaterial *out);

// ── tier 2: SK-payload AEAD (AES-256-GCM-16, RFC 5282 in RFC 7296 §3.14) ────────────────────────
//
// The SK payload body is `IV | ciphertext | ICV`. For AES-GCM (RFC 5282) the 12-byte GCM nonce is a
// 4-byte salt (the tail of SK_ei / SK_er, kept out of the 32-byte AES key) concatenated with the 8-byte
// explicit IV the sender writes into the body; the AAD is the message from the IKE header through the SK
// payload's 4-byte generic header (everything not encrypted). The plaintext framing (inner payloads +
// padding + pad-length) is the caller's; these functions are the crypto core.

/** @brief AES-256 key length for AES-GCM-16 (SK_ei / SK_er, salt excluded). */
#define PC_IKE_AEAD_KEY_LEN 32
/** @brief AEAD salt length: the 4-byte tail of SK_ei / SK_er (RFC 5282). */
#define PC_IKE_GCM_SALT_LEN 4
/** @brief Explicit IV length carried in the SK body for AES-GCM (RFC 5282). */
#define PC_IKE_GCM_IV_LEN 8
/** @brief AEAD tag / ICV length (AES-GCM-16). */
#define PC_IKE_AEAD_ICV_LEN 16

/**
 * @brief Seal an SK payload with AES-256-GCM: authenticate @p aad and encrypt @p pt, writing @p pt_len
 *        ciphertext bytes then the 16-byte ICV into @p out (which must hold @p pt_len + PC_IKE_AEAD_ICV_LEN;
 *        @p out may alias @p pt). The GCM nonce is @p salt || @p iv (RFC 5282).
 * @param key  32-byte AES-256 key = SK_ei (initiator->responder) or SK_er (responder->initiator).
 * @return false only on a null argument.
 */
bool pc_ike_sk_aead_seal(const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                         const uint8_t iv[PC_IKE_GCM_IV_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *pt,
                         size_t pt_len, uint8_t *out);

/**
 * @brief Open an AES-256-GCM SK payload: verify @p tag over @p aad || @p ct in constant time and, only on
 *        success, decrypt @p ct into @p out (@p out may alias @p ct). The nonce is @p salt || @p iv.
 * @return true iff the tag is valid (a forged / tampered message returns false and writes no plaintext).
 */
bool pc_ike_sk_aead_open(const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                         const uint8_t iv[PC_IKE_GCM_IV_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *ct,
                         size_t ct_len, const uint8_t tag[PC_IKE_AEAD_ICV_LEN], uint8_t *out);

// ── tier 2: Diffie-Hellman shared secret (the KE payload's g^ir, RFC 7296 §2.7) ─────────────────
//
// Group 31 (curve25519, RFC 7748 X25519) is supported today; groups 19 (NIST P-256) and 14 (MODP-2048)
// are a later increment. The ephemeral private key is the caller's (32 random bytes for X25519); these
// derive our KE public value and the shared secret that feeds pc_ike_derive_keys.

/** @brief X25519 private / public / shared-secret length (bytes). */
#define PC_IKE_X25519_LEN 32

/**
 * @brief Compute our KE public value for a negotiated D-H @p group (group 31: X25519(@p our_priv, base)).
 * @return the public-value length written to @p out, or 0 on an unsupported group / bad length / small cap.
 */
size_t pc_ike_dh_public(uint16_t group, const uint8_t *our_priv, size_t priv_len, uint8_t *out, size_t out_cap);

/**
 * @brief Compute the D-H shared secret g^ir for a negotiated @p group (group 31: X25519(@p our_priv,
 *        @p peer_pub)). The result feeds SKEYSEED in pc_ike_derive_keys.
 * @return the shared-secret length written to @p out, or 0 on an unsupported group / bad length / small cap.
 */
size_t pc_ike_dh_compute(uint16_t group, const uint8_t *our_priv, size_t priv_len, const uint8_t *peer_pub,
                         size_t pub_len, uint8_t *out, size_t out_cap);

// ── tier 2: IKE_AUTH pre-shared-key authentication (RFC 7296 §2.15) ─────────────────────────────
//
// With a shared key the AUTH payload data is
//     AUTH = prf( prf(PSK, "Key Pad for IKEv2"), <SignedOctets> )
// where <SignedOctets> = RealMessage | Nonce(peer) | prf(SK_p, RestOfIDPayload):
//   * RealMessage  = the octets of this side's first message (the whole IKE_SA_INIT it sent, for the
//                    initiator; the IKE_SA_INIT it sent, for the responder),
//   * Nonce(peer)  = the *other* side's nonce data,
//   * SK_p         = SK_pi when the initiator signs, SK_pr when the responder signs,
//   * RestOfIDPayload = the ID payload body (the 4 bytes after its generic header: ID type + 3 reserved
//                    + the identification data), i.e. an IkePayload::body for an IDi/IDr payload.
// The PRF is HMAC-SHA2-256. This computes the 32-byte AUTH data; verifying a peer is the same value
// compared in constant time against the received AUTH payload.

/** @brief AUTH payload data length for prf HMAC-SHA2-256 (bytes). */
#define PC_IKE_AUTH_LEN 32
/** @brief The fixed RFC 7296 §2.15 pre-shared-key pad string (17 octets, no NUL). */
#define PC_IKE_PSK_PAD "Key Pad for IKEv2"

/**
 * @brief Compute the RFC 7296 §2.15 pre-shared-key AUTH payload data into @p out (PC_IKE_AUTH_LEN bytes).
 * @param psk / psk_len          the shared key.
 * @param real_msg / real_len    this side's first message octets (RealMessage).
 * @param peer_nonce / nonce_len the peer's nonce data.
 * @param sk_p / sk_p_len        SK_pi (initiator) or SK_pr (responder).
 * @param id_body / id_body_len  the signing side's ID payload body (RestOfIDPayload).
 * @return false on a null argument; true on success (@p out filled).
 */
bool pc_ike_auth_psk(const uint8_t *psk, size_t psk_len, const uint8_t *real_msg, size_t real_len,
                     const uint8_t *peer_nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                     const uint8_t *id_body, size_t id_body_len, uint8_t out[PC_IKE_AUTH_LEN]);

// ── tier 2: IKE_SA_INIT message assembly (RFC 7296 §1.2) ───────────────────────────────────────
//
// The IKE_SA_INIT exchange's message is HDR | SA | KE | Nonce. These compose the tier-1 payload codec
// into the whole message (correct Next Payload chain + header Length) and read it back, so the state
// machine works in messages, not loose payloads. One proposal per SA (the common client shape).

/** @brief The salient parsed contents of an IKE_SA_INIT message; slices point into the message buffer. */
struct IkeSaInitMsg
{
    uint8_t init_spi[PC_IKE_SPI_LEN];
    uint8_t resp_spi[PC_IKE_SPI_LEN];
    bool is_response;        ///< true if the RESPONSE flag was set
    IkeProposalRef proposal; ///< the first proposal of SAi1 / SAr1
    uint16_t dh_group;       ///< the KE payload's D-H group
    const uint8_t *ke_data;  ///< KE key-exchange data
    size_t ke_len;           ///< KE data length
    const uint8_t *nonce;    ///< Ni / Nr data
    size_t nonce_len;        ///< nonce length
};

/**
 * @brief Build a complete IKE_SA_INIT message: HDR | SA(one proposal) | KE | Nonce, with the payload
 *        chain's Next Payload fields and the header Length all set correctly.
 * @param is_response      false for a request (sets the INITIATOR flag), true for a response.
 * @param proposal_num     the SA proposal number (usually 1).
 * @param transforms       the proposal's transforms (ENCR / PRF / INTEG / DH ...).
 * @param dh_group         the KE payload's D-H group id.
 * @param ke_data          the KE key-exchange data (our public value).
 * @param nonce            the Ni / Nr nonce data.
 * @return total message length written, or 0 on overflow / a bad argument.
 */
size_t pc_ike_sa_init_build(uint8_t *buf, size_t cap, const uint8_t init_spi[PC_IKE_SPI_LEN],
                            const uint8_t resp_spi[PC_IKE_SPI_LEN], uint32_t msg_id, bool is_response,
                            uint8_t proposal_num, const IkeTransform *transforms, uint8_t num_transforms,
                            uint16_t dh_group, const uint8_t *ke_data, size_t ke_len, const uint8_t *nonce,
                            size_t nonce_len);

/**
 * @brief Parse an IKE_SA_INIT message into @p out (SPIs, first proposal, KE group + data, nonce).
 * @return true iff the header is IKE_SA_INIT and the SA, KE, and Nonce payloads are all present + valid.
 */
bool pc_ike_sa_init_parse(const uint8_t *msg, size_t len, IkeSaInitMsg *out);

// ── tier 2: IKE_AUTH encrypted-message assembly (RFC 7296 §3.14, RFC 5282) ─────────────────────
//
// The IKE_AUTH (and any post-IKE_SA_INIT) message is HDR | SK{ inner payloads }. The SK payload body is
// IV | ciphertext | ICV; the plaintext is the inner payload chain followed by RFC 7296 §3.14 padding +
// a 1-byte Pad Length (this build uses zero padding, so a single 0x00). The AEAD (AES-256-GCM-16) is
// keyed by SK_ei / SK_er with the RFC 5282 salt||IV nonce, and authenticates the AAD = the IKE header
// through the SK payload's 4-byte generic header (everything before the IV). The inner chain is the
// caller's (build IDi | AUTH | SAi2 | TSi | TSr with the tier-1 payload builders, correctly chained).

/** @brief Fixed overhead the SK envelope adds around @p inner_len bytes: SK generic hdr + IV + pad-len + ICV. */
#define PC_IKE_SK_OVERHEAD (PC_IKE_PAYLOAD_HDR_LEN + PC_IKE_GCM_IV_LEN + 1 + PC_IKE_AEAD_ICV_LEN)

/**
 * @brief Build a complete SK-encrypted message (HDR | SK{ @p inner }) with AES-256-GCM.
 * @param first_inner_type  the type of the first inner payload (the SK payload's Next Payload), e.g. IKE_PL_IDI.
 * @param inner / inner_len the pre-built, chained inner payloads (their own Next Payload fields set).
 * @param key / salt / iv   SK_ei/SK_er (32 B), the 4-byte salt, and the 8-byte explicit IV (written into the body).
 * @return total message length, or 0 on overflow / a bad argument.
 */
size_t pc_ike_auth_msg_build(uint8_t *buf, size_t cap, const uint8_t init_spi[PC_IKE_SPI_LEN],
                             const uint8_t resp_spi[PC_IKE_SPI_LEN], uint32_t msg_id, bool is_response,
                             IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                             const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                             const uint8_t iv[PC_IKE_GCM_IV_LEN]);

/**
 * @brief Verify + decrypt an SK-encrypted message in place, exposing the inner payload chain.
 *
 * Parses HDR | SK, verifies the ICV over the AAD in constant time, decrypts the ciphertext in place, and
 * strips the RFC 7296 §3.14 padding + Pad Length. On success @p inner_out points at the decrypted inner
 * chain inside @p msg and @p first_inner_type is its first payload's type.
 * @return true iff the header is SK-framed and the tag verifies (a forged message returns false).
 */
bool pc_ike_auth_msg_open(uint8_t *msg, size_t len, const uint8_t key[PC_IKE_AEAD_KEY_LEN],
                          const uint8_t salt[PC_IKE_GCM_SALT_LEN], IkePayloadType *first_inner_type,
                          const uint8_t **inner_out, size_t *inner_len_out);

// ── tier 2: IKE_AUTH ECDSA-P256 (certificate) authentication (RFC 7296 §2.15, RFC 7427) ─────────
//
// A digital-signature AUTH signs the SAME octets the PSK MAC covers - RealMessage | Nonce |
// prf(SK_p, RestOfIDPayload) - with the identity's private key (here NIST P-256 / ECDSA-SHA256), and a
// peer is authenticated by verifying that signature against the public key from its CERT. The octets are
// assembled into a caller-provided scratch buffer (zero-heap) since the signer hashes them whole.

/** @brief P-256 uncompressed public point length (0x04 || X || Y). */
#define PC_IKE_ECDSA_P256_PUB_LEN 65
/** @brief P-256 private scalar length. */
#define PC_IKE_ECDSA_P256_PRIV_LEN 32
/** @brief Raw ECDSA-P256 signature length (r || s). */
#define PC_IKE_ECDSA_P256_SIG_LEN 64

/**
 * @brief Assemble the RFC 7296 §2.15 signed octets = RealMessage | Nonce | prf(SK_p, id_body) into
 *        @p scratch (needs @p real_len + @p nonce_len + 32 bytes).
 * @return the octet length written, or 0 on overflow / a null argument.
 */
size_t pc_ike_signed_octets(uint8_t *scratch, size_t cap, const uint8_t *real, size_t real_len, const uint8_t *nonce,
                            size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len, const uint8_t *id_body,
                            size_t id_body_len);

/**
 * @brief Produce an ECDSA-P256 (SHA-256) AUTH signature over the signed octets. @p scratch holds the
 *        assembled octets (see pc_ike_signed_octets). @return true on success (@p sig filled).
 */
bool pc_ike_auth_sign_ecdsa_p256(uint8_t sig[PC_IKE_ECDSA_P256_SIG_LEN], const uint8_t priv[PC_IKE_ECDSA_P256_PRIV_LEN],
                                 uint8_t *scratch, size_t scratch_cap, const uint8_t *real, size_t real_len,
                                 const uint8_t *nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                                 const uint8_t *id_body, size_t id_body_len);

/**
 * @brief Verify a peer's ECDSA-P256 (SHA-256) AUTH signature over the signed octets against its public
 *        point @p pub. @return true iff the signature is valid (a forged AUTH / wrong key returns false).
 */
bool pc_ike_auth_verify_ecdsa_p256(const uint8_t pub[PC_IKE_ECDSA_P256_PUB_LEN],
                                   const uint8_t sig[PC_IKE_ECDSA_P256_SIG_LEN], uint8_t *scratch, size_t scratch_cap,
                                   const uint8_t *real, size_t real_len, const uint8_t *nonce, size_t nonce_len,
                                   const uint8_t *sk_p, size_t sk_p_len, const uint8_t *id_body, size_t id_body_len);

// ── tier 2: IKE SA context + key material from a completed IKE_SA_INIT (RFC 7296 §2.14, §2.17) ──
//
// After IKE_SA_INIT both peers know the SPIs, the negotiated cipher suite, both nonces, and (via their
// own D-H private + the peer's KE) the shared secret - everything needed to derive the SK_* keys. These
// tie the tier-2 crypto (D-H + prf+ key schedule) together against a parsed exchange, so the state
// machine holds one `IkeSa` per session.

/** @brief The negotiated IKE cipher suite (the transforms chosen in the IKE_SA_INIT SA payload). */
struct IkeSuite
{
    uint16_t encr;       ///< encryption transform id (e.g. IKE_ENCR_AES_GCM_16).
    int32_t encr_keylen; ///< encryption key length in BITS (e.g. 256), or < 0 for a fixed-size cipher.
    uint16_t prf;        ///< PRF transform id (IKE_PRF_HMAC_SHA2_256).
    uint16_t integ;      ///< integrity transform id, or 0 for an AEAD cipher (no separate integrity key).
    uint16_t dh;         ///< D-H group id (IKE_DH_CURVE25519).
};

/** @brief One IKE SA's session state after IKE_SA_INIT: identity, negotiated suite, and derived keys. */
struct IkeSa
{
    uint8_t init_spi[PC_IKE_SPI_LEN];
    uint8_t resp_spi[PC_IKE_SPI_LEN];
    bool is_initiator;   ///< our role in this SA
    IkeSuite suite;      ///< the negotiated cipher suite
    IkeKeyMaterial keys; ///< the derived SK_d / ai / ar / ei / er / pi / pr (filled by keys_from_init)
};

/**
 * @brief Map a negotiated @p suite to the SK_* per-key lengths (RFC 7296 §2.14 + the cipher's key size).
 *
 * Supports the suites the library implements: PRF/INTEG HMAC-SHA2-256 (32-byte keys, sk_a = 0 for an
 * AEAD cipher), and AES-GCM-16 (encr key + a 4-byte salt) or a plain block cipher (encr key only).
 * @return false on an unsupported suite (a PRF other than HMAC-SHA2-256, or a bad key length).
 */
bool pc_ike_suite_keylengths(const IkeSuite *suite, IkeKeyLengths *out);

/**
 * @brief Derive @p sa->keys from a completed IKE_SA_INIT: compute g^ir = D-H(@p our_dh_priv, @p peer_ke)
 *        for the suite's group, then run the §2.14 SKEYSEED + SK_* schedule with @p sa's SPIs and the
 *        suite's key lengths. @p sa->init_spi / resp_spi / suite must already be set.
 * @return false on an unsupported suite / group or a bad length; the two peers derive identical keys.
 */
bool pc_ike_sa_keys_from_init(IkeSa *sa, const uint8_t *our_dh_priv, size_t our_dh_priv_len, const uint8_t *peer_ke,
                              size_t peer_ke_len, const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len);

/**
 * @brief Derive the NEW IKE SA keys when rekeying an IKE SA (RFC 7296 §2.18):
 *          SKEYSEED = prf(SK_d(old), g^ir(new) | Ni | Nr)
 *          {SK_*} = prf+(SKEYSEED, Ni | Nr | SPIi | SPIr)     [the new SPIs]
 *        Distinct from the initial schedule: the OLD SA's SK_d is the PRF key and g^ir prepends the seed.
 * @param sk_d_old  the current IKE SA's SK_d. @param dh_secret the new g^ir (X25519). @param spi_i / spi_r
 *        the new SA's SPIs. @return false on a null arg or an out-of-range length.
 */
bool pc_ike_rekey_derive_keys(const uint8_t *sk_d_old, size_t sk_d_old_len, const uint8_t *dh_secret, size_t dh_len,
                              const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len, const uint8_t *spi_i,
                              const uint8_t *spi_r, const IkeKeyLengths *lens, IkeKeyMaterial *out);

// ── tier 2: initiator IKE_SA_INIT handshake driver (RFC 7296 §1.2) ─────────────────────────────
//
// A small deterministic state machine for the initiator's first exchange: emit the IKE_SA_INIT request,
// then consume the responder's IKE_SA_INIT and derive the SA keys. The ephemeral material (SPI, the D-H
// key pair, the nonce) is the CALLER's (supplied from the platform RNG), so the core stays pure and
// host-testable; the context carries only what the next step needs. The IKE_AUTH exchange is the next
// increment.

/** @brief Handshake progress for the initiator's IKE_SA_INIT exchange. */
/** @brief Largest IKE_SA_INIT message the handshake stores as its RealMessage (for the AUTH octets). */
#define PC_IKE_MSG_MAX 640

enum class IkeState : uint8_t
{
    IKE_ST_INIT = 0,     ///< nothing sent yet
    IKE_ST_SA_INIT_SENT, ///< IKE_SA_INIT request emitted, awaiting the response
    IKE_ST_SA_INIT_DONE, ///< response consumed, SA keys derived (ready for IKE_AUTH)
    IKE_ST_AUTH_SENT,    ///< IKE_AUTH request emitted, awaiting the response
    IKE_ST_ESTABLISHED,  ///< responder's IKE_AUTH verified; the IKE SA is up
    IKE_ST_FAILED,       ///< a received message was rejected
};

/** @brief Initiator handshake context: the SA under construction plus what the next step needs. */
struct IkeHandshake
{
    IkeSa sa;                               ///< the SA being established (keys filled after SA_INIT)
    IkeState state;                         ///< @ref IkeState
    uint8_t our_dh_priv[PC_IKE_X25519_LEN]; ///< our ephemeral D-H private (to compute g^ir on the response)
    uint8_t our_nonce[PC_IKE_NONCE_MAX];    ///< Ni (needed for key derivation + later the AUTH octets)
    uint16_t our_nonce_len;
    uint8_t peer_nonce[PC_IKE_NONCE_MAX]; ///< Nr, captured from the response (the AUTH octets sign over it)
    uint16_t peer_nonce_len;
    uint8_t init_msg[PC_IKE_MSG_MAX]; ///< our IKE_SA_INIT bytes = RealMessage1 (signed by the AUTH)
    uint16_t init_msg_len;
    uint8_t resp_msg[PC_IKE_MSG_MAX]; ///< the responder's IKE_SA_INIT = RealMessage2 (verifies its AUTH)
    uint16_t resp_msg_len;
};

/**
 * @brief Begin the initiator handshake: fill @p hs and emit the IKE_SA_INIT request into @p out.
 *
 * The request is HDR | SA(one proposal from @p transforms) | KE(@p our_dh_pub) | Nonce(@p our_nonce),
 * with the responder SPI 0 and message id 0. @p our_dh_priv / @p our_dh_pub are the caller's ephemeral
 * X25519 key pair (group @p suite->dh, which must be curve25519 today); @p our_spi is a fresh 8-byte SPI.
 * @return the request length written to @p out, or 0 on a bad argument / overflow.
 */
size_t pc_ike_initiator_start(IkeHandshake *hs, const uint8_t our_spi[PC_IKE_SPI_LEN],
                              const uint8_t our_dh_priv[PC_IKE_X25519_LEN], const uint8_t our_dh_pub[PC_IKE_X25519_LEN],
                              const uint8_t *our_nonce, size_t nonce_len, const IkeSuite *suite,
                              const IkeTransform *transforms, uint8_t num_transforms, uint8_t *out, size_t out_cap);

/**
 * @brief Consume the responder's IKE_SA_INIT: validate it, capture the responder SPI + KE + nonce, and
 *        derive the SA keys (@p hs->sa.keys). Advances @p hs->state to IKE_ST_SA_INIT_DONE, or
 *        IKE_ST_FAILED on a mismatch.
 * @return true on success (keys derived), false if the message is malformed, echoes the wrong initiator
 *         SPI, or the key derivation fails.
 */
bool pc_ike_initiator_on_sa_init(IkeHandshake *hs, const uint8_t *resp, size_t resp_len);

/**
 * @brief Emit the initiator's IKE_AUTH request (PSK auth) into @p out: SK{ IDi | AUTH }.
 *
 * Requires @p hs in IKE_ST_SA_INIT_DONE. Builds the IDi payload from @p idi_type / @p idi_data, computes
 * AUTH = prf(prf(PSK, "Key Pad for IKEv2"), RealMessage1 | Nr | prf(SK_pi, IDi')) (RFC 7296 §2.15) over
 * the stored IKE_SA_INIT + the responder nonce, and wraps IDi | AUTH in the SK envelope keyed by SK_ei
 * (the salt is SK_ei's 4-byte tail) with the caller's 8-byte @p iv. Advances @p hs to IKE_ST_AUTH_SENT.
 * @return the message length, or 0 on a bad state / argument / overflow.
 */
size_t pc_ike_initiator_build_auth_psk(IkeHandshake *hs, IkeIdType idi_type, const uint8_t *idi_data, size_t idi_len,
                                       const uint8_t *psk, size_t psk_len, const uint8_t iv[PC_IKE_GCM_IV_LEN],
                                       uint8_t *out, size_t out_cap);

/**
 * @brief Consume the responder's IKE_AUTH (PSK): decrypt SK{ IDr | AUTH } with SK_er, then verify the
 *        responder's AUTH over ResponderSignedOctets = RealMessage2 | Ni | prf(SK_pr, IDr') in constant
 *        time. Requires @p hs in IKE_ST_AUTH_SENT; advances to IKE_ST_ESTABLISHED, or IKE_ST_FAILED on a
 *        decrypt / parse / verify failure.
 * @return true iff the responder authenticated (the IKE SA is now up).
 */
bool pc_ike_initiator_on_auth_psk(IkeHandshake *hs, const uint8_t *resp, size_t resp_len, const uint8_t *psk,
                                  size_t psk_len);

// ── tier 2: responder IKE_SA_INIT handshake driver (RFC 7296 §1.2) ─────────────────────────────
//
// The responder mirror of pc_ike_initiator_start / _on_sa_init: consume the initiator's IKE_SA_INIT,
// emit the IKE_SA_INIT response, and derive the SA keys - after which both peers hold identical SK_*.
// The IkeHandshake fields keep their role-neutral meaning: init_msg = RealMessage1 (the request),
// resp_msg = RealMessage2 (our response), our_nonce = our nonce, peer_nonce = the peer's.

/**
 * @brief Consume an initiator's IKE_SA_INIT and emit the response into @p out, deriving the SA keys.
 *
 * @p our_spi is the responder's fresh SPI; @p our_dh_priv / @p our_dh_pub the ephemeral X25519 key pair;
 * @p our_nonce the responder nonce (Nr); @p suite the accepted cipher suite (its group must match the
 * request's KE); @p transforms the echoed accepted proposal. Sets @p hs (role = responder) to
 * IKE_ST_SA_INIT_DONE.
 * @return the response message length written to @p out, or 0 on a malformed / mismatched request or overflow.
 */
size_t pc_ike_responder_on_sa_init(IkeHandshake *hs, const uint8_t *req, size_t req_len,
                                   const uint8_t our_spi[PC_IKE_SPI_LEN], const uint8_t our_dh_priv[PC_IKE_X25519_LEN],
                                   const uint8_t our_dh_pub[PC_IKE_X25519_LEN], const uint8_t *our_nonce,
                                   size_t nonce_len, const IkeSuite *suite, const IkeTransform *transforms,
                                   uint8_t num_transforms, uint8_t *out, size_t out_cap);

/**
 * @brief Consume the initiator's IKE_AUTH (PSK) and emit the responder's, reaching ESTABLISHED.
 *
 * Requires @p hs in IKE_ST_SA_INIT_DONE (responder role). Decrypts SK{ IDi | AUTH } with SK_ei, verifies
 * the initiator's AUTH over RealMessage1 | Nr | prf(SK_pi, IDi') in constant time, then builds this side's
 * IDr (from @p idr_type / @p idr_data) + AUTH over RealMessage2 | Ni | prf(SK_pr, IDr') and wraps IDr |
 * AUTH in SK{} keyed by SK_er with @p iv. Advances to IKE_ST_ESTABLISHED, or IKE_ST_FAILED on any miss.
 * @return the response message length, or 0 on a decrypt / verify / build failure.
 */
size_t pc_ike_responder_on_auth_psk(IkeHandshake *hs, const uint8_t *req, size_t req_len, const uint8_t *psk,
                                    size_t psk_len, IkeIdType idr_type, const uint8_t *idr_data, size_t idr_len,
                                    const uint8_t iv[PC_IKE_GCM_IV_LEN], uint8_t *out, size_t out_cap);

// ── tier 2: INFORMATIONAL exchange (RFC 7296 §1.4) over an established SA ───────────────────────
//
// Once an SA is IKE_ST_ESTABLISHED, either peer may run an INFORMATIONAL exchange: Dead-Peer Detection
// (an empty request, @p first_inner_type = IKE_PL_NONE, @p inner_len = 0), a Delete, or a Notify. The
// message is SK-encrypted keyed by our egress direction (SK_ei when we are the original initiator, else
// SK_er) with the flags carrying INITIATOR/RESPONSE independently.

/**
 * @brief Build an SK-encrypted INFORMATIONAL message we are SENDING over @p sa (an empty inner is DPD).
 * @return the message length, or 0 on a bad argument / overflow.
 */
size_t pc_ike_informational_build(const IkeSa *sa, bool is_response, uint32_t msg_id, IkePayloadType first_inner_type,
                                  const uint8_t *inner, size_t inner_len, const uint8_t iv[PC_IKE_GCM_IV_LEN],
                                  uint8_t *out, size_t out_cap);

/**
 * @brief Verify + decrypt a received INFORMATIONAL @p msg in place (keyed by the peer's egress direction),
 *        exposing the inner payload chain. @return true iff the ICV verifies.
 */
bool pc_ike_informational_open(const IkeSa *sa, uint8_t *msg, size_t len, IkePayloadType *first_inner_type,
                               const uint8_t **inner_out, size_t *inner_len_out);

// ── tier 2: CREATE_CHILD_SA exchange (RFC 7296 §1.3, §2.17) ─────────────────────────────────────
//
// Create a new Child SA (or rekey) over an established IKE SA. The message is SK-encrypted like an
// INFORMATIONAL; the inner SA | Ni | Nr | [KEi/KEr] | TSi | TSr chain is the caller's (tier-1 builders).
// The Child SA's ESP keys come from the §2.17 key schedule below.

/** @brief Build an SK-encrypted CREATE_CHILD_SA message we are SENDING (open it with pc_ike_informational_open). */
size_t pc_ike_create_child_sa_build(const IkeSa *sa, bool is_response, uint32_t msg_id, IkePayloadType first_inner_type,
                                    const uint8_t *inner, size_t inner_len, const uint8_t iv[PC_IKE_GCM_IV_LEN],
                                    uint8_t *out, size_t out_cap);

/**
 * @brief Derive Child SA keying material: KEYMAT = prf+(SK_d, [g^ir |] Ni | Nr) (RFC 7296 §2.17), written
 *        end to end into @p out (SK_ei | SK_ai | SK_er | SK_ar; the caller slices by the ESP transforms).
 * @param dh_secret NULL for no-PFS; the new g^ir (X25519) for a PFS rekey. @param out_len the total ESP
 *        key length needed.
 * @return false on a null arg, an out-of-range length, or more than 255 prf+ blocks.
 */
bool pc_ike_child_keymat(const uint8_t *sk_d, size_t sk_d_len, const uint8_t *dh_secret, size_t dh_len,
                         const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len, uint8_t *out,
                         size_t out_len);

/**
 * @brief Verify a peer's RSA-2048 (PKCS#1 v1.5, SHA-256) AUTH signature over the signed octets against
 *        its public key (@p n_be = 256-byte modulus, @p e_be4 = 4-byte exponent, from its CERT).
 *
 * The device signs with its own ECDSA-P256 key (pc_ike_auth_sign_ecdsa_p256); this covers the common
 * case of authenticating a PEER whose certificate is RSA. @p scratch holds the assembled signed octets.
 * @return true iff the signature is valid.
 */
bool pc_ike_auth_verify_rsa_sha256(const uint8_t *n_be, const uint8_t *e_be4, const uint8_t *sig, size_t sig_len,
                                   uint8_t *scratch, size_t scratch_cap, const uint8_t *real, size_t real_len,
                                   const uint8_t *nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                                   const uint8_t *id_body, size_t id_body_len);

#endif // PC_ENABLE_IKEV2

#endif // PROTOCORE_IKEV2_H
