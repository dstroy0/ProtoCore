// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb2.h
 * @brief SMB2 client wire codec (MS-SMB2), PROTOCORE_ENABLE_SMB - increment 1: the transport
 *        frame, the 64-byte sync packet header, and the NEGOTIATE exchange.
 *
 * Windows-share program storage is a common CNC file path (Fanuc / Haas / Mazak / Heidenhain
 * expose one), so a device can read/write `.nc` files over SMB2. This is the pure wire layer:
 * build the little-endian SMB2 messages and parse the responses; the TCP socket is the
 * application's. All fields are little-endian (SMB2 is a little-endian protocol).
 *
 * A client speaks SMB2 over Direct TCP (port 445): each message is prefixed by a 4-byte transport
 * header (`0x00` + a 24-bit big-endian length), then the 64-byte SMB2 sync header (MS-SMB2
 * §2.2.1.2), then the per-command body. The exchange begins with NEGOTIATE (§2.2.3 request /
 * §2.2.4 response): the client offers a dialect list, the server picks one and returns the SPNEGO
 * security token that seeds authentication.
 *
 * Shipped: the NEGOTIATE exchange; the NTLM crypto (smb_md / ntlm / ntlmssp); the SPNEGO wrapping
 * (spnego); the SESSION_SETUP request/response framing that carries those tokens; and the
 * TREE_CONNECT / CREATE / CLOSE / READ / WRITE file commands - the full read/write-a-file-on-a-share
 * client; **SMB 2.x message signing** (protocore_smb2_sign / protocore_smb2_verify, HMAC-SHA256) wired into the
 * client's SigningRequired path; and the **SMB 3.1.1 negotiate-context codec** (protocore_smb2_build_negotiate_311
 * / protocore_smb2_parse_negotiate_contexts - preauth-integrity SHA-512, signing, and encryption capabilities);
 * and the **SP800-108 counter-mode KDF** (protocore_kdf_ctr_hmac_sha256 in src/crypto/kdf, NIST-CAVP-verified) that
 * SMB 3.x uses to derive its keys. **SMB 3.1.1 runs end to end:** the client offers 2.0.2 .. 3.1.1, chains
 * the preauth-integrity hash (protocore_smb_preauth_*) across NEGOTIATE + both SESSION_SETUP rounds, derives the
 * signing key (derive_signing_key), and signs the session with AES-128-CMAC (protocore_smb2_sign_cmac /
 * _verify_cmac; crypto/aes_cmac) - the KDF assembly + CMAC cross-checked byte-for-byte against impacket.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMB2_H
#define PROTOCORE_SMB2_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SMB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Fixed SMB2 sync header size (MS-SMB2 §2.2.1). */
#define PROTOCORE_SMB2_HEADER_SIZE 64

/** @brief NEGOTIATE / SESSION_SETUP SecurityMode flags (MS-SMB2 §2.2.3). */
#define SMB2_NEGOTIATE_SIGNING_ENABLED 0x0001
#define SMB2_NEGOTIATE_SIGNING_REQUIRED 0x0002

/** @brief SMB2 header Flags field (MS-SMB2 §2.2.1.2). */
#define SMB2_FLAGS_SERVER_TO_REDIR 0x00000001 ///< set on a response (server -> client)
#define SMB2_FLAGS_SIGNED 0x00000008          ///< the message carries an HMAC signature

/** @brief SESSION_SETUP response SessionFlags (MS-SMB2 §2.2.6). */
#define SMB2_SESSION_FLAG_IS_GUEST 0x0001
#define SMB2_SESSION_FLAG_IS_NULL 0x0002
#define SMB2_SESSION_FLAG_ENCRYPT_DATA 0x0004

/** @brief NT status values seen in the SMB2 header during the SESSION_SETUP exchange. */
#define SMB2_STATUS_SUCCESS 0x00000000
#define SMB2_STATUS_MORE_PROCESSING_REQUIRED 0xC0000016 ///< server wants the next round
#define SMB2_STATUS_END_OF_FILE 0xC0000011              ///< a READ at/past end of file

/** @brief TREE_CONNECT response ShareType (MS-SMB2 §2.2.10). */
#define SMB2_SHARE_TYPE_DISK 0x01
#define SMB2_SHARE_TYPE_PIPE 0x02
#define SMB2_SHARE_TYPE_PRINT 0x03

/** @brief CREATE DesiredAccess masks (MS-DTYP ACCESS_MASK; the common file rights). */
#define SMB2_FILE_READ_DATA 0x00000001
#define SMB2_FILE_WRITE_DATA 0x00000002
#define SMB2_FILE_APPEND_DATA 0x00000004
#define SMB2_FILE_READ_ATTRIBUTES 0x00000080
#define SMB2_FILE_GENERIC_READ 0x00120089  ///< RC|SYNC|READ_ATTR|READ_EA|READ_DATA
#define SMB2_FILE_GENERIC_WRITE 0x00120116 ///< RC|SYNC|WRITE_ATTR|WRITE_EA|APPEND|WRITE

/** @brief CREATE ShareAccess (MS-SMB2 §2.2.13). */
#define SMB2_FILE_SHARE_READ 0x01
#define SMB2_FILE_SHARE_WRITE 0x02
#define SMB2_FILE_SHARE_DELETE 0x04

/** @brief CREATE CreateDisposition (MS-SMB2 §2.2.13). */
#define SMB2_FILE_SUPERSEDE 0
#define SMB2_FILE_OPEN 1      ///< open an existing file, fail if absent
#define SMB2_FILE_CREATE 2    ///< create, fail if it exists
#define SMB2_FILE_OPEN_IF 3   ///< open, create if absent
#define SMB2_FILE_OVERWRITE 4 ///< open + truncate, fail if absent
#define SMB2_FILE_OVERWRITE_IF 5

/** @brief CREATE CreateOptions (MS-SMB2 §2.2.13; the two we set). */
#define SMB2_FILE_DIRECTORY_FILE 0x00000001
#define SMB2_FILE_NON_DIRECTORY_FILE 0x00000040

/** @brief SMB 3.1.1 negotiate-context types (MS-SMB2 §2.2.3.1). */
#define SMB2_PREAUTH_INTEGRITY_CAPABILITIES 0x0001
#define SMB2_ENCRYPTION_CAPABILITIES 0x0002
#define SMB2_COMPRESSION_CAPABILITIES 0x0003
#define SMB2_NETNAME_NEGOTIATE_CONTEXT_ID 0x0005
#define SMB2_TRANSPORT_CAPABILITIES 0x0006
#define SMB2_RDMA_TRANSFORM_CAPABILITIES 0x0007
#define SMB2_SIGNING_CAPABILITIES 0x0008

/** @brief Preauth-integrity hash algorithm IDs (MS-SMB2 §2.2.3.1.1). */
#define SMB2_PREAUTH_INTEGRITY_SHA512 0x0001

/** @brief Signing algorithm IDs (MS-SMB2 §2.2.3.1.7). */
#define SMB2_SIGNING_HMAC_SHA256 0x0000
#define SMB2_SIGNING_AES_CMAC 0x0001
#define SMB2_SIGNING_AES_GMAC 0x0002

/** @brief NEGOTIATE request/response Capabilities flags (MS-SMB2 §2.2.3 / §2.2.4). A client that supports
 *  transport encryption MUST advertise SMB2_GLOBAL_CAP_ENCRYPTION here, or a server (e.g. Samba with
 *  `smb encrypt = required`) will not negotiate a cipher and will reject the unencrypted session (§3.2.4.2.2). */
#define SMB2_GLOBAL_CAP_ENCRYPTION 0x00000040

/** @brief Encryption cipher IDs (MS-SMB2 §2.2.3.1.2). */
#define SMB2_ENCRYPTION_AES128_CCM 0x0001
#define SMB2_ENCRYPTION_AES128_GCM 0x0002
#define SMB2_ENCRYPTION_AES256_CCM 0x0003
#define SMB2_ENCRYPTION_AES256_GCM 0x0004

/** @brief Max encryption ciphers a NEGOTIATE request can advertise (the four SMB 3.1.1 ciphers). */
#define PROTOCORE_SMB2_MAX_OFFER_CIPHERS 4

/** @brief Length of the SMB 3.1.1 preauth-integrity hash (SHA-512 digest size). */
#define PROTOCORE_SMB2_PREAUTH_HASH_LEN 64

/** @brief TREE_CONNECT response ShareFlags of interest (MS-SMB2 §2.2.10). */
#define SMB2_SHAREFLAG_ENCRYPT_DATA 0x00008000 ///< the share mandates SMB3 encryption

/** @brief TRANSFORM_HEADER size: ProtocolId(4)+Signature(16)+Nonce(16)+OriginalMessageSize(4)+Reserved(2)+
 *         Flags(2)+SessionId(8) = 52 bytes (MS-SMB2 §2.2.41). */
#define PROTOCORE_SMB2_TRANSFORM_HDR_LEN 52

/** @brief TRANSFORM_HEADER ProtocolId 0xFD 'S' 'M' 'B' as a little-endian u32. */
#define PROTOCORE_SMB2_TRANSFORM_PROTOCOL_ID 0x424D53FDu

/** @brief The TRANSFORM_HEADER Nonce field width (MS-SMB2 §2.2.41). The AEAD uses the leading
 *         protocore_smb2_cipher_nonce_len() bytes; the rest are zero. */
#define PROTOCORE_SMB2_NONCE_FIELD_LEN 16

/** @brief AES-GCM nonce length used within the 16-byte Nonce field. */
#define PROTOCORE_SMB2_GCM_NONCE_LEN 12

/** @brief AES-CCM nonce length used within the 16-byte Nonce field (MS-SMB2 §3.1.4.3). */
#define PROTOCORE_SMB2_CCM_NONCE_LEN 11

/** @brief Largest cipher key length across the four SMB 3.1.1 ciphers (AES-256), for buffer sizing. */
#define PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN 32

/** @brief SMB2 command codes (MS-SMB2 §2.2.1.2). */
typedef enum PROTO_ENUM_PACKED
{
    SMB2_NEGOTIATE = 0x0000,
    SMB2_SESSION_SETUP = 0x0001,
    SMB2_LOGOFF = 0x0002,
    SMB2_TREE_CONNECT = 0x0003,
    SMB2_TREE_DISCONNECT = 0x0004,
    SMB2_CREATE = 0x0005,
    SMB2_CLOSE = 0x0006,
    SMB2_READ = 0x0008,
    SMB2_WRITE = 0x0009,
} Smb2Command;

/** @brief SMB2 dialect revision numbers (MS-SMB2 §2.2.4). */
typedef enum PROTO_ENUM_PACKED
{
    SMB2_DIALECT_0202 = 0x0202, ///< SMB 2.0.2
    SMB2_DIALECT_0210 = 0x0210, ///< SMB 2.1
    SMB2_DIALECT_0300 = 0x0300, ///< SMB 3.0
    SMB2_DIALECT_0302 = 0x0302, ///< SMB 3.0.2
    SMB2_DIALECT_0311 = 0x0311, ///< SMB 3.1.1
} Smb2Dialect;

/** @brief Parsed SMB2 sync header. */
typedef struct
{
    Smb2Command command;
    uint32_t status; ///< NT status (response); 0 = STATUS_SUCCESS
    uint32_t flags;
    uint64_t message_id;
    uint32_t tree_id;
    uint64_t session_id;
    uint16_t credit_response;
} Smb2Header;

/** @brief Parsed SMB 3.1.1 NEGOTIATE-response negotiate contexts (MS-SMB2 §2.2.4 / §2.2.3.1). */
typedef struct
{
    proto_bool have_preauth;    ///< a PREAUTH_INTEGRITY_CAPABILITIES context was present
    uint16_t hash_algorithm;    ///< the server's chosen preauth hash (expect SMB2_PREAUTH_INTEGRITY_SHA512)
    const uint8_t *salt;        ///< the preauth-integrity salt (points into msg), or nullptr
    uint16_t salt_len;          ///< length of @ref salt
    proto_bool have_signing;    ///< a SIGNING_CAPABILITIES context was present
    uint16_t signing_algorithm; ///< the server's chosen signing algorithm
    proto_bool have_encryption; ///< an ENCRYPTION_CAPABILITIES context was present
    uint16_t cipher;            ///< the server's chosen cipher
} Smb2NegotiateContexts;

/** @brief Parsed NEGOTIATE response (MS-SMB2 §2.2.4). */
typedef struct
{
    uint16_t security_mode;
    uint16_t dialect; ///< the DialectRevision the server chose
    uint8_t server_guid[16];
    uint32_t capabilities;
    uint32_t max_transact;
    uint32_t max_read;
    uint32_t max_write;
    const uint8_t *sec_buf; ///< SPNEGO/NTLM security token (points into @p msg), or nullptr
    uint16_t sec_buf_len;
} Smb2NegotiateResp;

/**
 * @brief The SMB 3.1.1 preauth-integrity hash value (MS-SMB2 §3.1.5.2): a running SHA-512 chained over
 *        every NEGOTIATE and SESSION_SETUP message of the handshake. Its final value binds the whole
 *        pre-authentication exchange and feeds the 3.1.1 signing / encryption key derivation.
 */
typedef struct
{
    uint8_t hash[PROTOCORE_SMB2_PREAUTH_HASH_LEN];
} SmbPreauth;

/** @brief Parsed SESSION_SETUP response (MS-SMB2 §2.2.6). */
typedef struct
{
    uint16_t session_flags;
    const uint8_t *sec_buf; ///< the server's SPNEGO/NTLM token (points into @p msg), or nullptr
    uint16_t sec_buf_len;
} Smb2SessionSetupResp;

/** @brief Parsed TREE_CONNECT response (MS-SMB2 §2.2.10). The TreeId is in the response header. */
typedef struct
{
    uint8_t share_type;
    uint32_t share_flags;
    uint32_t capabilities;
    uint32_t maximal_access;
} Smb2TreeConnectResp;

/** @brief Parsed CREATE response (MS-SMB2 §2.2.14). */
typedef struct
{
    uint8_t file_id[16]; ///< the open handle (persistent 8 + volatile 8), for READ/WRITE/CLOSE
    uint64_t end_of_file;
    uint32_t create_action;
    uint32_t file_attributes;
} Smb2CreateResp;

/** @brief Parsed CLOSE response (MS-SMB2 §2.2.16). */
typedef struct
{
    uint64_t end_of_file;
    uint32_t file_attributes;
} Smb2CloseResp;

/** @brief Parsed READ response (MS-SMB2 §2.2.20). */
typedef struct
{
    const uint8_t *data; ///< the file bytes read (points into @p msg), or nullptr when DataLength is 0
    uint32_t data_len;
} Smb2ReadResp;

/** @brief Parsed WRITE response (MS-SMB2 §2.2.22). */
typedef struct
{
    uint32_t count; ///< bytes actually written
} Smb2WriteResp;

/** @brief The per-session message-signing algorithm the client selects from the negotiated dialect. */
typedef enum PROTO_ENUM_PACKED
{
    SMB2_SIGN_ALGO_HMAC_SHA256 = 0, ///< SMB 2.0.2 / 2.1 (key = the NTLMv2 session key)
    SMB2_SIGN_ALGO_AES_CMAC = 1,    ///< SMB 3.0 / 3.0.2 / 3.1.1 (key = the SP800-108-derived signing key)
} Smb2SignAlgo;

/** @brief AES key length in bytes for an SMB2 cipher id: 16 for the -128 ciphers, 32 for the -256 ciphers,
 *         0 if @p cipher is not a recognized cipher id. */
static inline size_t protocore_smb2_cipher_key_len(uint16_t cipher)
{
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES128_GCM:
        return 16;
    case SMB2_ENCRYPTION_AES256_CCM:
    case SMB2_ENCRYPTION_AES256_GCM:
        return 32;
    default:
        return 0;
    }
}

/** @brief AEAD nonce length in bytes for an SMB2 cipher id: 12 for the GCM ciphers, 11 for the CCM ciphers
 *         (MS-SMB2 §3.1.4.3), 0 if unrecognized. Both are written into the 16-byte TRANSFORM_HEADER Nonce
 *         field with the remaining bytes zero. */
static inline size_t protocore_smb2_cipher_nonce_len(uint16_t cipher)
{
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_GCM:
    case SMB2_ENCRYPTION_AES256_GCM:
        return 12;
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES256_CCM:
        return 11;
    default:
        return 0;
    }
}

/** @brief What transport_frame takes: out, cap, msg, msg_len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *msg;
    size_t msg_len;
} Smb2TransportFrameArgs;

/** @brief What transport_len takes: buf, len. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
} Smb2TransportLenArgs;

/** @brief What build_header takes: buf, cap, command, credit_request, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    Smb2Command command;
    uint16_t credit_request;
    uint64_t message_id;
    uint32_t tree_id;
    uint64_t session_id;
} Smb2BuildHeaderArgs;

/** @brief What parse_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    Smb2Header *out;
} Smb2ParseHeaderArgs;

/** @brief What build_negotiate takes: buf, cap, client_guid, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *client_guid; ///< the 16-byte client GUID 16 bytes.
    uint16_t security_mode;     ///< SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED
} Smb2BuildNegotiateArgs;

/** @brief What parse_negotiate_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg; ///< the SMB2 message (starting at the sync header, transport prefix already stripped)
    size_t len;
    Smb2NegotiateResp *out;
} Smb2ParseNegotiateResponseArgs;

/** @brief What build_negotiate_311 takes: buf, cap, client_guid, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *client_guid; ///< 16 bytes.
    uint16_t security_mode;
    const uint8_t *salt; ///< the preauth-integrity salt (a fresh random blob the client keeps for the hash chain)
    size_t salt_len;     ///< salt length in bytes (>= 1); a common choice is 32
    const uint16_t
        *ciphers; ///< cipher ids to offer, most-preferred first (a server picks the first it supports, in this ...
    size_t cipher_count; ///< number of entries in ciphers (0 .. PROTOCORE_SMB2_MAX_OFFER_CIPHERS)
} Smb2BuildNegotiate311Args;

/** @brief What parse_negotiate_contexts takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2NegotiateContexts *out;
} Smb2ParseNegotiateContextsArgs;

/** @brief What preauth_init takes: p. */
typedef struct
{
    SmbPreauth *p;
} Smb2ProtocoreSmbPreauthInitArgs;

/** @brief What preauth_update takes: crypto_work, p, ... */
typedef struct
{
    uint8_t *crypto_work;
    SmbPreauth *p;
    const uint8_t *msg;
    size_t len;
} Smb2ProtocoreSmbPreauthUpdateArgs;

/** @brief What build_session_setup takes: buf, cap, message_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;   ///< the SMB2 MessageId (increments across the exchange)
    uint64_t session_id;   ///< 0 on the first round; the server-assigned SessionId on the second
    uint8_t security_mode; ///< SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED (one byte on the wire)
    const uint8_t *sec_buf;
    size_t sec_len;
} Smb2BuildSessionSetupArgs;

/** @brief What parse_session_setup_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg; ///< the SMB2 message (starting at the sync header, transport prefix already stripped)
    size_t len;
    Smb2SessionSetupResp *out;
} Smb2ParseSessionSetupResponseArgs;

/** @brief What build_tree_connect takes: buf, cap, message_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;
    uint64_t session_id;
    const uint8_t *path_utf16; ///< the UNC path `\\server\share` in UTF-16LE (no NUL); path_len its byte length
    size_t path_len;
} Smb2BuildTreeConnectArgs;

/** @brief What parse_tree_connect_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2TreeConnectResp *out;
} Smb2ParseTreeConnectResponseArgs;

/** @brief What build_create takes: buf, cap, message_id, session_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;
    uint64_t session_id;
    uint32_t tree_id;
    uint32_t desired_access;     ///< e.g. SMB2_FILE_GENERIC_READ / _WRITE
    uint32_t share_access;       ///< SMB2_FILE_SHARE_* bitmask
    uint32_t create_disposition; ///< SMB2_FILE_OPEN / _CREATE / _OPEN_IF /
    uint32_t create_options;     ///< SMB2_FILE_NON_DIRECTORY_FILE for a regular file
    const uint8_t
        *name_utf16; ///< the file name relative to the share root in UTF-16LE (no leading backslash, no NUL); ...
    size_t name_len;
} Smb2BuildCreateArgs;

/** @brief What parse_create_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2CreateResp *out;
} Smb2ParseCreateResponseArgs;

/** @brief What build_close takes: buf, cap, message_id, session_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;
    uint64_t session_id;
    uint32_t tree_id;
    const uint8_t *file_id; ///< 16 bytes.
} Smb2BuildCloseArgs;

/** @brief What parse_close_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2CloseResp *out;
} Smb2ParseCloseResponseArgs;

/** @brief What build_read takes: buf, cap, message_id, session_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;
    uint64_t session_id;
    uint32_t tree_id;
    const uint8_t *file_id; ///< 16 bytes.
    uint32_t length;
    uint64_t offset;
} Smb2BuildReadArgs;

/** @brief What parse_read_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2ReadResp *out;
} Smb2ParseReadResponseArgs;

/** @brief What build_write takes: buf, cap, message_id, session_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t message_id;
    uint64_t session_id;
    uint32_t tree_id;
    const uint8_t *file_id; ///< 16 bytes.
    const uint8_t *data;
    size_t data_len;
    uint64_t offset;
} Smb2BuildWriteArgs;

/** @brief What parse_write_response takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    Smb2WriteResp *out;
} Smb2ParseWriteResponseArgs;

/** @brief What sign takes: crypto_work, key, msg, msg_len. */
typedef struct
{
    uint8_t *crypto_work;
    const uint8_t *key; ///< the session signing key (16 octets; the NTLMv2 session key for SMB 2.x) 16 bytes.
    uint8_t *msg;       ///< the full message (header + body), modified in place; must be at least a 64-byte header
    size_t msg_len;     ///< total message length. A message shorter than the header is left untouched
} Smb2SignArgs;

/** @brief What verify takes: crypto_work, key, msg, msg_len. */
typedef struct
{
    uint8_t *crypto_work;
    const uint8_t *key; ///< 16 bytes.
    uint8_t *msg;
    size_t msg_len;
} Smb2VerifyArgs;

/** @brief What sign_cmac takes: crypto_work, key, msg, msg_len. */
typedef struct
{
    uint8_t *crypto_work;
    const uint8_t *key; ///< 16 bytes.
    uint8_t *msg;
    size_t msg_len;
} Smb2SignCmacArgs;

/** @brief What verify_cmac takes: crypto_work, key, msg, msg_len. */
typedef struct
{
    uint8_t *crypto_work;
    const uint8_t *key; ///< 16 bytes.
    uint8_t *msg;
    size_t msg_len;
} Smb2VerifyCmacArgs;

/** @brief What derive_signing_key takes: session_key, ... */
typedef struct
{
    const uint8_t
        *session_key; ///< the 16-byte NTLM ExportedSessionKey (SessionBaseKey for NTLMv2 with no key exch) 16 bytes.
    uint16_t dialect; ///< the negotiated DialectRevision (only 3.1.1 vs pre-3.1.1 matters here)
    const uint8_t *preauth; ///< the 64-byte final preauth-integrity hash; required iff dialect == 3.1.1, else ignored
    uint8_t *out_key;       ///< receives the 16-byte signing key 16 bytes.
} Smb2ProtocoreSmb3DeriveSigningKeyArgs;

/** @brief What derive_encryption_keys takes: ... */
typedef struct
{
    const uint8_t *session_key; ///< 16 bytes.
    uint16_t dialect;
    const uint8_t *preauth;
    size_t key_len;
    uint8_t *out_c2s; ///< client->server key (ENCRYPTS our requests); out_s2c server->client key (DECRYPTS ...
    uint8_t *out_s2c;
} Smb2ProtocoreSmb3DeriveEncryptionKeysArgs;

/** @brief What encrypt takes: cipher, key, nonce, session_id, msg, ... */
typedef struct
{
    uint16_t cipher;      ///< one of Smb2Cipher; selects the key length and AEAD nonce length
    const uint8_t *key;   ///< cipher key (protocore_smb2_cipher_key_len(cipher) bytes, i.e. the C2S key)
    const uint8_t *nonce; ///< the 16-byte Nonce field; the leading nonce-length bytes must be UNIQUE per key (caller
                          ///< ... PROTOCORE_SMB2_NONCE_FIELD_LEN bytes.
    uint64_t session_id;  ///< echoed into the header; out needs >= PROTOCORE_SMB2_TRANSFORM_HDR_LEN + msg_len
    const uint8_t *msg;
    size_t msg_len;
    uint8_t *out;
    size_t out_cap;
} Smb2EncryptArgs;

/** @brief What decrypt takes: cipher, key, in, in_len, out, out_cap. */
typedef struct
{
    uint16_t cipher; ///< one of Smb2Cipher; key the S2C cipher key; out needs >= OriginalMessageSize
    const uint8_t *key;
    const uint8_t *in;
    size_t in_len;
    uint8_t *out;
    size_t out_cap;
} Smb2DecryptArgs;

/**
 * @brief SMB2 client wire codec (MS-SMB2), PROTOCORE_ENABLE_SMB - increment 1: the transport frame, the 64-byte sync
 * packet header, and the NEGOTIATE exchange.
 *
 * A caller sets the members a call takes, invokes it through ::Smb2 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Smb2.transport_frame_args.out = ...;
 *   Smb2.transport_frame_args.cap = ...;
 *   Smb2.transport_frame_args.msg = ...;
 *   Smb2.transport_frame_args.msg_len = ...;
 *   Smb2.transport_frame(work);
 *   // Smb2.n is what the call reports
 *
 * @var Smb2Ns::transport_frame_args  what transport_frame takes: out, cap, msg, msg_len
 * @var Smb2Ns::transport_len_args  what transport_len takes: buf, len
 * @var Smb2Ns::build_header_args  what build_header takes: buf, cap, command, credit_request,
 * @var Smb2Ns::parse_header_args  what parse_header takes: buf, len, out
 * @var Smb2Ns::build_negotiate_args  what build_negotiate takes: buf, cap, client_guid,
 * @var Smb2Ns::parse_negotiate_response_args  what parse_negotiate_response takes: msg, len, out
 * @var Smb2Ns::build_negotiate_311_args  what build_negotiate_311 takes: buf, cap, client_guid,
 * @var Smb2Ns::parse_negotiate_contexts_args  what parse_negotiate_contexts takes: msg, len, out
 * @var Smb2Ns::preauth_init_args  what preauth_init takes: p
 * @var Smb2Ns::preauth_update_args  what preauth_update takes: crypto_work, p,
 * @var Smb2Ns::build_session_setup_args  what build_session_setup takes: buf, cap, message_id,
 * @var Smb2Ns::parse_session_setup_response_args  what parse_session_setup_response takes: msg, len, out
 * @var Smb2Ns::build_tree_connect_args  what build_tree_connect takes: buf, cap, message_id,
 * @var Smb2Ns::parse_tree_connect_response_args  what parse_tree_connect_response takes: msg, len, out
 * @var Smb2Ns::build_create_args  what build_create takes: buf, cap, message_id, session_id,
 * @var Smb2Ns::parse_create_response_args  what parse_create_response takes: msg, len, out
 * @var Smb2Ns::build_close_args  what build_close takes: buf, cap, message_id, session_id,
 * @var Smb2Ns::parse_close_response_args  what parse_close_response takes: msg, len, out
 * @var Smb2Ns::build_read_args  what build_read takes: buf, cap, message_id, session_id,
 * @var Smb2Ns::parse_read_response_args  what parse_read_response takes: msg, len, out
 * @var Smb2Ns::build_write_args  what build_write takes: buf, cap, message_id, session_id,
 * @var Smb2Ns::parse_write_response_args  what parse_write_response takes: msg, len, out
 * @var Smb2Ns::sign_args  what sign takes: crypto_work, key, msg, msg_len
 * @var Smb2Ns::verify_args  what verify takes: crypto_work, key, msg, msg_len
 * @var Smb2Ns::sign_cmac_args  what sign_cmac takes: crypto_work, key, msg, msg_len
 * @var Smb2Ns::verify_cmac_args  what verify_cmac takes: crypto_work, key, msg, msg_len
 * @var Smb2Ns::derive_signing_key_args  what derive_signing_key takes: session_key,
 * @var Smb2Ns::derive_encryption_keys_args  what derive_encryption_keys takes:
 * @var Smb2Ns::encrypt_args  what encrypt takes: cipher, key, nonce, session_id, msg,
 * @var Smb2Ns::decrypt_args  what decrypt takes: cipher, key, in, in_len, out, out_cap
 * @var Smb2Ns::ok  true on a valid header; false if len < 64, ProtocolId != `FE 53 4D ...
 * @var Smb2Ns::n  total bytes written (4 + msg_len), or 0 on overflow / a length that ...
 * @var Smb2Ns::u32  the SMB2 message length that follows the 4-byte prefix, or 0 if len ...
 * @var Smb2Ns::transport_frame  prefix an SMB2 message with the 4-byte Direct-TCP transport header ...
 * @var Smb2Ns::transport_len  read the Direct-TCP transport length prefix
 * @var Smb2Ns::build_header  build a 64-byte SMB2 sync header into buf
 * @var Smb2Ns::parse_header  parse a 64-byte SMB2 sync header (validates ProtocolId + ...
 * @var Smb2Ns::build_negotiate  build a NEGOTIATE request (header + body) offering SMB 2.0.2 / 2.1 ...
 * @var Smb2Ns::parse_negotiate_response  parse a NEGOTIATE response message (the SMB2 header + §2.2.4 body)
 * @var Smb2Ns::build_negotiate_311  build an SMB 3.1.1 NEGOTIATE request: the dialect list SMB 2.0.2 .. ...
 * @var Smb2Ns::parse_negotiate_contexts  walk the negotiate-context list of a 3.1.1 NEGOTIATE response ...
 * @var Smb2Ns::preauth_init  seed the preauth-integrity hash with 64 zero bytes (the initial ...
 * @var Smb2Ns::preauth_update  fold one handshake message into the preauth-integrity hash: hash = ...
 * @var Smb2Ns::build_session_setup  build a SESSION_SETUP request (header + §2.2.5 body) carrying a ...
 * @var Smb2Ns::parse_session_setup_response  parse a SESSION_SETUP response message (the SMB2 header + §2.2.6 ...
 * @var Smb2Ns::build_tree_connect  build a TREE_CONNECT request (header + §2.2.9 body) for a share path
 * @var Smb2Ns::parse_tree_connect_response  parse a TREE_CONNECT response message (validates command + ...
 * @var Smb2Ns::build_create  build a CREATE request (header + §2.2.13 body) to open/create a ...
 * @var Smb2Ns::parse_create_response  parse a CREATE response message (validates command + StructureSize ...
 * @var Smb2Ns::build_close  build a CLOSE request (header + §2.2.15 body) for an open FileId
 * @var Smb2Ns::parse_close_response  parse a CLOSE response message (validates command + StructureSize ...
 * @var Smb2Ns::build_read  build a READ request (header + §2.2.19 body) for length bytes at ...
 * @var Smb2Ns::parse_read_response  parse a READ response message (validates command + StructureSize ...
 * @var Smb2Ns::build_write  build a WRITE request (header + §2.2.21 body) writing data at ...
 * @var Smb2Ns::parse_write_response  parse a WRITE response message (validates command + StructureSize ...
 * @var Smb2Ns::sign  sign an SMB2 message in place (MS-SMB2 §3.1.4.1, SMB 2.x). Sets ...
 * @var Smb2Ns::verify  verify an SMB2 message's signature (MS-SMB2 §3.1.5.1). Recomputes ...
 * @var Smb2Ns::sign_cmac  sign an SMB2 message in place with AES-128-CMAC (MS-SMB2 §3.1.4.1, ...
 * @var Smb2Ns::verify_cmac  verify an AES-128-CMAC-signed SMB2 message (MS-SMB2 §3.1.5.1, SMB ...
 * @var Smb2Ns::derive_signing_key  derive the 16-byte SMB 3.x signing key from the NTLM session key ...
 * @var Smb2Ns::derive_encryption_keys  derive the two SMB 3.x cipher keys from the NTLM session key ...
 * @var Smb2Ns::encrypt  encrypt one SMB2 message into a TRANSFORM_HEADER-wrapped blob ...
 * @var Smb2Ns::decrypt  decrypt a TRANSFORM_HEADER-wrapped SMB2 message (MS-SMB2 §3.1.4.4): ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Smb2TransportFrameArgs transport_frame_args;
    Smb2TransportLenArgs transport_len_args;
    Smb2BuildHeaderArgs build_header_args;
    Smb2ParseHeaderArgs parse_header_args;
    Smb2BuildNegotiateArgs build_negotiate_args;
    Smb2ParseNegotiateResponseArgs parse_negotiate_response_args;
    Smb2BuildNegotiate311Args build_negotiate_311_args;
    Smb2ParseNegotiateContextsArgs parse_negotiate_contexts_args;
    Smb2ProtocoreSmbPreauthInitArgs preauth_init_args;
    Smb2ProtocoreSmbPreauthUpdateArgs preauth_update_args;
    Smb2BuildSessionSetupArgs build_session_setup_args;
    Smb2ParseSessionSetupResponseArgs parse_session_setup_response_args;
    Smb2BuildTreeConnectArgs build_tree_connect_args;
    Smb2ParseTreeConnectResponseArgs parse_tree_connect_response_args;
    Smb2BuildCreateArgs build_create_args;
    Smb2ParseCreateResponseArgs parse_create_response_args;
    Smb2BuildCloseArgs build_close_args;
    Smb2ParseCloseResponseArgs parse_close_response_args;
    Smb2BuildReadArgs build_read_args;
    Smb2ParseReadResponseArgs parse_read_response_args;
    Smb2BuildWriteArgs build_write_args;
    Smb2ParseWriteResponseArgs parse_write_response_args;
    Smb2SignArgs sign_args;
    Smb2VerifyArgs verify_args;
    Smb2SignCmacArgs sign_cmac_args;
    Smb2VerifyCmacArgs verify_cmac_args;
    Smb2ProtocoreSmb3DeriveSigningKeyArgs derive_signing_key_args;
    Smb2ProtocoreSmb3DeriveEncryptionKeysArgs derive_encryption_keys_args;
    Smb2EncryptArgs encrypt_args;
    Smb2DecryptArgs decrypt_args;

    proto_bool ok;
    size_t n;
    uint32_t u32;

    void (*const transport_frame)(uint8_t *restrict work);
    void (*const transport_len)(uint8_t *restrict work);
    void (*const build_header)(uint8_t *restrict work);
    void (*const parse_header)(uint8_t *restrict work);
    void (*const build_negotiate)(uint8_t *restrict work);
    void (*const parse_negotiate_response)(uint8_t *restrict work);
    void (*const build_negotiate_311)(uint8_t *restrict work);
    void (*const parse_negotiate_contexts)(uint8_t *restrict work);
    void (*const preauth_init)(uint8_t *restrict work);
    void (*const preauth_update)(uint8_t *restrict work);
    void (*const build_session_setup)(uint8_t *restrict work);
    void (*const parse_session_setup_response)(uint8_t *restrict work);
    void (*const build_tree_connect)(uint8_t *restrict work);
    void (*const parse_tree_connect_response)(uint8_t *restrict work);
    void (*const build_create)(uint8_t *restrict work);
    void (*const parse_create_response)(uint8_t *restrict work);
    void (*const build_close)(uint8_t *restrict work);
    void (*const parse_close_response)(uint8_t *restrict work);
    void (*const build_read)(uint8_t *restrict work);
    void (*const parse_read_response)(uint8_t *restrict work);
    void (*const build_write)(uint8_t *restrict work);
    void (*const parse_write_response)(uint8_t *restrict work);
    void (*const sign)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
    void (*const sign_cmac)(uint8_t *restrict work);
    void (*const verify_cmac)(uint8_t *restrict work);
    void (*const derive_signing_key)(uint8_t *restrict work);
    void (*const derive_encryption_keys)(uint8_t *restrict work);
    void (*const encrypt)(uint8_t *restrict work);
    void (*const decrypt)(uint8_t *restrict work);
} Smb2Ns;

/** @brief The one symbol this module exports. */
extern Smb2Ns Smb2;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB

#endif // PROTOCORE_SMB2_H
