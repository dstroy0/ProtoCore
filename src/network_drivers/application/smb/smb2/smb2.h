// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SMB2_H
#define PROTOCORE_SMB2_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file smb2.h
 * @brief SMB2 client wire codec (MS-SMB2), PROTOCORE_ENABLE_SMB - increment 1: the transport
frame, the 64-byte sync packet header, and the NEGOTIATE exchange.
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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_SMB2_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*transport_frame)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *, size_t);
    uint32_t (*transport_len)(uint8_t *restrict, const uint8_t *, size_t);
    size_t (*build_header)(uint8_t *restrict, uint8_t *, size_t, Smb2Command, uint16_t, uint64_t, uint32_t, uint64_t);
    proto_bool (*parse_header)(uint8_t *restrict, const uint8_t *, size_t, Smb2Header *);
    size_t (*build_negotiate)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *, uint16_t);
    proto_bool (*parse_negotiate_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2NegotiateResp *);
    size_t (*build_negotiate_311)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *, uint16_t, const uint8_t *,
                                  size_t, const uint16_t *, size_t);
    proto_bool (*parse_negotiate_contexts)(uint8_t *restrict, const uint8_t *, size_t, Smb2NegotiateContexts *);
    void (*preauth_init)(uint8_t *restrict, SmbPreauth *);
    void (*preauth_update)(uint8_t *restrict, uint8_t *, SmbPreauth *, const uint8_t *, size_t);
    size_t (*build_session_setup)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint8_t, const uint8_t *,
                                  size_t);
    proto_bool (*parse_session_setup_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2SessionSetupResp *);
    size_t (*build_tree_connect)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, const uint8_t *, size_t);
    proto_bool (*parse_tree_connect_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2TreeConnectResp *);
    size_t (*build_create)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t,
                           uint32_t, uint32_t, const uint8_t *, size_t);
    proto_bool (*parse_create_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2CreateResp *);
    size_t (*build_close)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint32_t, const uint8_t *);
    proto_bool (*parse_close_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2CloseResp *);
    size_t (*build_read)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint32_t, const uint8_t *, uint32_t,
                         uint64_t);
    proto_bool (*parse_read_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2ReadResp *);
    size_t (*build_write)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint32_t, const uint8_t *,
                          const uint8_t *, size_t, uint64_t);
    proto_bool (*parse_write_response)(uint8_t *restrict, const uint8_t *, size_t, Smb2WriteResp *);
    void (*sign)(uint8_t *restrict, uint8_t *, const uint8_t *, uint8_t *, size_t);
    proto_bool (*verify)(uint8_t *restrict, uint8_t *, const uint8_t *, uint8_t *, size_t);
    void (*sign_cmac)(uint8_t *restrict, uint8_t *, const uint8_t *, uint8_t *, size_t);
    proto_bool (*verify_cmac)(uint8_t *restrict, uint8_t *, const uint8_t *, uint8_t *, size_t);
    proto_bool (*derive_signing_key)(uint8_t *restrict, const uint8_t *, uint16_t, const uint8_t *, uint8_t *);
    proto_bool (*derive_encryption_keys)(uint8_t *restrict, const uint8_t *, uint16_t, const uint8_t *, size_t,
                                         uint8_t *, uint8_t *);
    size_t (*encrypt)(uint8_t *restrict, uint16_t, const uint8_t *, const uint8_t *, uint64_t, const uint8_t *, size_t,
                      uint8_t *, size_t);
    size_t (*decrypt)(uint8_t *restrict, uint16_t, const uint8_t *, const uint8_t *, size_t, uint8_t *, size_t);
} Smb2Ns;
PROTOCORE_NS_LAYOUT(Smb2Ns, transport_frame, transport_len, build_header, parse_header, build_negotiate,
                    parse_negotiate_response, build_negotiate_311, parse_negotiate_contexts, preauth_init,
                    preauth_update, build_session_setup, parse_session_setup_response, build_tree_connect,
                    parse_tree_connect_response, build_create, parse_create_response, build_close, parse_close_response,
                    build_read, parse_read_response, build_write, parse_write_response, sign, verify, sign_cmac,
                    verify_cmac, derive_signing_key, derive_encryption_keys, encrypt, decrypt);

/**
 * @brief Prefix an SMB2 message with the 4-byte Direct-TCP transport header .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param msg Msg
 * @param msg_len Msg len
 * @return The size_t.
 */
size_t protocore_smb2_transport_frame(uint8_t *restrict work, uint8_t *out, size_t cap, const uint8_t *msg,
                                      size_t msg_len);
/**
 * @brief Read the Direct-TCP transport length prefix.
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @return The uint32_t.
 */
uint32_t protocore_smb2_transport_len(uint8_t *restrict work, const uint8_t *buf, size_t len);
/**
 * @brief Build a 64-byte SMB2 sync header into buf.
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param command Command
 * @param credit_request Credit request
 * @param message_id Message id
 * @param tree_id Tree id
 * @param session_id Session id
 * @return The size_t.
 */
size_t protocore_smb2_build_header(uint8_t *restrict work, uint8_t *buf, size_t cap, Smb2Command command,
                                   uint16_t credit_request, uint64_t message_id, uint32_t tree_id, uint64_t session_id);
/**
 * @brief Parse a 64-byte SMB2 sync header (validates ProtocolId + .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_header(uint8_t *restrict work, const uint8_t *buf, size_t len, Smb2Header *out);
/**
 * @brief Build a NEGOTIATE request (header + body) offering SMB 2.0.2 / 2.1 .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param client_guid the 16-byte client GUID 16 bytes
 * @param security_mode SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED
 * @return The size_t.
 */
size_t protocore_smb2_build_negotiate(uint8_t *restrict work, uint8_t *buf, size_t cap, const uint8_t *client_guid,
                                      uint16_t security_mode);
/**
 * @brief Parse a NEGOTIATE response message (the SMB2 header + §2.2.4 body).
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg the SMB2 message (starting at the sync header, transport prefix already stripped)
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_negotiate_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                                   Smb2NegotiateResp *out);
/**
 * @brief Build an SMB 3.1.1 NEGOTIATE request: the dialect list SMB 2.0.2 .. .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param client_guid 16 bytes
 * @param security_mode Security mode
 * @param salt the preauth-integrity salt (a fresh random blob the client keeps for the hash chain)
 * @param salt_len salt length in bytes (>= 1); a common choice is 32
 * @param ciphers cipher ids to offer, most-preferred first (a server picks the first it supports, in this
 * @param cipher_count number of entries in ciphers (0 .. PROTOCORE_SMB2_MAX_OFFER_CIPHERS)
 * @return The size_t.
 */
size_t protocore_smb2_build_negotiate_311(uint8_t *restrict work, uint8_t *buf, size_t cap, const uint8_t *client_guid,
                                          uint16_t security_mode, const uint8_t *salt, size_t salt_len,
                                          const uint16_t *ciphers, size_t cipher_count);
/**
 * @brief Walk the negotiate-context list of a 3.1.1 NEGOTIATE response .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_negotiate_contexts(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                                   Smb2NegotiateContexts *out);
/**
 * @brief Seed the preauth-integrity hash with 64 zero bytes (the initial .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param p P
 */
void protocore_smb2_preauth_init(uint8_t *restrict work, SmbPreauth *p);
/**
 * @brief Fold one handshake message into the preauth-integrity hash: hash = .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param crypto_work Crypto work
 * @param p P
 * @param msg Msg
 * @param len Len
 */
void protocore_smb2_preauth_update(uint8_t *restrict work, uint8_t *crypto_work, SmbPreauth *p, const uint8_t *msg,
                                   size_t len);
/**
 * @brief Build a SESSION_SETUP request (header + §2.2.5 body) carrying a .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id the SMB2 MessageId (increments across the exchange)
 * @param session_id 0 on the first round; the server-assigned SessionId on the second
 * @param security_mode SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED (one byte on the wire)
 * @param sec_buf Sec buf
 * @param sec_len Sec len
 * @return The size_t.
 */
size_t protocore_smb2_build_session_setup(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                          uint64_t session_id, uint8_t security_mode, const uint8_t *sec_buf,
                                          size_t sec_len);
/**
 * @brief Parse a SESSION_SETUP response message (the SMB2 header + §2.2.6 .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg the SMB2 message (starting at the sync header, transport prefix already stripped)
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_session_setup_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                                       Smb2SessionSetupResp *out);
/**
 * @brief Build a TREE_CONNECT request (header + §2.2.9 body) for a share path.
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id Message id
 * @param session_id Session id
 * @param path_utf16 the UNC path `\\server\share` in UTF-16LE (no NUL); path_len its byte length
 * @param path_len Path len
 * @return The size_t.
 */
size_t protocore_smb2_build_tree_connect(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                         uint64_t session_id, const uint8_t *path_utf16, size_t path_len);
/**
 * @brief Parse a TREE_CONNECT response message (validates command + .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_tree_connect_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                                      Smb2TreeConnectResp *out);
/**
 * @brief Build a CREATE request (header + §2.2.13 body) to open/create a .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id Message id
 * @param session_id Session id
 * @param tree_id Tree id
 * @param desired_access e.g. SMB2_FILE_GENERIC_READ / _WRITE
 * @param share_access SMB2_FILE_SHARE_* bitmask
 * @param create_disposition SMB2_FILE_OPEN / _CREATE / _OPEN_IF /
 * @param create_options SMB2_FILE_NON_DIRECTORY_FILE for a regular file
 * @param name_utf16 the file name relative to the share root in UTF-16LE (no leading backslash, no NUL);
 * @param name_len Name len
 * @return The size_t.
 */
size_t protocore_smb2_build_create(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                   uint64_t session_id, uint32_t tree_id, uint32_t desired_access,
                                   uint32_t share_access, uint32_t create_disposition, uint32_t create_options,
                                   const uint8_t *name_utf16, size_t name_len);
/**
 * @brief Parse a CREATE response message (validates command + StructureSize .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_create_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                                Smb2CreateResp *out);
/**
 * @brief Build a CLOSE request (header + §2.2.15 body) for an open FileId.
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id Message id
 * @param session_id Session id
 * @param tree_id Tree id
 * @param file_id 16 bytes
 * @return The size_t.
 */
size_t protocore_smb2_build_close(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                  uint64_t session_id, uint32_t tree_id, const uint8_t *file_id);
/**
 * @brief Parse a CLOSE response message (validates command + StructureSize .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_close_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                               Smb2CloseResp *out);
/**
 * @brief Build a READ request (header + §2.2.19 body) for length bytes at .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id Message id
 * @param session_id Session id
 * @param tree_id Tree id
 * @param file_id 16 bytes
 * @param length Length
 * @param offset Offset
 * @return The size_t.
 */
size_t protocore_smb2_build_read(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                 uint64_t session_id, uint32_t tree_id, const uint8_t *file_id, uint32_t length,
                                 uint64_t offset);
/**
 * @brief Parse a READ response message (validates command + StructureSize .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_read_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                              Smb2ReadResp *out);
/**
 * @brief Build a WRITE request (header + §2.2.21 body) writing data at .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param message_id Message id
 * @param session_id Session id
 * @param tree_id Tree id
 * @param file_id 16 bytes
 * @param data Data
 * @param data_len Data len
 * @param offset Offset
 * @return The size_t.
 */
size_t protocore_smb2_build_write(uint8_t *restrict work, uint8_t *buf, size_t cap, uint64_t message_id,
                                  uint64_t session_id, uint32_t tree_id, const uint8_t *file_id, const uint8_t *data,
                                  size_t data_len, uint64_t offset);
/**
 * @brief Parse a WRITE response message (validates command + StructureSize .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_parse_write_response(uint8_t *restrict work, const uint8_t *msg, size_t len,
                                               Smb2WriteResp *out);
/**
 * @brief Sign an SMB2 message in place (MS-SMB2 §3.1.4.1, SMB 2.x). Sets .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param crypto_work Crypto work
 * @param key the session signing key (16 octets; the NTLMv2 session key for SMB 2.x) 16 bytes
 * @param msg the full message (header + body), modified in place; must be at least a 64-byte header
 * @param msg_len total message length. A message shorter than the header is left untouched
 */
void protocore_smb2_sign(uint8_t *restrict work, uint8_t *crypto_work, const uint8_t *key, uint8_t *msg,
                         size_t msg_len);
/**
 * @brief Verify an SMB2 message's signature (MS-SMB2 §3.1.5.1). Recomputes .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param crypto_work Crypto work
 * @param key 16 bytes
 * @param msg Msg
 * @param msg_len Msg len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_verify(uint8_t *restrict work, uint8_t *crypto_work, const uint8_t *key, uint8_t *msg,
                                 size_t msg_len);
/**
 * @brief Sign an SMB2 message in place with AES-128-CMAC (MS-SMB2 §3.1.4.1, .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param crypto_work Crypto work
 * @param key 16 bytes
 * @param msg Msg
 * @param msg_len Msg len
 */
void protocore_smb2_sign_cmac(uint8_t *restrict work, uint8_t *crypto_work, const uint8_t *key, uint8_t *msg,
                              size_t msg_len);
/**
 * @brief Verify an AES-128-CMAC-signed SMB2 message (MS-SMB2 §3.1.5.1, SMB .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param crypto_work Crypto work
 * @param key 16 bytes
 * @param msg Msg
 * @param msg_len Msg len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_verify_cmac(uint8_t *restrict work, uint8_t *crypto_work, const uint8_t *key, uint8_t *msg,
                                      size_t msg_len);
/**
 * @brief Derive the 16-byte SMB 3.x signing key from the NTLM session key .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param session_key the 16-byte NTLM ExportedSessionKey (SessionBaseKey for NTLMv2 with no key exch) 16 bytes
 * @param dialect the negotiated DialectRevision (only 3.1.1 vs pre-3.1.1 matters here)
 * @param preauth the 64-byte final preauth-integrity hash; required iff dialect == 3.1.1, else ignored
 * @param out_key receives the 16-byte signing key 16 bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_derive_signing_key(uint8_t *restrict work, const uint8_t *session_key, uint16_t dialect,
                                             const uint8_t *preauth, uint8_t *out_key);
/**
 * @brief Derive the two SMB 3.x cipher keys from the NTLM session key .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param session_key 16 bytes
 * @param dialect Dialect
 * @param preauth Preauth
 * @param key_len Key len
 * @param out_c2s client->server key (ENCRYPTS our requests); out_s2c server->client key (DECRYPTS
 * @param out_s2c Out s2c
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smb2_derive_encryption_keys(uint8_t *restrict work, const uint8_t *session_key, uint16_t dialect,
                                                 const uint8_t *preauth, size_t key_len, uint8_t *out_c2s,
                                                 uint8_t *out_s2c);
/**
 * @brief Encrypt one SMB2 message into a TRANSFORM_HEADER-wrapped blob .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param cipher one of Smb2Cipher; selects the key length and AEAD nonce length
 * @param key cipher key (protocore_smb2_cipher_key_len(cipher) bytes, i.e. the C2S key)
 * @param nonce the 16-byte Nonce field; the leading nonce-length bytes must be UNIQUE per key (caller
 * @param session_id echoed into the header; out needs >= PROTOCORE_SMB2_TRANSFORM_HDR_LEN + msg_len
 * @param msg Msg
 * @param msg_len Msg len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_smb2_encrypt(uint8_t *restrict work, uint16_t cipher, const uint8_t *key, const uint8_t *nonce,
                              uint64_t session_id, const uint8_t *msg, size_t msg_len, uint8_t *out, size_t out_cap);
/**
 * @brief Decrypt a TRANSFORM_HEADER-wrapped SMB2 message (MS-SMB2 §3.1.4.4): .
 * @param work PROTOCORE_SMB2_BORROW bytes the caller took. Not held past the call.
 * @param cipher one of Smb2Cipher; key the S2C cipher key; out needs >= OriginalMessageSize
 * @param key Key
 * @param in In
 * @param in_len In len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_smb2_decrypt(uint8_t *restrict work, uint16_t cipher, const uint8_t *key, const uint8_t *in,
                              size_t in_len, uint8_t *out, size_t out_cap);

/** @brief Module namespace. */
PROTOCORE_NS Smb2Ns Smb2 PROTOCORE_UNUSED = {.transport_frame = protocore_smb2_transport_frame,
                                             .transport_len = protocore_smb2_transport_len,
                                             .build_header = protocore_smb2_build_header,
                                             .parse_header = protocore_smb2_parse_header,
                                             .build_negotiate = protocore_smb2_build_negotiate,
                                             .parse_negotiate_response = protocore_smb2_parse_negotiate_response,
                                             .build_negotiate_311 = protocore_smb2_build_negotiate_311,
                                             .parse_negotiate_contexts = protocore_smb2_parse_negotiate_contexts,
                                             .preauth_init = protocore_smb2_preauth_init,
                                             .preauth_update = protocore_smb2_preauth_update,
                                             .build_session_setup = protocore_smb2_build_session_setup,
                                             .parse_session_setup_response =
                                                 protocore_smb2_parse_session_setup_response,
                                             .build_tree_connect = protocore_smb2_build_tree_connect,
                                             .parse_tree_connect_response = protocore_smb2_parse_tree_connect_response,
                                             .build_create = protocore_smb2_build_create,
                                             .parse_create_response = protocore_smb2_parse_create_response,
                                             .build_close = protocore_smb2_build_close,
                                             .parse_close_response = protocore_smb2_parse_close_response,
                                             .build_read = protocore_smb2_build_read,
                                             .parse_read_response = protocore_smb2_parse_read_response,
                                             .build_write = protocore_smb2_build_write,
                                             .parse_write_response = protocore_smb2_parse_write_response,
                                             .sign = protocore_smb2_sign,
                                             .verify = protocore_smb2_verify,
                                             .sign_cmac = protocore_smb2_sign_cmac,
                                             .verify_cmac = protocore_smb2_verify_cmac,
                                             .derive_signing_key = protocore_smb2_derive_signing_key,
                                             .derive_encryption_keys = protocore_smb2_derive_encryption_keys,
                                             .encrypt = protocore_smb2_encrypt,
                                             .decrypt = protocore_smb2_decrypt};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SMB2_H
