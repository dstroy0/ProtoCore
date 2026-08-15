// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2.h
 * @brief IKEv2 (RFC 7296): the message and payload codec, the key schedule, and the handshake driver.
 *
 * RFC 7296 sec 3.1: a message begins with the 28-octet IKE header - IKE SA Initiator's SPI, IKE SA
 * Responder's SPI, Next Payload, MjVer/MnVer, Exchange Type, Flags, Message ID, Length - and every
 * multi-octet field is big endian. RFC 7296 sec 3.2: every payload begins with the generic payload
 * header - Next Payload, the Critical bit, RESERVED, Payload Length - so the chain is walked forward
 * from the header's Next Payload until a Next Payload of zero.
 *
 * The codec frames the Security Association payload (sec 3.3) with its Proposal (sec 3.3.1) and
 * Transform (sec 3.3.2) substructures and the Key Length attribute (sec 3.3.5), Key Exchange
 * (sec 3.4), Identification (sec 3.5), Certificate and Certificate Request (sec 3.6, 3.7),
 * Authentication (sec 3.8), Nonce (sec 3.9), Notify (sec 3.10), Delete (sec 3.11), Traffic Selector
 * (sec 3.13), Encrypted (sec 3.14), Configuration (sec 3.15), and the Encrypted Fragment payload of
 * RFC 7383 sec 2.5.
 *
 * On the codec sits the crypto: prf+ (sec 2.13), the SKEYSEED / SK_* schedule (sec 2.14), the IKE SA
 * rekey schedule (sec 2.18), Child SA KEYMAT (sec 2.17), the authenticated encryption that protects
 * the Encrypted payload (RFC 5282 sec 3, 4 and 5.1: AES-GCM with a 16-octet ICV, ENCR transform id 20
 * per RFC 5282 sec 7.2), the Curve25519 key exchange (Diffie-Hellman Group Num 31, RFC 8031 sec 3),
 * pre-shared key and digital-signature authentication (sec 2.15, RFC 7427 sec 3), and the stateless
 * COOKIE (sec 2.6).
 *
 * On the crypto sits the handshake driver: both roles run IKE_SA_INIT then IKE_AUTH (sec 1.2) to
 * IKE_ST_ESTABLISHED with mutual pre-shared key authentication, then INFORMATIONAL (sec 1.4) and
 * CREATE_CHILD_SA (sec 1.3) exchanges over the established SA.
 *
 * The PRF is HMAC-SHA2-256, PRF transform id 5 (RFC 4868 sec 4), whose preferred key length fixes
 * SK_d, SK_pi and SK_pr (sec 2.13); the integrity transform, when one is negotiated, is
 * AUTH_HMAC_SHA2_256_128, id 12 (RFC 4868 sec 4), keyed with the 32-octet hash output
 * (RFC 4868 sec 2.1.1). An AEAD cipher carries its own integrity, so SK_ai and SK_ar are then zero
 * octets (RFC 5282 sec 7.1).
 *
 * The module exports one symbol, @ref Ike. Everything in ikev2.c has internal linkage. A caller sets
 * the members a call takes, invokes it through ::Ike, and reads the outcome off the same handle.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IKEV2_H
#define PROTOCORE_IKEV2_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_IKEV2

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

/** @brief UDP port IKE runs on (RFC 7296 sec 2.23). */
#define PROTOCORE_IKEV2_PORT 500
/** @brief UDP port reserved for UDP-encapsulated ESP and IKE (RFC 7296 sec 2.23, RFC 3948 sec 2.2). */
#define PROTOCORE_IKEV2_NAT_PORT 4500
/** @brief IKE header size (RFC 7296 sec 3.1). */
#define PROTOCORE_IKE_HDR_LEN 28
/** @brief IKE SA Initiator's / Responder's SPI size (RFC 7296 sec 3.1). */
#define PROTOCORE_IKE_SPI_LEN 8
/** @brief Generic payload header size: Next Payload, C bit + RESERVED, Payload Length (RFC 7296 sec 3.2). */
#define PROTOCORE_IKE_PAYLOAD_HDR_LEN 4
/** @brief MjVer 2, MnVer 0 in the version octet (RFC 7296 sec 3.1). */
#define PROTOCORE_IKE_VERSION 0x20
/** @brief The Critical bit in a payload's second header octet (RFC 7296 sec 3.2). */
#define PROTOCORE_IKE_CRITICAL 0x80

/** @brief IKE header Flags octet, bit layout X|X|R|V|I|X|X|X (RFC 7296 sec 3.1). */
#define PROTOCORE_IKE_FLAG_INITIATOR 0x08 ///< I: sent by the original initiator of the IKE SA
#define PROTOCORE_IKE_FLAG_VERSION 0x10   ///< V: a higher major version is supported
#define PROTOCORE_IKE_FLAG_RESPONSE 0x20  ///< R: a response to the message with the same Message ID

/** @brief Transform attribute type Key Length, in bits, TV form (RFC 7296 sec 3.3.5). */
#define IKE_ATTR_KEY_LENGTH 14

/** @brief Transform IDs this build names; any 16-bit id is accepted on the wire. */
#define IKE_ENCR_AES_CBC 12            ///< ENCR_AES_CBC
#define IKE_ENCR_AES_GCM_16 20         ///< AES-GCM with a 16-octet ICV (RFC 5282 sec 7.2)
#define IKE_ENCR_CHACHA20_POLY1305 28  ///< ENCR_CHACHA20_POLY1305
#define IKE_PRF_HMAC_SHA2_256 5        ///< PRF_HMAC_SHA2_256 (RFC 4868 sec 4)
#define IKE_INTEG_HMAC_SHA2_256_128 12 ///< AUTH_HMAC_SHA2_256_128 (RFC 4868 sec 4)
#define IKE_DH_MODP2048 14             ///< 2048-bit MODP group (RFC 7296 app. B)
#define IKE_DH_ECP256 19               ///< 256-bit random ECP group
#define IKE_DH_CURVE25519 31           ///< Curve25519 (RFC 8031 sec 3)

// Configuration Attribute types (RFC 7296 sec 3.15.1).
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_ADDRESS 1
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_NETMASK 2
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_DNS 3
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_NBNS 4
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_DHCP 6
#define PROTOCORE_IKE_CFG_APPLICATION_VERSION 7
#define PROTOCORE_IKE_CFG_INTERNAL_IP6_ADDRESS 8
#define PROTOCORE_IKE_CFG_INTERNAL_IP6_DNS 10
#define PROTOCORE_IKE_CFG_INTERNAL_IP6_DHCP 12
#define PROTOCORE_IKE_CFG_INTERNAL_IP4_SUBNET 13
#define PROTOCORE_IKE_CFG_INTERNAL_IP6_SUBNET 15

/** @brief IKEV2_FRAGMENTATION_SUPPORTED Notify Message Type, no data (RFC 7383 sec 2.3, sec 6). */
#define PROTOCORE_IKE_N_FRAGMENTATION_SUPPORTED 16430
/** @brief Largest Total Fragments this reassembler tracks (RFC 7383 sec 2.5). */
#define PROTOCORE_IKE_FRAG_MAX 32

/** @brief COOKIE Notify Message Type (RFC 7296 sec 3.10.1). */
#define PROTOCORE_IKE_N_COOKIE 16390
/** @brief Cookie length here: the VersionIDofSecret octet plus a SHA-256 hash (RFC 7296 sec 2.6). */
#define PROTOCORE_IKE_COOKIE_LEN 33

/** @brief PRF_HMAC_SHA2_256 output and preferred key length, in octets (RFC 4868 sec 2.1.2). */
#define PROTOCORE_IKE_PRF_LEN 32
/** @brief Largest single SK_* key stored: a 32-octet cipher key plus a 4-octet salt, with margin. */
#define PROTOCORE_IKE_SK_MAX 40
/** @brief Largest nonce stored; a nonce is at least 128 bits (RFC 7296 sec 2.10). */
#define PROTOCORE_IKE_NONCE_MAX 256

/** @brief AES-256 cipher key length inside SK_ei / SK_er, salt excluded (RFC 5282 sec 7.1). */
#define PROTOCORE_IKE_AEAD_KEY_LEN 32
/** @brief Salt: the 4-octet tail of SK_ei / SK_er, not sent on the wire (RFC 5282 sec 4, sec 7.1). */
#define PROTOCORE_IKE_GCM_SALT_LEN 4
/** @brief Initialization Vector carried in the Encrypted payload (RFC 5282 sec 3.1). */
#define PROTOCORE_IKE_GCM_IV_LEN 8
/** @brief Integrity Check Value length: the full AES-GCM Authentication Tag (RFC 5282 sec 3.2). */
#define PROTOCORE_IKE_AEAD_ICV_LEN 16

/** @brief Octets the SK envelope adds around the inner payloads: generic header, IV, Pad Length, ICV. */
#define PROTOCORE_IKE_SK_OVERHEAD                                                                                      \
    (PROTOCORE_IKE_PAYLOAD_HDR_LEN + PROTOCORE_IKE_GCM_IV_LEN + 1 + PROTOCORE_IKE_AEAD_ICV_LEN)

/** @brief AUTH payload Authentication Data length for a PRF_HMAC_SHA2_256 MAC (RFC 7296 sec 2.15). */
#define PROTOCORE_IKE_AUTH_LEN 32
/** @brief The pre-shared key pad string: 17 ASCII characters, no null termination (RFC 7296 sec 2.15). */
#define PROTOCORE_IKE_PSK_PAD "Key Pad for IKEv2"

/** @brief Curve25519 private, public and shared-secret length (RFC 8031 sec 2, sec 3.1). */
#define PROTOCORE_IKE_X25519_LEN 32

/** @brief P-256 public point length, uncompressed (0x04 | X | Y). */
#define PROTOCORE_IKE_ECDSA_P256_PUB_LEN 65
/** @brief P-256 private scalar length. */
#define PROTOCORE_IKE_ECDSA_P256_PRIV_LEN 32
/** @brief ECDSA-P256 signature length, r | s. */
#define PROTOCORE_IKE_ECDSA_P256_SIG_LEN 64

/** @brief Largest message the handshake stores as RealMessage1 / RealMessage2 (RFC 7296 sec 2.15). */
#define PROTOCORE_IKE_MSG_MAX 640

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

/** @brief Exchange Type (RFC 7296 sec 3.1). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_SA_INIT = 34,
    IKE_AUTH = 35,
    IKE_CREATE_CHILD_SA = 36,
    IKE_INFORMATIONAL = 37,
} IkeExchange;

/** @brief Next Payload / payload types; zero ends the chain (RFC 7296 sec 3.2). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_PL_NONE = 0,
    IKE_PL_SA = 33,      ///< Security Association, SA
    IKE_PL_KE = 34,      ///< Key Exchange, KE
    IKE_PL_IDI = 35,     ///< Identification - Initiator, IDi
    IKE_PL_IDR = 36,     ///< Identification - Responder, IDr
    IKE_PL_CERT = 37,    ///< Certificate, CERT
    IKE_PL_CERTREQ = 38, ///< Certificate Request, CERTREQ
    IKE_PL_AUTH = 39,    ///< Authentication, AUTH
    IKE_PL_NONCE = 40,   ///< Nonce, Ni or Nr
    IKE_PL_NOTIFY = 41,  ///< Notify, N
    IKE_PL_DELETE = 42,  ///< Delete, D
    IKE_PL_VENDOR = 43,  ///< Vendor ID, V
    IKE_PL_TSI = 44,     ///< Traffic Selector - Initiator, TSi
    IKE_PL_TSR = 45,     ///< Traffic Selector - Responder, TSr
    IKE_PL_SK = 46,      ///< Encrypted and Authenticated, SK
    IKE_PL_CP = 47,      ///< Configuration, CP
    IKE_PL_EAP = 48,     ///< Extensible Authentication, EAP
    IKE_PL_SKF = 53,     ///< Encrypted and Authenticated Fragment, SKF (RFC 7383 sec 2.5)
} IkePayloadType;

/** @brief Transform Type (RFC 7296 sec 3.3.2). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_TRANSFORM_ENCR = 1,  ///< Encryption Algorithm
    IKE_TRANSFORM_PRF = 2,   ///< Pseudorandom Function
    IKE_TRANSFORM_INTEG = 3, ///< Integrity Algorithm
    IKE_TRANSFORM_DH = 4,    ///< Diffie-Hellman Group
    IKE_TRANSFORM_ESN = 5,   ///< Extended Sequence Numbers
} IkeTransformType;

/** @brief Protocol ID (RFC 7296 sec 3.3.1, sec 3.10, sec 3.11). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_PROTO_NONE = 0, ///< the notification concerns no existing SA and the SPI field is empty
    IKE_PROTO_IKE = 1,
    IKE_PROTO_AH = 2,
    IKE_PROTO_ESP = 3,
} IkeProtocol;

/** @brief ID Type (RFC 7296 sec 3.5). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_ID_RESERVED = 0, ///< reserved; the value an out member holds before a parse succeeds
    IKE_ID_IPV4_ADDR = 1,
    IKE_ID_FQDN = 2,
    IKE_ID_RFC822_ADDR = 3,
    IKE_ID_IPV6_ADDR = 5,
    IKE_ID_KEY_ID = 11,
} IkeIdType;

/** @brief Auth Method (RFC 7296 sec 3.8). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_AUTH_RESERVED = 0,    ///< reserved; the value an out member holds before a parse succeeds
    IKE_AUTH_RSA_SIG = 1,     ///< RSA Digital Signature
    IKE_AUTH_PSK = 2,         ///< Shared Key Message Integrity Code
    IKE_AUTH_DSS_SIG = 3,     ///< DSS Digital Signature
    IKE_AUTH_DIGITAL_SIG = 14 ///< Digital Signature (RFC 7427 sec 3)
} IkeAuthMethod;

/** @brief TS Type (RFC 7296 sec 3.13.1). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_TS_IPV4_ADDR_RANGE = 7,
    IKE_TS_IPV6_ADDR_RANGE = 8,
} IkeTsType;

/** @brief CFG Type (RFC 7296 sec 3.15.1). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_CFG_REQUEST = 1, ///< CFG_REQUEST
    IKE_CFG_REPLY = 2,   ///< CFG_REPLY
    IKE_CFG_SET = 3,     ///< CFG_SET
    IKE_CFG_ACK = 4,     ///< CFG_ACK
} IkeCfgType;

/** @brief Handshake progress across IKE_SA_INIT and IKE_AUTH (RFC 7296 sec 1.2). */
typedef enum PROTO_ENUM_PACKED
{
    IKE_ST_INIT = 0,     ///< nothing sent yet
    IKE_ST_SA_INIT_SENT, ///< IKE_SA_INIT emitted, awaiting the response
    IKE_ST_SA_INIT_DONE, ///< the peer's IKE_SA_INIT consumed and the SK_* keys derived
    IKE_ST_AUTH_SENT,    ///< IKE_AUTH request emitted, awaiting the response
    IKE_ST_ESTABLISHED,  ///< the peer's AUTH verified; the IKE SA is up
    IKE_ST_FAILED,       ///< a received message was rejected
} IkeState;

/** @brief The IKE header, decoded or to be encoded (RFC 7296 sec 3.1). */
typedef struct
{
    uint8_t init_spi[PROTOCORE_IKE_SPI_LEN]; ///< IKE SA Initiator's SPI
    uint8_t resp_spi[PROTOCORE_IKE_SPI_LEN]; ///< IKE SA Responder's SPI
    IkePayloadType next_payload;             ///< type of the first payload in the message
    uint8_t version;                         ///< MjVer | MnVer, 0x20 for IKEv2
    IkeExchange exchange;                    ///< Exchange Type
    uint8_t flags;                           ///< OR of PROTOCORE_IKE_FLAG_*
    uint32_t message_id;                     ///< Message ID
    uint32_t length;                         ///< Length of the whole message
} IkeHeader;

/** @brief One payload off the chain: its type, the following type, and its body (RFC 7296 sec 3.2). */
typedef struct
{
    IkePayloadType type;         ///< this payload's type, taken from the chain
    IkePayloadType next_payload; ///< Next Payload; IKE_PL_NONE ends the chain
    proto_bool critical;         ///< the Critical bit
    const uint8_t *body;         ///< the octets after the generic payload header
    size_t body_len;
} IkePayload;

/** @brief Forward walk of a message's payload chain (RFC 7296 sec 3.2). */
typedef struct
{
    const uint8_t *area;      ///< the payload area: the message plus PROTOCORE_IKE_HDR_LEN
    size_t len;               ///< octets in that area
    size_t off;               ///< current offset into @c area
    IkePayloadType next_type; ///< type of the payload at @c off; IKE_PL_NONE is done
} IkePayloadIter;

/** @brief One Transform Substructure to encode (RFC 7296 sec 3.3.2). */
typedef struct
{
    IkeTransformType type; ///< Transform Type
    uint16_t id;           ///< Transform ID
    int32_t key_length;    ///< Key Length attribute in bits (sec 3.3.5), or below zero for none
} IkeTransform;

/** @brief One decoded Transform Substructure (RFC 7296 sec 3.3.2). */
typedef struct
{
    IkeTransformType type; ///< Transform Type
    uint16_t id;           ///< Transform ID
    int32_t key_length;    ///< decoded Key Length attribute, or below zero when absent
    proto_bool last;       ///< Last Substruc was 0: no transform follows
} IkeTransformRef;

/** @brief One decoded Proposal Substructure (RFC 7296 sec 3.3.1). */
typedef struct
{
    uint8_t proposal_num;    ///< Proposal Num
    IkeProtocol protocol_id; ///< Protocol ID
    uint8_t spi_size;        ///< SPI Size
    uint8_t num_transforms;  ///< Num Transforms
    const uint8_t *spi;      ///< SPI, @c spi_size octets, or nullptr when SPI Size is zero
    const uint8_t *transforms;
    size_t transforms_len;
    proto_bool last; ///< Last Substruc was 0: no proposal follows
} IkeProposalRef;

/** @brief Walk of the Transform Substructures inside one proposal (RFC 7296 sec 3.3.2). */
typedef struct
{
    const uint8_t *area;
    size_t len;
    size_t off;
} IkeTransformIter;

/** @brief One Traffic Selector (RFC 7296 sec 3.13.1). */
typedef struct
{
    IkeTsType ts_type;         ///< TS Type
    uint8_t ip_protocol;       ///< IP Protocol ID; zero means any
    uint16_t start_port;       ///< Start Port
    uint16_t end_port;         ///< End Port
    const uint8_t *start_addr; ///< Starting Address, 4 or 16 octets
    const uint8_t *end_addr;   ///< Ending Address, the same length
    size_t addr_len;           ///< 4 or 16
} IkeTrafficSelector;

/** @brief One Configuration Attribute: a 15-bit type and its value (RFC 7296 sec 3.15.1). */
typedef struct
{
    uint16_t type;        ///< Attribute Type, the reserved high bit masked off
    const uint8_t *value; ///< Value, or nullptr when @c value_len is zero
    uint16_t value_len;   ///< Length
} IkeCfgAttr;

/** @brief Walk of a Configuration payload's attribute area (RFC 7296 sec 3.15.1). */
typedef struct
{
    const uint8_t *area;
    size_t len;
    size_t off;
} IkeCfgAttrIter;

/**
 * @brief Reassembly of one fragmented message (RFC 7383 sec 2.6).
 *
 * Decrypted Encrypted Fragment contents are staged into a caller-owned pool in arrival order and
 * merged 1..Total once every fragment is present.
 */
typedef struct
{
    uint16_t total;                             ///< Total Fragments; zero until the first fragment sets it
    uint16_t count;                             ///< distinct fragments stored
    proto_bool present[PROTOCORE_IKE_FRAG_MAX]; ///< present[i] holds fragment (i+1)
    size_t off[PROTOCORE_IKE_FRAG_MAX];         ///< pool offset of fragment (i+1)
    size_t len[PROTOCORE_IKE_FRAG_MAX];         ///< length of fragment (i+1)
    uint8_t *pool;                              ///< caller-owned staging buffer
    size_t pool_cap;
    size_t pool_used;
} IkeFragReasm;

/** @brief Per-key lengths of the SK_* chain, in octets (RFC 7296 sec 2.13, sec 2.14). */
typedef struct
{
    size_t sk_d; ///< SK_d: the PRF's preferred key length
    size_t sk_a; ///< SK_ai / SK_ar: the integrity key length, zero for an AEAD cipher
    size_t sk_e; ///< SK_ei / SK_er: the cipher key plus any AEAD salt
    size_t sk_p; ///< SK_pi / SK_pr: the PRF's preferred key length
} IkeKeyLengths;

/** @brief The seven keys taken in order from prf+ (RFC 7296 sec 2.14). */
typedef struct
{
    uint8_t sk_d[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_ai[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_ar[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_ei[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_er[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_pi[PROTOCORE_IKE_SK_MAX];
    uint8_t sk_pr[PROTOCORE_IKE_SK_MAX];
    size_t sk_d_len; ///< valid octets in sk_d
    size_t sk_a_len; ///< valid octets in sk_ai / sk_ar
    size_t sk_e_len; ///< valid octets in sk_ei / sk_er
    size_t sk_p_len; ///< valid octets in sk_pi / sk_pr
} IkeKeyMaterial;

/** @brief The four transforms negotiated for an IKE SA (RFC 7296 sec 2.13). */
typedef struct
{
    uint16_t encr;       ///< Encryption Algorithm transform id
    int32_t encr_keylen; ///< Key Length attribute in bits, or below zero for a fixed-length key
    uint16_t prf;        ///< Pseudorandom Function transform id
    uint16_t integ;      ///< Integrity Algorithm transform id, zero for an AEAD cipher
    uint16_t dh;         ///< Diffie-Hellman Group Num
} IkeSuite;

/** @brief One IKE SA after IKE_SA_INIT: the SPIs, the negotiated suite, and the SK_* keys. */
typedef struct
{
    uint8_t init_spi[PROTOCORE_IKE_SPI_LEN]; ///< IKE SA Initiator's SPI
    uint8_t resp_spi[PROTOCORE_IKE_SPI_LEN]; ///< IKE SA Responder's SPI
    proto_bool is_initiator;                 ///< this side is the original initiator
    IkeSuite suite;                          ///< the negotiated transforms
    IkeKeyMaterial keys;                     ///< SK_d, SK_ai, SK_ar, SK_ei, SK_er, SK_pi, SK_pr
    // The cookie hash, the prf+ chain, the AUTH MAC and the signature run in sequence, so one region
    // sized for the largest serves them all.
    uint8_t work[PROTOCORE_IKE_BORROW];
} IkeSa;

/** @brief The salient contents of a parsed IKE_SA_INIT; slices point into the message (sec 1.2). */
typedef struct
{
    uint8_t init_spi[PROTOCORE_IKE_SPI_LEN];
    uint8_t resp_spi[PROTOCORE_IKE_SPI_LEN];
    proto_bool is_response;  ///< the R flag was set
    IkeProposalRef proposal; ///< the first proposal of SAi1 or SAr1
    uint16_t dh_group;       ///< Diffie-Hellman Group Num from the KE payload
    const uint8_t *ke_data;  ///< Key Exchange Data
    size_t ke_len;
    const uint8_t *nonce; ///< Ni or Nr data
    size_t nonce_len;
} IkeSaInitMsg;

/** @brief Handshake context: the SA under construction and what the next step signs over. */
typedef struct
{
    IkeSa sa;                                      ///< the SA being established
    IkeState state;                                ///< @ref IkeState
    uint8_t our_dh_priv[PROTOCORE_IKE_X25519_LEN]; ///< our ephemeral private, to compute g^ir
    uint8_t our_nonce[PROTOCORE_IKE_NONCE_MAX];    ///< the nonce this side sent
    uint16_t our_nonce_len;
    uint8_t peer_nonce[PROTOCORE_IKE_NONCE_MAX]; ///< the nonce the peer sent
    uint16_t peer_nonce_len;
    uint8_t init_msg[PROTOCORE_IKE_MSG_MAX]; ///< RealMessage1: the IKE_SA_INIT request (sec 2.15)
    uint16_t init_msg_len;
    uint8_t resp_msg[PROTOCORE_IKE_MSG_MAX]; ///< RealMessage2: the IKE_SA_INIT response (sec 2.15)
    uint16_t resp_msg_len;
} IkeHandshake;

/** @brief A decoded Key Exchange payload (RFC 7296 sec 3.4). */
typedef struct
{
    uint16_t dh_group;      ///< Diffie-Hellman Group Num
    const uint8_t *ke_data; ///< Key Exchange Data
    size_t ke_len;
} IkeKeRef;

/** @brief A decoded Identification payload (RFC 7296 sec 3.5). */
typedef struct
{
    IkeIdType id_type;      ///< ID Type
    const uint8_t *id_data; ///< Identification Data
    size_t id_len;
} IkeIdRef;

/** @brief A decoded Authentication payload (RFC 7296 sec 3.8). */
typedef struct
{
    IkeAuthMethod auth_method; ///< Auth Method
    const uint8_t *auth_data;  ///< Authentication Data
    size_t auth_len;
} IkeAuthRef;

/** @brief A decoded Notify payload (RFC 7296 sec 3.10). */
typedef struct
{
    IkeProtocol protocol_id; ///< Protocol ID
    uint8_t spi_size;        ///< SPI Size
    uint16_t notify_type;    ///< Notify Message Type
    const uint8_t *spi;      ///< SPI, or nullptr when SPI Size is zero
    const uint8_t *data;     ///< Notification Data
    size_t data_len;
} IkeNotifyRef;

/** @brief A decoded Delete payload (RFC 7296 sec 3.11). */
typedef struct
{
    IkeProtocol protocol_id; ///< Protocol ID
    uint8_t spi_size;        ///< SPI Size
    uint16_t num_spis;       ///< Num of SPIs
    const uint8_t *spis;     ///< the SPI list, or nullptr when it is empty
} IkeDeleteRef;

/** @brief A sliced Encrypted or Encrypted Fragment payload body (RFC 7296 sec 3.14, RFC 7383 sec 2.5). */
typedef struct
{
    uint16_t frag_num; ///< Fragment Number, zero for an unfragmented Encrypted payload
    uint16_t total;    ///< Total Fragments, zero for an unfragmented Encrypted payload
    const uint8_t *iv; ///< Initialization Vector
    const uint8_t *ct; ///< Ciphertext
    size_t ct_len;
    const uint8_t *icv; ///< Integrity Checksum Data
} IkeSkRef;

/** @brief A decoded Configuration payload (RFC 7296 sec 3.15). */
typedef struct
{
    IkeCfgType cfg_type;  ///< CFG Type
    const uint8_t *attrs; ///< the Configuration Attribute area
    size_t attrs_len;
} IkeCpRef;

/** @brief The inner payload chain an Encrypted payload was carrying (RFC 7296 sec 3.14). */
typedef struct
{
    IkePayloadType first_inner_type; ///< the Encrypted payload's Next Payload
    const uint8_t *inner;            ///< the decrypted chain, inside the caller's message buffer
    size_t inner_len;
} IkeInnerRef;

/** @brief Where a build, a hash or a signature writes. */
typedef struct
{
    uint8_t *buf; ///< the octets a call writes
    size_t cap;   ///< room there; prf+ and child_keymat fill it exactly
} IkeOutArgs;

/** @brief The octets a parse reads: a whole message, or one payload body (RFC 7296 sec 3.2). */
typedef struct
{
    const uint8_t *msg; ///< a message, a payload body, or an attribute area
    size_t len;
} IkeWireArgs;

/** @brief The generic payload header a build writes, and the payload's own variable field (sec 3.2). */
typedef struct
{
    IkePayloadType next_payload; ///< Next Payload: the type of the payload that follows this one
    proto_bool critical;         ///< the Critical bit
    const uint8_t *data;         ///< this payload's variable field, named per payload by each call
    size_t data_len;
} IkePayloadArgs;

/** @brief The Proposal Substructure a build encodes, and the SPIs a Notify or Delete names (sec 3.3.1). */
typedef struct
{
    uint8_t proposal_num;           ///< Proposal Num
    IkeProtocol protocol_id;        ///< Protocol ID
    const uint8_t *spi;             ///< SPI
    uint8_t spi_size;               ///< SPI Size
    uint16_t num_spis;              ///< Num of SPIs in a Delete payload (sec 3.11)
    const IkeTransform *transforms; ///< the Transform Substructures to encode (sec 3.3.2)
    uint8_t num_transforms;         ///< Num Transforms
} IkeProposalArgs;

/** @brief The key exchange: the group and the values (RFC 7296 sec 3.4, RFC 8031 sec 3). */
typedef struct
{
    uint16_t dh_group;       ///< Diffie-Hellman Group Num
    const uint8_t *our_priv; ///< our ephemeral private value
    size_t our_priv_len;     ///< its length; 32 for group 31
    const uint8_t *our_pub;  ///< our Key Exchange Data
    size_t our_pub_len;      ///< its length; 32 for group 31
    const uint8_t *peer_pub; ///< the peer's Key Exchange Data
    size_t peer_pub_len;     ///< its length
} IkeKeArgs;

/** @brief The identity: the ID payload's fields and its signed remainder (sec 3.5, 3.6, 2.15). */
typedef struct
{
    IkeIdType id_type;      ///< ID Type
    const uint8_t *id_body; ///< RestOfInitIDPayload / RestOfRespIDPayload: the ID payload body
    size_t id_body_len;
    uint8_t cert_encoding; ///< Cert Encoding for a CERT or CERTREQ payload (sec 3.6)
} IkeIdArgs;

/** @brief Authentication: the method and every input the AUTH value is computed from (sec 2.15). */
typedef struct
{
    IkeAuthMethod auth_method; ///< Auth Method (sec 3.8)
    const uint8_t *psk;        ///< the Shared Secret
    size_t psk_len;
    const uint8_t *real_msg; ///< RealMessage1 when the initiator signs, RealMessage2 when the responder does
    size_t real_len;
    const uint8_t *peer_nonce; ///< NonceRData when the initiator signs, NonceIData when the responder does
    size_t peer_nonce_len;
    const uint8_t *sk_p; ///< SK_pi when the initiator signs, SK_pr when the responder does
    size_t sk_p_len;
    uint8_t *scratch;   ///< where the signed octets are assembled
    size_t scratch_cap; ///< room there: real_len + peer_nonce_len + PROTOCORE_IKE_AUTH_LEN
    const uint8_t *sig; ///< the signature a verify judges
    size_t sig_len;
    const uint8_t *priv;  ///< the P-256 private scalar a sign uses
    const uint8_t *pub;   ///< the peer's P-256 public point
    const uint8_t *rsa_n; ///< the peer's RSA modulus, 256 octets big endian
    const uint8_t *rsa_e; ///< the peer's RSA exponent, 4 octets big endian
} IkeAuthArgs;

/** @brief The Notify payload's own fields and the COOKIE it carries (sec 3.10, sec 2.6). */
typedef struct
{
    uint16_t notify_type;  ///< Notify Message Type
    uint8_t version;       ///< VersionIDofSecret tagging which secret a cookie was made with
    const uint8_t *secret; ///< the responder's current secret
    size_t secret_len;
    const uint8_t *ni; ///< Ni: the initiator's nonce data
    size_t ni_len;
    const uint8_t *ipi; ///< IPi: the initiator's source address octets
    size_t ipi_len;
    const uint8_t *spii;   ///< SPIi: the initiator's SPI
    const uint8_t *cookie; ///< the cookie a verify judges or a build carries
    size_t cookie_len;
} IkeNotifyArgs;

/** @brief The Traffic Selectors a build encodes and the one a get names (RFC 7296 sec 3.13). */
typedef struct
{
    const IkeTrafficSelector *sels; ///< the selectors to encode
    uint8_t num;                    ///< Number of TSs
    uint8_t index;                  ///< which selector a get decodes, zero based
} IkeTsArgs;

/** @brief The Configuration payload a build encodes (RFC 7296 sec 3.15). */
typedef struct
{
    IkeCfgType cfg_type;     ///< CFG Type
    const IkeCfgAttr *attrs; ///< the Configuration Attributes to encode
    uint8_t num_attrs;       ///< how many
} IkeCpArgs;

/** @brief The Encrypted payload and its AEAD inputs (RFC 7296 sec 3.14, RFC 5282 sec 3, 4, 5.1). */
typedef struct
{
    uint8_t *msg;        ///< an SK-framed message an open verifies and decrypts in place
    size_t msg_len;      ///< octets received in it
    const uint8_t *key;  ///< the cipher key: SK_ei or SK_er, salt excluded (RFC 5282 sec 7.1)
    const uint8_t *salt; ///< the implicit nonce half (RFC 5282 sec 4)
    const uint8_t *iv;   ///< Initialization Vector, the explicit nonce half
    size_t iv_len;       ///< its length as the negotiated transform defines it
    const uint8_t *ct;   ///< Ciphertext
    size_t ct_len;
    const uint8_t *icv; ///< Integrity Checksum Data
    size_t icv_len;     ///< its length as the negotiated transform defines it
    const uint8_t *aad; ///< associated data: the header through the Encrypted payload's own header
    size_t aad_len;
    const uint8_t *pt; ///< the plaintext a seal encrypts
    size_t pt_len;
} IkeSkArgs;

/** @brief The Encrypted Fragment counters and the chunk a reassembler stages (RFC 7383 sec 2.5, 2.6). */
typedef struct
{
    uint16_t frag_num;    ///< Fragment Number, from 1
    uint16_t total;       ///< Total Fragments
    IkeFragReasm *reasm;  ///< the reassembly a call acts on
    const uint8_t *chunk; ///< one fragment's decrypted content
    size_t chunk_len;
} IkeFragArgs;

/** @brief The key schedule's inputs and outputs (RFC 7296 sec 2.13, 2.14, 2.17, 2.18). */
typedef struct
{
    const IkeSuite *suite;  ///< the negotiated transforms a length mapping reads
    IkeKeyLengths *lens;    ///< in: the per-key lengths a derive uses; out: what suite_keylengths computed
    IkeKeyMaterial *keys;   ///< where a derive writes the seven SK_* keys
    const uint8_t *prf_key; ///< K for a bare prf+ call
    size_t prf_key_len;
    const uint8_t *seed; ///< S for a bare prf+ call
    size_t seed_len;
    const uint8_t *dh_secret; ///< g^ir; nullptr in a Child SA derivation without a new exchange
    size_t dh_len;
    const uint8_t *ni; ///< Ni, stripped of its payload header
    size_t ni_len;
    const uint8_t *nr; ///< Nr, stripped of its payload header
    size_t nr_len;
    const uint8_t *spi_i; ///< SPIi of the SA being keyed
    const uint8_t *spi_r; ///< SPIr of the SA being keyed
    const uint8_t *sk_d;  ///< SK_d: the old SA's for a rekey, this SA's for a Child SA
    size_t sk_d_len;
} IkeKeyArgs;

/** @brief What a whole-message build stamps into the header and wraps (sec 3.1, sec 3.14). */
typedef struct
{
    const uint8_t *init_spi;         ///< IKE SA Initiator's SPI
    const uint8_t *resp_spi;         ///< IKE SA Responder's SPI
    uint32_t message_id;             ///< Message ID
    proto_bool is_response;          ///< set the R flag rather than the I flag
    uint32_t length;                 ///< the Length a patch writes into an already-built header
    IkePayloadType first_inner_type; ///< the Encrypted payload's Next Payload
    const uint8_t *inner;            ///< the chained inner payloads to encrypt
    size_t inner_len;
} IkeMsgArgs;

/** @brief The walks a caller owns, so nested walks do not share one cursor (RFC 7296 sec 3.2). */
typedef struct
{
    IkePayloadIter *chain;          ///< the payload chain walk
    IkeTransformIter *transforms;   ///< the transform walk
    IkeCfgAttrIter *attrs;          ///< the Configuration Attribute walk
    const IkeProposalRef *proposal; ///< the proposal a transform walk starts on
    IkePayloadType first_type;      ///< the header's Next Payload, where a chain walk starts
} IkeWalkArgs;

/** @brief The session an SA-level or handshake call acts on (RFC 7296 sec 1.2). */
typedef struct
{
    IkeSa *sa;                ///< the SA a post-auth exchange is protected by
    IkeHandshake *hs;         ///< the handshake a driver step advances
    const uint8_t *our_spi;   ///< the SPI this side chose
    const uint8_t *our_nonce; ///< the nonce this side sends
    size_t our_nonce_len;
} IkeSessionArgs;

/** @brief The codec's calls, described only in ikev2.c. */
struct IkeInternal;

/**
 * @brief The IKEv2 handle (RFC 7296).
 *
 * A caller sets the members a call takes, invokes it through ::Ike, and reads the outcome off the
 * same handle. Slices returned by a parse point into the caller's own buffer.
 *
 * No storage member: every octet, SA, handshake and reassembly a call touches belongs to the caller,
 * so nothing survives a call.
 *
 * @var IkeNs::work     the caller's scratch region the hash, PRF and signature calls borrow
 * @var IkeNs::hdr      in: the header a build encodes; out: the header a parse decoded (sec 3.1)
 * @var IkeNs::out      where a build, a hash or a signature writes
 * @var IkeNs::wire     the octets a parse reads
 * @var IkeNs::pl       the generic payload header a build writes, and the payload's variable field
 * @var IkeNs::prop     the Proposal Substructure a build encodes, and the SPIs a Notify or Delete names
 * @var IkeNs::ke       the key exchange: the group and the public and private values
 * @var IkeNs::id       the identity: the ID payload's fields and its signed remainder
 * @var IkeNs::auth     the Auth Method and every input the AUTH value is computed from
 * @var IkeNs::notify   the Notify payload's own fields and the COOKIE it carries
 * @var IkeNs::ts       the Traffic Selectors a build encodes and the one a get names
 * @var IkeNs::cp       the Configuration payload a build encodes
 * @var IkeNs::sk       the Encrypted payload and its AEAD inputs
 * @var IkeNs::frag     the Encrypted Fragment counters and the chunk a reassembler stages
 * @var IkeNs::keymat   the key schedule's inputs and outputs
 * @var IkeNs::msg      what a whole-message build stamps into the header and wraps
 * @var IkeNs::walk     the caller-owned chain, transform and attribute walks
 * @var IkeNs::sess     the SA or handshake a call acts on
 *
 * @var IkeNs::ok          a call's true/false outcome
 * @var IkeNs::n           octets a build wrote or a call produced, zero on failure
 * @var IkeNs::u8          Number of TSs a count reports (sec 3.13)
 * @var IkeNs::payload     the payload a chain walk produced
 * @var IkeNs::proposal    the first Proposal Substructure of an SA payload
 * @var IkeNs::transform   the Transform Substructure a transform walk produced
 * @var IkeNs::sel         the Traffic Selector a get decoded
 * @var IkeNs::attr        the Configuration Attribute an attribute walk produced
 * @var IkeNs::ke_ref      the decoded Key Exchange payload
 * @var IkeNs::id_ref      the decoded Identification payload
 * @var IkeNs::auth_ref    the decoded Authentication payload
 * @var IkeNs::notify_ref  the decoded Notify payload
 * @var IkeNs::delete_ref  the decoded Delete payload
 * @var IkeNs::sk_ref      the sliced Encrypted or Encrypted Fragment body
 * @var IkeNs::cp_ref      the decoded Configuration payload
 * @var IkeNs::opened      the inner payload chain an open exposed
 * @var IkeNs::sa_init     the parsed IKE_SA_INIT message
 *
 * @var IkeNs::hdr_build   write the 28-octet IKE header from @c hdr, Length verbatim (sec 3.1)
 * @var IkeNs::hdr_parse   read the 28-octet IKE header into @c hdr (sec 3.1)
 * @var IkeNs::set_length  patch octets 24..27 of a built header to @c msg.length (sec 3.1)
 * @var IkeNs::payload_iter_init  start @c walk.chain at @c walk.first_type over @c wire (sec 3.2)
 * @var IkeNs::payload_next       read the next payload into @c payload and advance (sec 3.2)
 * @var IkeNs::payload_build      write a generic payload header and @c pl.data behind it (sec 3.2)
 * @var IkeNs::sa_build           write an SA payload carrying one proposal (sec 3.3, 3.3.1, 3.3.2)
 * @var IkeNs::ke_build           write a KE payload: @c ke.dh_group then @c pl.data (sec 3.4)
 * @var IkeNs::nonce_build        write a Nonce payload: @c pl.data is the Nonce Data (sec 3.9)
 * @var IkeNs::id_build           write an ID payload: @c id.id_type then @c pl.data (sec 3.5)
 * @var IkeNs::auth_build         write an AUTH payload: @c auth.auth_method then @c pl.data (sec 3.8)
 * @var IkeNs::cert_build         write a CERT or CERTREQ body: @c id.cert_encoding then @c pl.data (sec 3.6)
 * @var IkeNs::notify_build       write a Notify payload: @c prop SPI fields, type, @c pl.data (sec 3.10)
 * @var IkeNs::delete_build       write a Delete payload: @c prop fields then @c pl.data SPIs (sec 3.11)
 * @var IkeNs::ts_build           write a TSi or TSr payload from @c ts.sels (sec 3.13)
 * @var IkeNs::cp_build           write a CP payload: @c cp.cfg_type then its attributes (sec 3.15)
 * @var IkeNs::sk_build           lay out an SK payload: IV, Ciphertext, ICV (sec 3.14)
 * @var IkeNs::skf_build          lay out an SKF payload: the counters then IV, Ciphertext, ICV (RFC 7383 sec 2.5)
 * @var IkeNs::skf_parse          slice an SKF body into @c sk_ref (RFC 7383 sec 2.5)
 * @var IkeNs::frag_reasm_init    bind @c frag.reasm to the pool at @c out (RFC 7383 sec 2.6)
 * @var IkeNs::frag_reasm_add     stage one fragment's content (RFC 7383 sec 2.6)
 * @var IkeNs::frag_reasm_complete  every one of Total Fragments is staged (RFC 7383 sec 2.6)
 * @var IkeNs::frag_reasm_assemble  merge the staged fragments 1..Total into @c out (RFC 7383 sec 2.6)
 * @var IkeNs::cookie_compute     VersionIDofSecret | SHA-256(Ni | IPi | SPIi | secret) (sec 2.6)
 * @var IkeNs::cookie_verify      recompute that cookie and compare it in constant time (sec 2.6)
 * @var IkeNs::cookie_notify_build  write a COOKIE Notify carrying @c notify.cookie (sec 2.6)
 * @var IkeNs::ke_parse           decode a KE body into @c ke_ref (sec 3.4)
 * @var IkeNs::id_parse           decode an ID body into @c id_ref (sec 3.5)
 * @var IkeNs::auth_parse         decode an AUTH body into @c auth_ref (sec 3.8)
 * @var IkeNs::notify_parse       decode a Notify body into @c notify_ref (sec 3.10)
 * @var IkeNs::delete_parse       decode a Delete body into @c delete_ref (sec 3.11)
 * @var IkeNs::sk_parse           slice an SK body into @c sk_ref by @c sk.iv_len and @c sk.icv_len (sec 3.14)
 * @var IkeNs::sa_first_proposal  decode the first Proposal Substructure into @c proposal (sec 3.3.1)
 * @var IkeNs::transform_iter_init  start @c walk.transforms over @c walk.proposal (sec 3.3.2)
 * @var IkeNs::transform_next     read the next Transform Substructure into @c transform (sec 3.3.2)
 * @var IkeNs::ts_count           Number of TSs in a TS body, into @c u8 (sec 3.13)
 * @var IkeNs::ts_get             decode selector @c ts.index into @c sel (sec 3.13.1)
 * @var IkeNs::cp_parse           decode a CP body into @c cp_ref (sec 3.15)
 * @var IkeNs::cp_attr_iter_init  start @c walk.attrs over @c wire (sec 3.15.1)
 * @var IkeNs::cp_attr_next       read the next Configuration Attribute into @c attr (sec 3.15.1)
 * @var IkeNs::prf_plus           expand prf+(@c keymat.prf_key, @c keymat.seed) into @c out (sec 2.13)
 * @var IkeNs::derive_keys        SKEYSEED then the seven SK_* keys for a new IKE SA (sec 2.14)
 * @var IkeNs::rekey_derive_keys  the same split with SKEYSEED keyed by the old SK_d (sec 2.18)
 * @var IkeNs::child_keymat       KEYMAT = prf+(SK_d, [g^ir |] Ni | Nr) into @c out (sec 2.17)
 * @var IkeNs::suite_keylengths   map @c keymat.suite to the SK_* lengths in @c keymat.lens (sec 2.13)
 * @var IkeNs::sa_keys_from_init  compute g^ir then run the sec 2.14 schedule for @c sess.sa
 * @var IkeNs::sk_aead_seal       encrypt @c sk.pt under @c sk.key, writing Ciphertext then ICV (RFC 5282 sec 3.2)
 * @var IkeNs::sk_aead_open       verify the ICV, then decrypt into @c out (RFC 5282 sec 3.2, 5.1)
 * @var IkeNs::dh_public          our Key Exchange Data for @c ke.dh_group (RFC 8031 sec 3.1)
 * @var IkeNs::dh_compute         g^ir from @c ke.our_priv and @c ke.peer_pub (RFC 8031 sec 2)
 * @var IkeNs::auth_psk           AUTH = prf(prf(Shared Secret, pad), SignedOctets) (sec 2.15)
 * @var IkeNs::signed_octets      assemble RealMessage | Nonce | MACedID into @c auth.scratch (sec 2.15)
 * @var IkeNs::auth_sign_ecdsa_p256    sign those octets with P-256, writing r | s to @c out (RFC 7427 sec 3)
 * @var IkeNs::auth_verify_ecdsa_p256  verify a peer's P-256 signature over them (RFC 7427 sec 3)
 * @var IkeNs::auth_verify_rsa_sha256  verify a peer's RSA PKCS#1 v1.5 SHA-256 signature (sec 3.8)
 * @var IkeNs::sa_init_build      build HDR, SA, KE, Nonce as one IKE_SA_INIT message (sec 1.2)
 * @var IkeNs::sa_init_parse      parse an IKE_SA_INIT message into @c sa_init (sec 1.2)
 * @var IkeNs::auth_msg_build     build HDR, SK{ @c msg.inner } for an IKE_AUTH exchange (sec 3.14)
 * @var IkeNs::auth_msg_open      verify and decrypt an SK message in place into @c opened (sec 3.14)
 * @var IkeNs::initiator_start    emit the initiator's IKE_SA_INIT request (sec 1.2)
 * @var IkeNs::initiator_on_sa_init     consume the IKE_SA_INIT response and derive the keys (sec 1.2)
 * @var IkeNs::initiator_build_auth_psk emit SK{ IDi, AUTH } with a pre-shared key (sec 1.2, 2.15)
 * @var IkeNs::initiator_on_auth_psk    verify the responder's SK{ IDr, AUTH } (sec 1.2, 2.15)
 * @var IkeNs::responder_on_sa_init     consume an IKE_SA_INIT request and emit the response (sec 1.2)
 * @var IkeNs::responder_on_auth_psk    verify SK{ IDi, AUTH } and emit SK{ IDr, AUTH } (sec 1.2, 2.15)
 * @var IkeNs::informational_build  build an SK-protected INFORMATIONAL message (sec 1.4)
 * @var IkeNs::informational_open   verify and decrypt a received SK-protected message (sec 1.4)
 * @var IkeNs::create_child_sa_build  build an SK-protected CREATE_CHILD_SA message (sec 1.3)
 * @var IkeNs::internal   the calls that read this handle
 */
typedef struct
{
    uint8_t *work; ///< the caller's scratch region the hash, PRF and signature calls borrow

    IkeHeader hdr;        ///< the IKE header a build encodes and a parse decodes (sec 3.1)
    IkeOutArgs out;       ///< where a build writes
    IkeWireArgs wire;     ///< the octets a parse reads
    IkePayloadArgs pl;    ///< the generic payload header and the payload's variable field (sec 3.2)
    IkeProposalArgs prop; ///< the proposal a build encodes and the SPIs a payload names (sec 3.3.1)
    IkeKeArgs ke;         ///< the key exchange values (sec 3.4)
    IkeIdArgs id;         ///< the identity (sec 3.5, 3.6)
    IkeAuthArgs auth;     ///< authentication inputs (sec 2.15, 3.8)
    IkeNotifyArgs notify; ///< the Notify payload and the COOKIE (sec 3.10, 2.6)
    IkeTsArgs ts;         ///< the Traffic Selectors (sec 3.13)
    IkeCpArgs cp;         ///< the Configuration payload (sec 3.15)
    IkeSkArgs sk;         ///< the Encrypted payload and its AEAD inputs (sec 3.14)
    IkeFragArgs frag;     ///< the Encrypted Fragment counters and reassembly (RFC 7383)
    IkeKeyArgs keymat;    ///< the key schedule (sec 2.13, 2.14, 2.17, 2.18)
    IkeMsgArgs msg;       ///< whole-message assembly (sec 3.1, 3.14)
    IkeWalkArgs walk;     ///< the caller-owned walks (sec 3.2)
    IkeSessionArgs sess;  ///< the SA or handshake a call acts on (sec 1.2)

    proto_bool ok;
    size_t n;
    uint8_t u8;
    IkePayload payload;
    IkeProposalRef proposal;
    IkeTransformRef transform;
    IkeTrafficSelector sel;
    IkeCfgAttr attr;
    IkeKeRef ke_ref;
    IkeIdRef id_ref;
    IkeAuthRef auth_ref;
    IkeNotifyRef notify_ref;
    IkeDeleteRef delete_ref;
    IkeSkRef sk_ref;
    IkeCpRef cp_ref;
    IkeInnerRef opened;
    IkeSaInitMsg sa_init;

    void (*hdr_build)(struct IkeInternal *ctx);
    void (*hdr_parse)(struct IkeInternal *ctx);
    void (*set_length)(struct IkeInternal *ctx);
    void (*payload_iter_init)(struct IkeInternal *ctx);
    void (*payload_next)(struct IkeInternal *ctx);
    void (*payload_build)(struct IkeInternal *ctx);

    void (*sa_build)(struct IkeInternal *ctx);
    void (*ke_build)(struct IkeInternal *ctx);
    void (*nonce_build)(struct IkeInternal *ctx);
    void (*id_build)(struct IkeInternal *ctx);
    void (*auth_build)(struct IkeInternal *ctx);
    void (*cert_build)(struct IkeInternal *ctx);
    void (*notify_build)(struct IkeInternal *ctx);
    void (*delete_build)(struct IkeInternal *ctx);
    void (*ts_build)(struct IkeInternal *ctx);
    void (*cp_build)(struct IkeInternal *ctx);
    void (*sk_build)(struct IkeInternal *ctx);

    void (*skf_build)(struct IkeInternal *ctx);
    void (*skf_parse)(struct IkeInternal *ctx);
    void (*frag_reasm_init)(struct IkeInternal *ctx);
    void (*frag_reasm_add)(struct IkeInternal *ctx);
    void (*frag_reasm_complete)(struct IkeInternal *ctx);
    void (*frag_reasm_assemble)(struct IkeInternal *ctx);

    void (*cookie_compute)(struct IkeInternal *ctx);
    void (*cookie_verify)(struct IkeInternal *ctx);
    void (*cookie_notify_build)(struct IkeInternal *ctx);

    void (*ke_parse)(struct IkeInternal *ctx);
    void (*id_parse)(struct IkeInternal *ctx);
    void (*auth_parse)(struct IkeInternal *ctx);
    void (*notify_parse)(struct IkeInternal *ctx);
    void (*delete_parse)(struct IkeInternal *ctx);
    void (*sk_parse)(struct IkeInternal *ctx);
    void (*sa_first_proposal)(struct IkeInternal *ctx);
    void (*transform_iter_init)(struct IkeInternal *ctx);
    void (*transform_next)(struct IkeInternal *ctx);
    void (*ts_count)(struct IkeInternal *ctx);
    void (*ts_get)(struct IkeInternal *ctx);
    void (*cp_parse)(struct IkeInternal *ctx);
    void (*cp_attr_iter_init)(struct IkeInternal *ctx);
    void (*cp_attr_next)(struct IkeInternal *ctx);

    void (*prf_plus)(struct IkeInternal *ctx);
    void (*derive_keys)(struct IkeInternal *ctx);
    void (*rekey_derive_keys)(struct IkeInternal *ctx);
    void (*child_keymat)(struct IkeInternal *ctx);
    void (*suite_keylengths)(struct IkeInternal *ctx);
    void (*sa_keys_from_init)(struct IkeInternal *ctx);

    void (*sk_aead_seal)(struct IkeInternal *ctx);
    void (*sk_aead_open)(struct IkeInternal *ctx);
    void (*dh_public)(struct IkeInternal *ctx);
    void (*dh_compute)(struct IkeInternal *ctx);

    void (*auth_psk)(struct IkeInternal *ctx);
    void (*signed_octets)(struct IkeInternal *ctx);
    void (*auth_sign_ecdsa_p256)(struct IkeInternal *ctx);
    void (*auth_verify_ecdsa_p256)(struct IkeInternal *ctx);
    void (*auth_verify_rsa_sha256)(struct IkeInternal *ctx);

    void (*sa_init_build)(struct IkeInternal *ctx);
    void (*sa_init_parse)(struct IkeInternal *ctx);
    void (*auth_msg_build)(struct IkeInternal *ctx);
    void (*auth_msg_open)(struct IkeInternal *ctx);

    void (*initiator_start)(struct IkeInternal *ctx);
    void (*initiator_on_sa_init)(struct IkeInternal *ctx);
    void (*initiator_build_auth_psk)(struct IkeInternal *ctx);
    void (*initiator_on_auth_psk)(struct IkeInternal *ctx);
    void (*responder_on_sa_init)(struct IkeInternal *ctx);
    void (*responder_on_auth_psk)(struct IkeInternal *ctx);

    void (*informational_build)(struct IkeInternal *ctx);
    void (*informational_open)(struct IkeInternal *ctx);
    void (*create_child_sa_build)(struct IkeInternal *ctx);

    struct IkeInternal *internal;
} IkeNs;

/** @brief The one symbol this module exports. */
extern IkeNs Ike;

#endif // PROTOCORE_ENABLE_IKEV2

PROTOCORE_END_DECLS

#endif // PROTOCORE_IKEV2_H
