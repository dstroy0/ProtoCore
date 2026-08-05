// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb2.h
 * @brief SMB2 client wire codec (MS-SMB2), PC_ENABLE_SMB - increment 1: the transport
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
 * client; **SMB 2.x message signing** (pc_smb2_sign / pc_smb2_verify, HMAC-SHA256) wired into the
 * client's SigningRequired path; and the **SMB 3.1.1 negotiate-context codec** (pc_smb2_build_negotiate_311
 * / pc_smb2_parse_negotiate_contexts - preauth-integrity SHA-512, signing, and encryption capabilities);
 * and the **SP800-108 counter-mode KDF** (pc_kdf_ctr_hmac_sha256 in src/crypto/kdf, NIST-CAVP-verified) that
 * SMB 3.x uses to derive its keys. **SMB 3.1.1 runs end to end:** the client offers 2.0.2 .. 3.1.1, chains
 * the preauth-integrity hash (pc_smb_preauth_*) across NEGOTIATE + both SESSION_SETUP rounds, derives the
 * signing key (pc_smb3_derive_signing_key), and signs the session with AES-128-CMAC (pc_smb2_sign_cmac /
 * _verify_cmac; crypto/aes_cmac) - the KDF assembly + CMAC cross-checked byte-for-byte against impacket.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMB2_H
#define PROTOCORE_SMB2_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SMB

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

// SMB2 wire constants: flag words that get OR'd/AND'd, and field values compared against the
// uint8/16/32 wire fields of a parsed header.

/** @brief Fixed SMB2 sync header size (MS-SMB2 §2.2.1). */
#define PC_SMB2_HEADER_SIZE 64

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

/** @brief AES key length in bytes for an SMB2 cipher id: 16 for the -128 ciphers, 32 for the -256 ciphers,
 *         0 if @p cipher is not a recognized cipher id. */
static inline size_t pc_smb2_cipher_key_len(uint16_t cipher)
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
static inline size_t pc_smb2_cipher_nonce_len(uint16_t cipher)
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
 * @brief Prefix an SMB2 message with the 4-byte Direct-TCP transport header (`0x00` + a 24-bit
 *        big-endian length) into @p out.
 * @return total bytes written (4 + @p msg_len), or 0 on overflow / a length that does not fit 24 bits.
 */
size_t pc_smb2_transport_frame(uint8_t *out, size_t cap, const uint8_t *msg, size_t msg_len);

/**
 * @brief Read the Direct-TCP transport length prefix.
 * @return the SMB2 message length that follows the 4-byte prefix, or 0 if @p len < 4 or the first
 *         byte is non-zero (an invalid Direct-TCP frame).
 */
uint32_t pc_smb2_transport_len(const uint8_t *buf, size_t len);

/**
 * @brief Build a 64-byte SMB2 sync header into @p buf.
 * @return PC_SMB2_HEADER_SIZE, or 0 if @p cap < 64.
 */
size_t pc_smb2_build_header(uint8_t *buf, size_t cap, Smb2Command command, uint16_t credit_request, uint64_t message_id,
                            uint32_t tree_id, uint64_t session_id);

/**
 * @brief Parse a 64-byte SMB2 sync header (validates ProtocolId + StructureSize).
 * @return true on a valid header; false if @p len < 64, ProtocolId != `FE 53 4D 42`, or
 *         StructureSize != 64.
 */
proto_bool pc_smb2_parse_header(const uint8_t *buf, size_t len, Smb2Header *out);

/**
 * @brief Build a NEGOTIATE request (header + body) offering SMB 2.0.2 / 2.1 / 3.0 / 3.0.2.
 * @param client_guid   the 16-byte client GUID.
 * @param security_mode SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED.
 * @return total message bytes (no transport prefix), or 0 on overflow.
 */
size_t pc_smb2_build_negotiate(uint8_t *buf, size_t cap, const uint8_t client_guid[16], uint16_t security_mode);

/**
 * @brief Parse a NEGOTIATE response message (the SMB2 header + §2.2.4 body).
 *
 * @param msg the SMB2 message (starting at the sync header, transport prefix already stripped).
 * @return true on a well-formed response (header valid, command == NEGOTIATE, StructureSize == 65,
 *         and the security buffer within bounds); false otherwise. On success @p out->sec_buf points
 *         into @p msg (or is nullptr when SecurityBufferLength is 0).
 */
proto_bool pc_smb2_parse_negotiate_response(const uint8_t *msg, size_t len, Smb2NegotiateResp *out);

/** @brief Max encryption ciphers a NEGOTIATE request can advertise (the four SMB 3.1.1 ciphers). */
#define PC_SMB2_MAX_OFFER_CIPHERS 4

/**
 * @brief Build an SMB 3.1.1 NEGOTIATE request: the dialect list SMB 2.0.2 .. 3.1.1 followed by the
 *        mandatory PREAUTH_INTEGRITY_CAPABILITIES negotiate context (SHA-512 + @p salt), a
 *        SIGNING_CAPABILITIES context advertising AES-CMAC, and an ENCRYPTION_CAPABILITIES context listing
 *        @p ciphers in preference order (MS-SMB2 §2.2.3 / §2.2.3.1.1 / §2.2.3.1.2 / §2.2.3.1.7). The
 *        Capabilities field advertises SMB2_GLOBAL_CAP_ENCRYPTION so a server that requires encryption will
 *        negotiate a cipher (§3.2.4.2.2.2).
 *
 * Offering 0x0311 obliges the client to send the preauth-integrity context, so this is a distinct
 * builder from ::pc_smb2_build_negotiate (which stops at 3.0.2). The NegotiateContextOffset /
 * NegotiateContextCount fields overlay the pre-3.1.1 ClientStartTime, and each context is 8-byte
 * aligned per §2.2.3.1.
 *
 * @param salt         the preauth-integrity salt (a fresh random blob the client keeps for the hash chain).
 * @param salt_len     salt length in bytes (>= 1); a common choice is 32.
 * @param ciphers      cipher ids to offer, most-preferred first (a server picks the first it supports, in
 *                     this order). May be null when @p cipher_count is 0 (offer no encryption).
 * @param cipher_count number of entries in @p ciphers (0 .. PC_SMB2_MAX_OFFER_CIPHERS).
 * @return total message bytes (no transport prefix), or 0 on overflow / bad args.
 */
size_t pc_smb2_build_negotiate_311(uint8_t *buf, size_t cap, const uint8_t client_guid[16], uint16_t security_mode,
                                   const uint8_t *salt, size_t salt_len, const uint16_t *ciphers, size_t cipher_count);

/**
 * @brief Walk the negotiate-context list of a 3.1.1 NEGOTIATE response (located by NegotiateContextOffset
 *        / NegotiateContextCount in the §2.2.4 body) and extract the preauth hash + salt, the signing
 *        algorithm, and the cipher. Every context header and its data are bounds-checked against @p len.
 * @return true if the list parsed cleanly (all contexts within bounds); false on a malformed / truncated
 *         list. Absent context types leave their `have_*` flag false.
 */
proto_bool pc_smb2_parse_negotiate_contexts(const uint8_t *msg, size_t len, Smb2NegotiateContexts *out);

/** @brief Length of the SMB 3.1.1 preauth-integrity hash (SHA-512 digest size). */
#define PC_SMB2_PREAUTH_HASH_LEN 64

/**
 * @brief The SMB 3.1.1 preauth-integrity hash value (MS-SMB2 §3.1.5.2): a running SHA-512 chained over
 *        every NEGOTIATE and SESSION_SETUP message of the handshake. Its final value binds the whole
 *        pre-authentication exchange and feeds the 3.1.1 signing / encryption key derivation.
 */
typedef struct
{
    uint8_t hash[PC_SMB2_PREAUTH_HASH_LEN];
} SmbPreauth;

/** @brief Seed the preauth-integrity hash with 64 zero bytes (the initial value, MS-SMB2 §3.1.5.2). */
void pc_smb_preauth_init(SmbPreauth *p);

/**
 * @brief Fold one handshake message into the preauth-integrity hash: hash = SHA-512(hash || msg).
 *        Call once per NEGOTIATE / SESSION_SETUP message (request and response), in wire order, passing
 *        the SMB2 message (header + body) without the Direct-TCP transport prefix.
 */
void pc_smb_preauth_update(SmbPreauth *p, const uint8_t *msg, size_t len);

/** @brief Parsed SESSION_SETUP response (MS-SMB2 §2.2.6). */
typedef struct
{
    uint16_t session_flags;
    const uint8_t *sec_buf; ///< the server's SPNEGO/NTLM token (points into @p msg), or nullptr
    uint16_t sec_buf_len;
} Smb2SessionSetupResp;

/**
 * @brief Build a SESSION_SETUP request (header + §2.2.5 body) carrying a security token.
 *
 * The token is the SPNEGO/NTLMSSP blob for this round of the handshake: the InitialContextToken on
 * the first request, and (echoing the SessionId the server returned) the AUTHENTICATE NegTokenResp
 * on the second. Capabilities / Channel / PreviousSessionId are 0 (a plain new session).
 *
 * @param message_id    the SMB2 MessageId (increments across the exchange).
 * @param session_id    0 on the first round; the server-assigned SessionId on the second.
 * @param security_mode SMB2_NEGOTIATE_SIGNING_ENABLED and/or _REQUIRED (one byte on the wire).
 * @return total message bytes (no transport prefix), or 0 on overflow / empty token.
 */
size_t pc_smb2_build_session_setup(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id,
                                   uint8_t security_mode, const uint8_t *sec_buf, size_t sec_len);

/**
 * @brief Parse a SESSION_SETUP response message (the SMB2 header + §2.2.6 body).
 *
 * The caller reads the SessionId (to echo on the next round) and the NT status
 * (SMB2_STATUS_MORE_PROCESSING_REQUIRED vs SMB2_STATUS_SUCCESS) from pc_smb2_parse_header on the same
 * @p msg; this extracts the SessionFlags and the server security buffer.
 *
 * @param msg the SMB2 message (starting at the sync header, transport prefix already stripped).
 * @return true on a well-formed response (header valid, command == SESSION_SETUP, StructureSize == 9,
 *         security buffer within bounds); false otherwise. On success @p out->sec_buf points into
 *         @p msg (or is nullptr when SecurityBufferLength is 0).
 */
proto_bool pc_smb2_parse_session_setup_response(const uint8_t *msg, size_t len, Smb2SessionSetupResp *out);

/**
 * @brief Build a TREE_CONNECT request (header + §2.2.9 body) for a share path.
 * @param path_utf16 the UNC path `\\server\share` in UTF-16LE (no NUL); @p path_len its byte length.
 * @return total message bytes (no transport prefix), or 0 on overflow / empty path.
 */
size_t pc_smb2_build_tree_connect(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id,
                                  const uint8_t *path_utf16, size_t path_len);

/** @brief TREE_CONNECT response ShareFlags of interest (MS-SMB2 §2.2.10). */
#define SMB2_SHAREFLAG_ENCRYPT_DATA 0x00008000 ///< the share mandates SMB3 encryption

/** @brief Parsed TREE_CONNECT response (MS-SMB2 §2.2.10). The TreeId is in the response header. */
typedef struct
{
    uint8_t share_type;
    uint32_t share_flags;
    uint32_t capabilities;
    uint32_t maximal_access;
} Smb2TreeConnectResp;

/**
 * @brief Parse a TREE_CONNECT response message (validates command + StructureSize 16).
 * @return true on a well-formed response; the caller reads the TreeId from pc_smb2_parse_header.
 */
proto_bool pc_smb2_parse_tree_connect_response(const uint8_t *msg, size_t len, Smb2TreeConnectResp *out);

/**
 * @brief Build a CREATE request (header + §2.2.13 body) to open/create a file on the tree.
 * @param name_utf16 the file name relative to the share root in UTF-16LE (no leading backslash, no
 *                   NUL); @p name_len its byte length (must be > 0).
 * @param desired_access     e.g. SMB2_FILE_GENERIC_READ / _WRITE.
 * @param share_access       SMB2_FILE_SHARE_* bitmask.
 * @param create_disposition SMB2_FILE_OPEN / _CREATE / _OPEN_IF / ...
 * @param create_options     SMB2_FILE_NON_DIRECTORY_FILE for a regular file.
 * @return total message bytes (no transport prefix), or 0 on overflow.
 */
size_t pc_smb2_build_create(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                            uint32_t desired_access, uint32_t share_access, uint32_t create_disposition,
                            uint32_t create_options, const uint8_t *name_utf16, size_t name_len);

/** @brief Parsed CREATE response (MS-SMB2 §2.2.14). */
typedef struct
{
    uint8_t file_id[16]; ///< the open handle (persistent 8 + volatile 8), for READ/WRITE/CLOSE
    uint64_t end_of_file;
    uint32_t create_action;
    uint32_t file_attributes;
} Smb2CreateResp;

/**
 * @brief Parse a CREATE response message (validates command + StructureSize 89, FileId in bounds).
 * @return true on a well-formed response.
 */
proto_bool pc_smb2_parse_create_response(const uint8_t *msg, size_t len, Smb2CreateResp *out);

/**
 * @brief Build a CLOSE request (header + §2.2.15 body) for an open FileId.
 * @return total message bytes (no transport prefix), or 0 on overflow.
 */
size_t pc_smb2_build_close(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                           const uint8_t file_id[16]);

/** @brief Parsed CLOSE response (MS-SMB2 §2.2.16). */
typedef struct
{
    uint64_t end_of_file;
    uint32_t file_attributes;
} Smb2CloseResp;

/**
 * @brief Parse a CLOSE response message (validates command + StructureSize 60).
 * @return true on a well-formed response.
 */
proto_bool pc_smb2_parse_close_response(const uint8_t *msg, size_t len, Smb2CloseResp *out);

/**
 * @brief Build a READ request (header + §2.2.19 body) for @p length bytes at @p offset of an open file.
 * @return total message bytes (no transport prefix), or 0 on overflow.
 */
size_t pc_smb2_build_read(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                          const uint8_t file_id[16], uint32_t length, uint64_t offset);

/** @brief Parsed READ response (MS-SMB2 §2.2.20). */
typedef struct
{
    const uint8_t *data; ///< the file bytes read (points into @p msg), or nullptr when DataLength is 0
    uint32_t data_len;
} Smb2ReadResp;

/**
 * @brief Parse a READ response message (validates command + StructureSize 17, data within bounds).
 * @return true on a well-formed response.
 */
proto_bool pc_smb2_parse_read_response(const uint8_t *msg, size_t len, Smb2ReadResp *out);

/**
 * @brief Build a WRITE request (header + §2.2.21 body) writing @p data at @p offset of an open file.
 * @return total message bytes (no transport prefix), or 0 on overflow / empty data.
 */
size_t pc_smb2_build_write(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                           const uint8_t file_id[16], const uint8_t *data, size_t data_len, uint64_t offset);

/** @brief Parsed WRITE response (MS-SMB2 §2.2.22). */
typedef struct
{
    uint32_t count; ///< bytes actually written
} Smb2WriteResp;

/**
 * @brief Parse a WRITE response message (validates command + StructureSize 17).
 * @return true on a well-formed response.
 */
proto_bool pc_smb2_parse_write_response(const uint8_t *msg, size_t len, Smb2WriteResp *out);

// ---------------------------------------------------------------------------
// Message signing (MS-SMB2 §3.1.4.1 / §3.1.5.1) - SMB 2.x: HMAC-SHA256; SMB 3.x: AES-128-CMAC
// ---------------------------------------------------------------------------

/** @brief The per-session message-signing algorithm the client selects from the negotiated dialect. */
typedef enum PROTO_ENUM_PACKED
{
    SMB2_SIGN_ALGO_HMAC_SHA256 = 0, ///< SMB 2.0.2 / 2.1 (key = the NTLMv2 session key)
    SMB2_SIGN_ALGO_AES_CMAC = 1,    ///< SMB 3.0 / 3.0.2 / 3.1.1 (key = the SP800-108-derived signing key)
} Smb2SignAlgo;

/**
 * @brief Sign an SMB2 message in place (MS-SMB2 §3.1.4.1, SMB 2.x). Sets SMB2_FLAGS_SIGNED in the Flags
 *        field, zeroes the 16-byte Signature, HMAC-SHA256s the whole message under the 16-byte session
 *        signing @p key, and writes the MAC's first 16 octets into the Signature field.
 * @param key      the session signing key (16 octets; the NTLMv2 session key for SMB 2.x).
 * @param msg      the full message (header + body), modified in place; must be at least a 64-byte header.
 * @param msg_len  total message length. A message shorter than the header is left untouched.
 */
void pc_smb2_sign(const uint8_t key[16], uint8_t *msg, size_t msg_len);

/**
 * @brief Verify an SMB2 message's signature (MS-SMB2 §3.1.5.1). Recomputes the HMAC-SHA256 over @p msg
 *        with the Signature field zeroed and constant-time-compares the first 16 octets to the received
 *        Signature. @p msg is restored unchanged before returning.
 * @return true iff the signature matches; false on a mismatch or a message shorter than the header.
 */
proto_bool pc_smb2_verify(const uint8_t key[16], uint8_t *msg, size_t msg_len);

// ---------------------------------------------------------------------------
// SMB 3.x signing (MS-SMB2 §3.1.4.1 / §3.1.5.1) - AES-128-CMAC (dialects 3.0 / 3.0.2 / 3.1.1)
// ---------------------------------------------------------------------------

/**
 * @brief Sign an SMB2 message in place with AES-128-CMAC (MS-SMB2 §3.1.4.1, SMB 3.x). Identical framing
 *        to ::pc_smb2_sign - sets SMB2_FLAGS_SIGNED, zeroes the Signature, MACs the whole message under
 *        the 16-byte SMB 3.x signing @p key - but the MAC is AES-CMAC, whose full 16-octet tag is the
 *        Signature. @p msg shorter than a 64-byte header is left untouched.
 */
void pc_smb2_sign_cmac(const uint8_t key[16], uint8_t *msg, size_t msg_len);

/**
 * @brief Verify an AES-128-CMAC-signed SMB2 message (MS-SMB2 §3.1.5.1, SMB 3.x). Recomputes the CMAC
 *        with the Signature zeroed and constant-time-compares to the received Signature; @p msg is
 *        restored unchanged.
 * @return true iff the signature matches; false on a mismatch or a message shorter than the header.
 */
proto_bool pc_smb2_verify_cmac(const uint8_t key[16], uint8_t *msg, size_t msg_len);

/**
 * @brief Derive the 16-byte SMB 3.x signing key from the NTLM session key via the SP800-108 counter-mode
 *        KDF (MS-SMB2 §3.1.4.2), dialect-dependent:
 *          - 3.1.1: SigningKey = KDF(SessionKey, "SMBSigningKey\\0", PreauthIntegrityHashValue)
 *          - 3.0 / 3.0.2: SigningKey = KDF(SessionKey, "SMB2AESCMAC\\0", "SmbSign\\0")
 *        The KDF's fixed input is `Label || 0x00 || Context || [L=128]_32be`; the label carries its own
 *        trailing NUL, so the on-wire fixed data has the label followed by two NULs (matches Windows /
 *        Samba / impacket).
 *
 * @param session_key the 16-byte NTLM ExportedSessionKey (SessionBaseKey for NTLMv2 with no key exch).
 * @param dialect     the negotiated DialectRevision (only 3.1.1 vs pre-3.1.1 matters here).
 * @param preauth     the 64-byte final preauth-integrity hash; required iff @p dialect == 3.1.1, else ignored.
 * @param out_key     receives the 16-byte signing key.
 * @return true on success; false on a null pointer, or a 3.1.1 request with a null @p preauth.
 */
proto_bool pc_smb3_derive_signing_key(const uint8_t session_key[16], uint16_t dialect, const uint8_t *preauth,
                                      uint8_t out_key[16]);

// ---------------------------------------------------------------------------
// SMB 3.x transport encryption - AEAD-wrapped messages (MS-SMB2 §2.2.41 TRANSFORM_HEADER, §3.1.4.3/§3.1.4.4).
// All four SMB 3.1.1 ciphers are supported: AES-128-GCM / AES-256-GCM (crypto/aes128gcm, crypto/aesgcm) and
// AES-128-CCM / AES-256-CCM (crypto/aesccm). The negotiated cipher (Connection.CipherId) selects the key
// length (16 / 32) and AEAD nonce length (12 GCM / 11 CCM); the codec dispatches on the cipher id.
// ---------------------------------------------------------------------------

/** @brief TRANSFORM_HEADER size: ProtocolId(4)+Signature(16)+Nonce(16)+OriginalMessageSize(4)+Reserved(2)+
 *         Flags(2)+SessionId(8) = 52 bytes (MS-SMB2 §2.2.41). */
#define PC_SMB2_TRANSFORM_HDR_LEN 52
/** @brief TRANSFORM_HEADER ProtocolId 0xFD 'S' 'M' 'B' as a little-endian u32. */
#define PC_SMB2_TRANSFORM_PROTOCOL_ID 0x424D53FDu
/** @brief The TRANSFORM_HEADER Nonce field width (MS-SMB2 §2.2.41). The AEAD uses the leading
 *         pc_smb2_cipher_nonce_len() bytes; the rest are zero. */
#define PC_SMB2_NONCE_FIELD_LEN 16
/** @brief AES-GCM nonce length used within the 16-byte Nonce field. */
#define PC_SMB2_GCM_NONCE_LEN 12
/** @brief AES-CCM nonce length used within the 16-byte Nonce field (MS-SMB2 §3.1.4.3). */
#define PC_SMB2_CCM_NONCE_LEN 11
/** @brief Largest cipher key length across the four SMB 3.1.1 ciphers (AES-256), for buffer sizing. */
#define PC_SMB2_MAX_CIPHER_KEY_LEN 32

/**
 * @brief Derive the two SMB 3.x cipher keys from the NTLM session key (MS-SMB2 §3.1.4.2), via the same
 *        SP800-108 KDF as pc_smb3_derive_signing_key, dialect-dependent:
 *          - 3.1.1:       C2S = KDF(SessionKey, "SMBC2SCipherKey\\0", PreauthHash);
 *                         S2C = KDF(SessionKey, "SMBS2CCipherKey\\0", PreauthHash)
 *          - 3.0 / 3.0.2: C2S = KDF(SessionKey, "SMB2AESCCM\\0", "ServerIn \\0");  (label is AESCCM even for GCM)
 *                         S2C = KDF(SessionKey, "SMB2AESCCM\\0", "ServerOut\\0")
 *        @p key_len (16 for the -128 ciphers, 32 for the -256 ciphers, per pc_smb2_cipher_key_len) sets the
 *        derived key length; it is encoded as [L] in the KDF's fixed input (128 or 256 bits).
 * @param out_c2s client->server key (ENCRYPTS our requests); @param out_s2c server->client key (DECRYPTS
 *        responses). Each receives @p key_len bytes.
 * @return true on success; false on a null pointer, a bad @p key_len, or a 3.1.1 request with a null @p preauth.
 */
proto_bool pc_smb3_derive_encryption_keys(const uint8_t session_key[16], uint16_t dialect, const uint8_t *preauth,
                                          size_t key_len, uint8_t *out_c2s, uint8_t *out_s2c);

/**
 * @brief Encrypt one SMB2 message into a TRANSFORM_HEADER-wrapped blob (MS-SMB2 §3.1.4.3): a 52-byte header
 *        (ProtocolId, the AEAD tag in Signature, @p nonce, OriginalMessageSize, Flags=Encrypted, @p
 *        session_id) followed by the ciphertext. AAD is the header from the Nonce field to its end. The
 *        codec dispatches on @p cipher (AES-128/256-GCM or AES-128/256-CCM).
 * @param cipher one of Smb2Cipher; selects the key length and AEAD nonce length.
 * @param key cipher key (pc_smb2_cipher_key_len(@p cipher) bytes, i.e. the C2S key).
 * @param nonce the 16-byte Nonce field; the leading nonce-length bytes must be UNIQUE per key (caller
 *        advances a counter), the rest zero.
 * @param session_id echoed into the header; @param out needs >= PC_SMB2_TRANSFORM_HDR_LEN + @p msg_len.
 * @return total encrypted length (52 + msg_len), or 0 on a bad cipher / null pointer / insufficient @p out_cap.
 */
size_t pc_smb2_encrypt(uint16_t cipher, const uint8_t *key, const uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN],
                       uint64_t session_id, const uint8_t *msg, size_t msg_len, uint8_t *out, size_t out_cap);

/**
 * @brief Decrypt a TRANSFORM_HEADER-wrapped SMB2 message (MS-SMB2 §3.1.4.4): validates the ProtocolId and
 *        verifies the AEAD tag (constant time) before exposing any plaintext, dispatching on @p cipher.
 * @param cipher one of Smb2Cipher; @param key the S2C cipher key; @param out needs >= OriginalMessageSize.
 * @return the plaintext length, or 0 on a short/invalid header, tag mismatch, or insufficient @p out_cap.
 */
size_t pc_smb2_decrypt(uint16_t cipher, const uint8_t *key, const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap);

#endif // PC_ENABLE_SMB

PROTO_END_DECLS

#endif // PROTOCORE_SMB2_H
