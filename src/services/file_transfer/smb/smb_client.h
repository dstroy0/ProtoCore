// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb_client.h
 * @brief SMB2 client dialogue engine (PC_ENABLE_SMB) - drives the smb2 / ntlm / spnego wire
 *        codecs through a real session to open a file on a Windows share.
 *
 * The wire codecs (smb2.h, ntlm.h, ntlmssp.h, spnego.h) are pure builders/parsers; this ties them
 * into the actual exchange: NEGOTIATE, the two-round NTLMv2 SESSION_SETUP (SPNEGO-wrapped),
 * TREE_CONNECT to `\\server\share`, and CREATE to open the file - handing back a handle that
 * smb_read / smb_write / smb_close use. Like the SMTP engine it is written against a send/recv seam,
 * so the whole exchange is host-tested with a scripted mock SMB2 server (no lwIP / real share).
 *
 * Direct-TCP framing (the 4-byte length prefix) is handled here: each request is framed before
 * `send`, each response is de-framed after `recv` (accumulating until a full message arrives).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMB_CLIENT_H
#define PROTOCORE_SMB_CLIENT_H

#include "protocore_config.h"

#if PC_ENABLE_SMB

#include "smb2.h" // Smb2SignAlgo (the per-session signing algorithm carried on the handle)

/** @brief Result of an SMB client operation. 0 is success; each failure is a distinct code. */
typedef enum PROTO_ENUM_PACKED
{
    SMB_OK = 0,
    SMB_ERR_ARG = -1,      ///< a required field was null/empty
    SMB_ERR_IO = -2,       ///< a send/recv failed, timed out, or the peer closed mid-message
    SMB_ERR_PROTOCOL = -3, ///< a malformed response, or an unexpected NT status
    SMB_ERR_AUTH = -4,     ///< SESSION_SETUP was rejected (bad user/password/domain)
    SMB_ERR_OVERFLOW = -5, ///< a message did not fit the work buffer (PC_SMB_BUF)
} SmbResult;

/**
 * @brief Transport seam: the engine moves raw bytes only through these, so it runs against a real
 *        socket (pc_client) or a test mock.
 * @return send: bytes written (must equal @p len), else < 0. recv: bytes read (> 0), else <= 0 on
 *         close / error / timeout.
 */
using SmbSendFn = int (*)(void *ctx, const uint8_t *data, size_t len);
using SmbRecvFn = int (*)(void *ctx, uint8_t *buf, size_t cap);

/** @brief Server credentials + the file to open. Strings are ASCII/UTF-8 (encoded UTF-16LE for you). */
typedef struct
{
    const char *user;        ///< account name
    const char *pass;        ///< password
    const char *domain;      ///< NTLM domain (null/empty for a local account)
    const char *workstation; ///< client name to announce (null => none)
    const char *share;       ///< the tree path, UNC `\\server\share`
    const char *path;        ///< file name relative to the share root (e.g. `PROGRAMS\A.NC`)
    uint32_t desired_access; ///< Smb2Access::SMB2_FILE_GENERIC_READ and/or _WRITE
    uint32_t disposition;    ///< Smb2Disposition::SMB2_FILE_OPEN / _OPEN_IF / _OVERWRITE_IF / _CREATE
    proto_bool encrypt;      ///< request SMB 3.x transport encryption from the session on (client-forced, like
                             ///< smbclient -e): needed to reach a share whose server requires encryption, which
                             ///< rejects the unencrypted TREE_CONNECT before it can advertise the share flag.
    uint16_t cipher_pref;    ///< preferred Smb2Cipher to negotiate (moved to the front of the offer); 0 = default
                             ///< order (AES-128-GCM, AES-256-GCM, AES-128-CCM, AES-256-CCM).
} SmbConfig;

/** @brief An open file on an authenticated session; the ids thread the follow-up requests. */
typedef struct
{
    uint64_t session_id;
    uint32_t tree_id;
    uint8_t file_id[16];
    uint64_t file_size;        ///< EndofFile from CREATE (the current size)
    uint64_t next_message_id;  ///< the MessageId for the next request on this handle
    proto_bool signing_active; ///< the session negotiated SMB signing (server set SigningRequired, not guest/null)
    Smb2SignAlgo signing_algo; ///< HMAC-SHA256 (SMB 2.x) or AES-CMAC (SMB 3.x), from the negotiated dialect
    uint8_t signing_key[16];   ///< the session signing key when @ref signing_active (2.x: NTLMv2 key; 3.x: KDF-derived)
    proto_bool encrypt_active; ///< SMB 3.x transport encryption is in force (server session or share required it)
    uint16_t enc_cipher;       ///< negotiated Smb2Cipher id (selects the key + nonce length) when @ref encrypt_active
    uint8_t enc_c2s[PC_SMB2_MAX_CIPHER_KEY_LEN]; ///< client->server cipher key (encrypts requests)
    uint8_t enc_s2c[PC_SMB2_MAX_CIPHER_KEY_LEN]; ///< server->client cipher key (decrypts responses)
    uint64_t enc_nonce; ///< monotonic per-session AEAD nonce counter, persisted across read/write/close
} SmbHandle;

/**
 * @brief Run NEGOTIATE -> NTLMv2 SESSION_SETUP -> TREE_CONNECT -> CREATE and fill @p h.
 * @return SMB_OK with @p h populated, or an ::SmbResult error.
 */
SmbResult smb_open(const SmbConfig *cfg, SmbHandle *h, SmbSendFn send, SmbRecvFn recv, void *ctx);

/**
 * @brief CLOSE the open handle (releases the server-side FileId).
 * @return SMB_OK, or an ::SmbResult error.
 */
SmbResult smb_close(SmbHandle *h, SmbSendFn send, SmbRecvFn recv, void *ctx);

/**
 * @brief Read up to @p cap bytes from @p offset of the open handle, looping READ requests until the
 *        buffer is full or the server signals end of file.
 * @param out_len receives the number of bytes actually read (may be < @p cap at EOF).
 * @return SMB_OK, or an ::SmbResult error. Reads at most PC_SMB_BUF-sized chunks per round trip.
 */
SmbResult smb_read(SmbHandle *h, uint64_t offset, uint8_t *out, size_t cap, size_t *out_len, SmbSendFn send,
                   SmbRecvFn recv, void *ctx);

/**
 * @brief Write @p len bytes at @p offset of the open handle, looping WRITE requests until all bytes
 *        are acknowledged. Grows the handle's cached file_size if the write extends the file.
 * @param written receives the number of bytes written (equals @p len on success).
 * @return SMB_OK, or an ::SmbResult error.
 */
SmbResult smb_write(SmbHandle *h, uint64_t offset, const uint8_t *data, size_t len, size_t *written, SmbSendFn send,
                    SmbRecvFn recv, void *ctx);

#endif // PC_ENABLE_SMB

#endif // PROTOCORE_SMB_CLIENT_H
