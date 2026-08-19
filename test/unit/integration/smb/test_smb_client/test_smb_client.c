// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/hash/md/md.h"
#include "network_drivers/application/smb/ntlm/ntlm.h"
#include "network_drivers/application/smb/ntlmssp/ntlmssp.h"
#include "network_drivers/application/smb/smb2/smb2.h"
#include "network_drivers/application/smb/smb_client/smb_client.h"
#include "network_drivers/application/smb/spnego/spnego.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t ntlm_work[16]; // the borrow an entry takes; Ntlm never reads it

static uint8_t spnego_work[16]; // the borrow an entry takes; Spnego never reads it

static uint8_t tw[4096];

void setUp()
{
}
void tearDown()
{
}

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}
static void w64(uint8_t *p, uint64_t v)
{
    w32(p, (uint32_t)v);
    w32(p + 4, (uint32_t)(v >> 32));
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static size_t protocore_ntlmssp_challenge(uint8_t *m, const uint8_t sc[8])
{
    memset(m, 0, 64);
    const uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
    memcpy(m, sig, 8);
    w32(m + 8, 2);
    w32(m + 20, NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8);

    uint8_t *ti = m + 48;
    w16(ti + 0, 7);
    w16(ti + 2, 8);
    w64(ti + 4, 0x01D000000000ULL);
    w16(ti + 12, 0);
    w16(ti + 14, 0);
    uint16_t ti_len = 16;
    w16(m + 40, ti_len);
    w16(m + 42, ti_len);
    w32(m + 44, 48);
    return 48 + ti_len;
}

static size_t protocore_ntlmssp_challenge_ti(uint8_t *m, const uint8_t sc[8], const uint8_t *ti, size_t ti_len)
{
    memset(m, 0, 48);
    const uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
    memcpy(m, sig, 8);
    w32(m + 8, 2);
    w32(m + 20, NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8);
    memcpy(m + 48, ti, ti_len);
    w16(m + 40, (uint16_t)ti_len);
    w16(m + 42, (uint16_t)ti_len);
    w32(m + 44, 48);
    return 48 + ti_len;
}

enum MockFault
{
    FAULT_NONE = 0,
    FAULT_DROP,
    FAULT_BAD_HEADER,
    FAULT_BAD_BODY,
};

enum SsSecBufMode
{
    SSBUF_NORMAL = 0,
    SSBUF_EMPTY,
    SSBUF_RAW_JUNK,
    SSBUF_SPNEGO_JUNK
};

typedef struct
{
    uint8_t rx[8192];
    size_t rx_len, rx_pos;
    int ss_round;
    uint64_t session_id;
    uint32_t tree_id;
    uint8_t file_id[16];
    uint64_t file_size;
    uint32_t auth_status;
    uint32_t tc_status;
    uint32_t create_status;
    proto_bool cut_after_negotiate;
    int req_count;
    uint8_t file_data[8192];
    size_t file_data_len;
    uint32_t ss1_status;
    int fault_at_req;
    int fault_kind;
    int ss1_secbuf_mode;
    const uint8_t *chal_ti;
    size_t chal_ti_len;

    proto_bool require_signing;
    proto_bool signing;
    uint8_t sign_key[16];
    int bad_req_sigs;
    proto_bool corrupt_read_sig;
    const SmbConfig *creds;

    proto_bool require_311;
    Smb2SignAlgo sign_algo;
    SmbPreauth preauth;

    proto_bool require_encrypt;

    proto_bool encrypt_share_only;
    uint16_t cipher;
    proto_bool enc_keys;
    uint8_t enc_c2s[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t enc_s2c[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint64_t enc_nonce;
} Mock;

static void append_frame(Mock *m, const uint8_t *resp, size_t rlen)
{
    m->rx_len += protocore_smb2_transport_frame(m->rx + m->rx_len, sizeof(m->rx) - m->rx_len, resp, rlen);
}

static proto_bool mock_derive_key(const uint8_t *msg, size_t mlen, const SmbConfig *cfg, uint8_t key[16])
{
    if (mlen < 88)
    {
        return PROTO_FALSE;
    }
    uint16_t sec_off = rd16(msg + 76);
    uint16_t sec_len = rd16(msg + 78);
    if ((size_t)sec_off + sec_len > mlen)
    {
        return PROTO_FALSE;
    }
    const uint8_t *auth = NULL;
    size_t auth_len = 0;
    Spnego.parse_response_args.blob = msg + sec_off;
    Spnego.parse_response_args.len = sec_len;
    Spnego.parse_response_args.protocore_resp_token = &auth;
    Spnego.parse_response_args.protocore_resp_len = &auth_len;
    Spnego.parse_response(spnego_work);
    if (!Spnego.ok || auth_len < 28)
    {
        return PROTO_FALSE;
    }
    uint16_t nt_len = rd16(auth + 20);
    uint32_t nt_off = rd32(auth + 24);
    if (nt_len < 16 || (size_t)nt_off + 16 > auth_len)
    {
        return PROTO_FALSE;
    }
    uint8_t nt_hash[16];
    uint8_t owf[16];
    Ntlm.nt_hash_args.password = cfg->pass;
    Ntlm.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    Ntlm.ntowfv2_args.nt_hash = nt_hash;
    Ntlm.ntowfv2_args.user = cfg->user;
    Ntlm.ntowfv2_args.domain = cfg->domain ? cfg->domain : "";
    Ntlm.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    if (!Ntlm.ok)
    {
        return PROTO_FALSE;
    }
    Md.hmac_args.key = owf;
    Md.hmac_args.key_len = 16;
    Md.hmac_args.msg = auth + nt_off;
    Md.hmac_args.msg_len = 16;
    Md.hmac_args.out = key;
    Md.hmac_md5(tw);
    return PROTO_TRUE;
}

static void mock_sign(const Mock *m, uint8_t *msg, size_t len)
{
    if (m->sign_algo == SMB2_SIGN_ALGO_AES_CMAC)
    {
        protocore_smb2_sign_cmac(tw, m->sign_key, msg, len);
    }
    else
    {
        protocore_smb2_sign(tw, m->sign_key, msg, len);
    }
}
static proto_bool mock_verify(const Mock *m, uint8_t *msg, size_t len)
{
    return m->sign_algo == SMB2_SIGN_ALGO_AES_CMAC ? protocore_smb2_verify_cmac(tw, m->sign_key, msg, len)
                                                   : protocore_smb2_verify(tw, m->sign_key, msg, len);
}

static size_t build_neg_resp_311(uint8_t *resp, uint64_t msg_id, proto_bool offer_encrypt, uint16_t cipher)
{
    protocore_smb2_build_header(resp, PROTOCORE_SMB_BUF + 128, SMB2_NEGOTIATE, 1, msg_id, 0, 0);
    uint8_t *b = resp + 64;
    memset(b, 0, 64);
    w16(b + 0, 65);
    w16(b + 2, SMB2_NEGOTIATE_SIGNING_REQUIRED);
    w16(b + 4, (uint16_t)SMB2_DIALECT_0311);
    w16(b + 6, offer_encrypt ? 3 : 2);
    w16(b + 56, 0);
    w16(b + 58, 0);
    const uint32_t ctx = 128;
    w32(b + 60, ctx);

    uint8_t *c = resp + ctx;
    w16(c + 0, SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    w16(c + 2, 6 + 32);
    w16(c + 8, 1);
    w16(c + 10, 32);
    w16(c + 12, SMB2_PREAUTH_INTEGRITY_SHA512);
    for (int i = 0; i < 32; i++)
    {
        c[14 + i] = (uint8_t)(0x40 + i);
    }

    uint8_t *c2 = c + 48;
    w16(c2 + 0, SMB2_SIGNING_CAPABILITIES);
    w16(c2 + 2, 4);
    w16(c2 + 8, 1);
    w16(c2 + 10, SMB2_SIGNING_AES_CMAC);
    if (!offer_encrypt)
    {
        return ctx + 48 + 12;
    }

    uint8_t *c3 = c2 + 16;
    w16(c3 + 0, SMB2_ENCRYPTION_CAPABILITIES);
    w16(c3 + 2, 4);
    w16(c3 + 8, 1);
    w16(c3 + 10, cipher);
    return ctx + 48 + 16 + 12;
}

static int mock_send(void *c, const uint8_t *d, size_t n)
{
    Mock *m = (Mock *)c;
    m->req_count++;
    const uint8_t *msg = d + 4;
    size_t mlen = n - 4;

    uint8_t plain[PROTOCORE_SMB_BUF];
    proto_bool req_enc = PROTO_FALSE;
    if (m->enc_keys && mlen >= PROTOCORE_SMB2_TRANSFORM_HDR_LEN && msg[0] == 0xFD && msg[1] == 'S' && msg[2] == 'M' &&
        msg[3] == 'B')
    {
        size_t pl = protocore_smb2_decrypt(m->cipher, m->enc_c2s, msg, mlen, plain, sizeof(plain));
        if (pl == 0)
        {
            return -1;
        }
        msg = plain;
        mlen = pl;
        req_enc = PROTO_TRUE;
    }
    Smb2Header h;
    if (!protocore_smb2_parse_header(msg, mlen, &h))
    {
        return -1;
    }

    if (m->require_311)
    {
        if (h.command == SMB2_NEGOTIATE)
        {
            protocore_smb_preauth_init(&m->preauth);
        }
        if (h.command == SMB2_NEGOTIATE || h.command == SMB2_SESSION_SETUP)
        {
            protocore_smb_preauth_update(tw, &m->preauth, msg, mlen);
        }
    }

    if (m->signing && !req_enc)
    {
        uint8_t vbuf[PROTOCORE_SMB_BUF];
        if (mlen <= sizeof(vbuf))
        {
            memcpy(vbuf, msg, mlen);
            if (!(vbuf[16] & SMB2_FLAGS_SIGNED) || !mock_verify(m, vbuf, mlen))
            {
                m->bad_req_sigs++;
            }
        }
    }

    uint8_t resp[PROTOCORE_SMB_BUF + 128];
    memset(resp, 0, sizeof(resp));
    size_t rlen = 0;
    uint8_t *b = resp + 64;
    switch (h.command)
    {
    case SMB2_NEGOTIATE:
        if (m->require_311)
        {
            if (m->cipher == 0)
            {
                m->cipher = SMB2_ENCRYPTION_AES128_GCM;
            }
            rlen = build_neg_resp_311(resp, h.message_id, m->require_encrypt || m->encrypt_share_only, m->cipher);
            break;
        }
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_NEGOTIATE, 1, h.message_id, 0, 0);
        w16(b + 0, 65);
        if (m->require_signing)
        {
            w16(b + 2, SMB2_NEGOTIATE_SIGNING_REQUIRED);
        }
        w16(b + 4, (uint16_t)SMB2_DIALECT_0210);
        rlen = 128;
        break;
    case SMB2_SESSION_SETUP: {
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_SESSION_SETUP, 1, h.message_id, 0, m->session_id);
        w16(b + 0, 9);
        if (m->ss_round++ == 0)
        {
            w32(resp + 8, m->ss1_status);
            if (m->ss1_secbuf_mode == SSBUF_EMPTY)
            {
                w16(b + 4, 0);
                w16(b + 6, 0);
                rlen = 72;
            }
            else
            {
                uint8_t chal[1024];
                uint8_t sctok[1200];
                const uint8_t sc[8] = {1, 2, 3, 4, 5, 6, 7, 8};
                size_t sc_n = 0;
                if (m->ss1_secbuf_mode == SSBUF_RAW_JUNK)
                {
                    memset(sctok, 0x77, 16);
                    sc_n = 16;
                }
                else if (m->ss1_secbuf_mode == SSBUF_SPNEGO_JUNK)
                {
                    uint8_t junk[16];
                    memset(junk, 0x55, sizeof(junk));
                    Spnego.wrap_authenticate_args.ntlm = junk;
                    Spnego.wrap_authenticate_args.protocore_ntlm_len = sizeof(junk);
                    Spnego.wrap_authenticate_args.out = sctok;
                    Spnego.wrap_authenticate_args.cap = sizeof(sctok);
                    Spnego.wrap_authenticate(spnego_work);
                    sc_n = Spnego.n;
                }
                else
                {
                    size_t chal_n = m->chal_ti ? protocore_ntlmssp_challenge_ti(chal, sc, m->chal_ti, m->chal_ti_len)
                                               : protocore_ntlmssp_challenge(chal, sc);
                    Spnego.wrap_authenticate_args.ntlm = chal;
                    Spnego.wrap_authenticate_args.protocore_ntlm_len = chal_n;
                    Spnego.wrap_authenticate_args.out = sctok;
                    Spnego.wrap_authenticate_args.cap = sizeof(sctok);
                    Spnego.wrap_authenticate(spnego_work);
                    sc_n = Spnego.n;
                }
                w16(b + 4, 72);
                w16(b + 6, (uint16_t)sc_n);
                memcpy(resp + 72, sctok, sc_n);
                rlen = 72 + sc_n;
            }
        }
        else
        {

            uint8_t base_key[16];
            if (m->creds && mock_derive_key(msg, mlen, m->creds, base_key))
            {
                if (m->require_311)
                {
                    protocore_smb3_derive_signing_key(base_key, (uint16_t)SMB2_DIALECT_0311, m->preauth.hash,
                                                      m->sign_key);
                    m->sign_algo = SMB2_SIGN_ALGO_AES_CMAC;
                    m->signing = PROTO_TRUE;
                    if (m->require_encrypt || m->encrypt_share_only)
                    {

                        protocore_smb3_derive_encryption_keys(base_key, (uint16_t)SMB2_DIALECT_0311, m->preauth.hash,
                                                              protocore_smb2_cipher_key_len(m->cipher), m->enc_c2s,
                                                              m->enc_s2c);
                        m->enc_keys = PROTO_TRUE;
                        if (m->require_encrypt)
                        {
                            w16(b + 2, SMB2_SESSION_FLAG_ENCRYPT_DATA);
                        }
                    }
                }
                else if (m->require_signing)
                {
                    memcpy(m->sign_key, base_key, 16);
                    m->signing = PROTO_TRUE;
                }
            }
            w32(resp + 8, m->auth_status);
            rlen = 72;
        }
        break;
    }
    case SMB2_TREE_CONNECT:
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_TREE_CONNECT, 1, h.message_id, m->tree_id, m->session_id);

        if (m->encrypt_share_only && !req_enc)
        {
            w32(resp + 8, 0xC0000022u);
        }
        else
        {
            w32(resp + 8, m->tc_status);
        }
        w16(b + 0, 16);
        b[2] = SMB2_SHARE_TYPE_DISK;
        rlen = 64 + 16;
        break;
    case SMB2_CREATE:
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_CREATE, 1, h.message_id, m->tree_id, m->session_id);
        w32(resp + 8, m->create_status);
        w16(b + 0, 89);
        w32(b + 4, 1);
        w64(b + 48, m->file_size);
        memcpy(b + 64, m->file_id, 16);
        rlen = 64 + 88;
        break;
    case SMB2_READ: {
        const uint8_t *rq = msg + 64;
        uint32_t length = rd32(rq + 4);
        uint64_t off = rd64(rq + 8);
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_READ, 1, h.message_id, m->tree_id, m->session_id);
        if (off >= m->file_data_len)
        {
            w32(resp + 8, SMB2_STATUS_END_OF_FILE);
            w16(b + 0, 17);
            rlen = 64 + 16;
        }
        else
        {
            uint32_t avail = (uint32_t)(m->file_data_len - off);
            uint32_t n2 = length < avail ? length : avail;
            w16(b + 0, 17);
            b[2] = 80;
            w32(b + 4, n2);
            memcpy(resp + 80, m->file_data + off, n2);
            rlen = 80 + n2;
        }
        break;
    }
    case SMB2_WRITE: {
        const uint8_t *wq = msg + 64;
        uint16_t data_off = rd16(wq + 2);
        uint32_t length = rd32(wq + 4);
        uint64_t off = rd64(wq + 8);
        if (off + length <= sizeof(m->file_data))
        {
            memcpy(m->file_data + off, msg + data_off, length);
            if (off + length > m->file_data_len)
            {
                m->file_data_len = (size_t)(off + length);
            }
        }
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_WRITE, 1, h.message_id, m->tree_id, m->session_id);
        w16(b + 0, 17);
        w32(b + 4, length);
        rlen = 64 + 16;
        break;
    }
    case SMB2_CLOSE:
        protocore_smb2_build_header(resp, sizeof(resp), SMB2_CLOSE, 1, h.message_id, m->tree_id, m->session_id);
        w16(b + 0, 60);
        rlen = 64 + 60;
        break;
    default:
        return -1;
    }
    resp[16] |= 0x01;
    proto_bool drop = m->cut_after_negotiate && h.command != SMB2_NEGOTIATE;
    if (m->fault_at_req == m->req_count)
    {
        if (m->fault_kind == FAULT_DROP)
        {
            drop = PROTO_TRUE;
        }
        else if (m->fault_kind == FAULT_BAD_HEADER)
        {
            resp[0] = 0x00;
        }
        else if (m->fault_kind == FAULT_BAD_BODY)
        {
            w16(resp + 64, 0xFFFF);
        }
    }

    if (m->require_311 && (h.command == SMB2_NEGOTIATE || (h.command == SMB2_SESSION_SETUP && m->ss_round == 1)))
    {
        protocore_smb_preauth_update(tw, &m->preauth, resp, rlen);
    }

    if (m->signing && !req_enc)
    {
        mock_sign(m, resp, rlen);
        if (m->corrupt_read_sig && h.command == SMB2_READ)
        {
            resp[48] ^= 0xFF;
        }
    }
    if (!drop)
    {
        if (req_enc)
        {

            uint8_t enc[PROTOCORE_SMB_BUF + 128];
            uint8_t nonce[PROTOCORE_SMB2_NONCE_FIELD_LEN] = {0};
            uint64_t ctr = m->enc_nonce++;
            for (int i = 0; i < 8; i++)
            {
                nonce[i] = (uint8_t)(ctr >> (8 * i));
            }
            size_t el =
                protocore_smb2_encrypt(m->cipher, m->enc_s2c, nonce, m->session_id, resp, rlen, enc, sizeof(enc));
            if (m->corrupt_read_sig && h.command == SMB2_READ)
            {
                enc[PROTOCORE_SMB2_TRANSFORM_HDR_LEN + 2] ^= 0xFF;
            }
            append_frame(m, enc, el);
        }
        else
        {
            append_frame(m, resp, rlen);
        }
    }
    return (int)n;
}

static int mock_recv(void *c, uint8_t *buf, size_t cap)
{
    Mock *m = (Mock *)c;
    if (m->rx_pos >= m->rx_len)
    {
        return 0;
    }
    size_t avail = m->rx_len - m->rx_pos;
    size_t take = avail < cap ? avail : cap;
    memcpy(buf, m->rx + m->rx_pos, take);
    m->rx_pos += take;
    return (int)take;
}

static Mock make_mock()
{
    Mock m;
    memset(&m, 0, sizeof(m));
    m.session_id = 0x1122334455667788ULL;
    m.tree_id = 0x00A1;
    for (int i = 0; i < 16; i++)
    {
        m.file_id[i] = (uint8_t)(0xE0 + i);
    }
    m.file_size = 4096;
    m.auth_status = SMB2_STATUS_SUCCESS;
    m.tc_status = SMB2_STATUS_SUCCESS;
    m.create_status = SMB2_STATUS_SUCCESS;
    m.ss1_status = SMB2_STATUS_MORE_PROCESSING_REQUIRED;
    return m;
}

static SmbConfig make_cfg()
{
    SmbConfig c;
    memset(&c, 0, sizeof(c));
    c.user = "operator";
    c.pass = "secretpassword";
    c.domain = "SHOP";
    c.workstation = "ESP32";
    c.share = "\\\\nc01\\programs";
    c.path = "A.NC";
    c.desired_access = SMB2_FILE_GENERIC_READ;
    c.disposition = SMB2_FILE_OPEN;
    return c;
}

void test_open_close_success()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));

    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_HEX64(m.session_id, h.session_id);
    TEST_ASSERT_EQUAL_HEX32(m.tree_id, h.tree_id);
    TEST_ASSERT_EQUAL_MEMORY(m.file_id, h.file_id, 16);
    TEST_ASSERT_EQUAL_HEX64(4096, h.file_size);
    TEST_ASSERT_EQUAL_UINT64(5, h.next_message_id);

    TEST_ASSERT_EQUAL_INT(5, m.req_count);

    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT64(6, h.next_message_id);
    TEST_ASSERT_EQUAL_INT(6, m.req_count);
}

void test_auth_failure()
{
    Mock m = make_mock();
    m.auth_status = 0xC000006D;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_bad_share()
{
    Mock m = make_mock();
    m.tc_status = 0xC00000CC;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_create_not_found()
{
    Mock m = make_mock();
    m.create_status = 0xC0000034;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_io_error()
{
    Mock m = make_mock();
    m.cut_after_negotiate = PROTO_TRUE;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_arg_validation()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    cfg.user = NULL;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    cfg = make_cfg();
    cfg.path = NULL;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

static SmbResult open_ok(Mock *m, SmbConfig *cfg, SmbHandle *h)
{
    memset(h, 0, sizeof(*h));
    return smb_open(cfg, h, mock_send, mock_recv, m);
}

void test_read_file()
{
    Mock m = make_mock();
    for (int i = 0; i < 2000; i++)
    {
        m.file_data[i] = (uint8_t)(i * 31 + 7);
    }
    m.file_data_len = 2000;
    m.file_size = 2000;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t buf[2048];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, 2000, &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(2000, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 2000);
}

void test_read_past_eof()
{
    Mock m = make_mock();
    for (int i = 0; i < 100; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 100;
    m.file_size = 100;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t buf[512];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(100, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 100);
}

void test_write_file()
{
    Mock m = make_mock();
    m.file_data_len = 0;
    m.file_size = 0;
    SmbConfig cfg = make_cfg();
    cfg.desired_access = SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = SMB2_FILE_OVERWRITE_IF;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t data[2000];
    for (int i = 0; i < 2000; i++)
    {
        data[i] = (uint8_t)(i * 13 + 3);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, data, sizeof(data), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(2000, wrote);
    TEST_ASSERT_EQUAL_size_t(2000, m.file_data_len);
    TEST_ASSERT_EQUAL_MEMORY(data, m.file_data, 2000);
    TEST_ASSERT_EQUAL_HEX64(2000, h.file_size);
}

void test_write_then_read_roundtrip()
{
    Mock m = make_mock();
    m.file_data_len = 0;
    m.file_size = 0;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t data[1500];
    for (int i = 0; i < 1500; i++)
    {
        data[i] = (uint8_t)(i ^ 0x5A);
    }
    size_t wrote = 0, got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, data, sizeof(data), &wrote, mock_send, mock_recv, &m));
    uint8_t back[1500];
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, back, sizeof(back), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1500, got);
    TEST_ASSERT_EQUAL_MEMORY(data, back, 1500);
}

void test_negotiate_malformed()
{
    Mock m = make_mock();
    m.fault_at_req = 1;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_negotiate_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 1;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 2;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_wrong_status()
{
    Mock m = make_mock();
    m.ss1_status = SMB2_STATUS_SUCCESS;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 2;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_no_secbuf()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_EMPTY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_bad_spnego()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_RAW_JUNK;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session1_bad_ntlmssp()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_SPNEGO_JUNK;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session2_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 3;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_session2_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 3;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_tree_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_tree_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_create_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_create_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_long_share_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char share[300];
    memset(share, 'S', sizeof(share) - 1);
    share[sizeof(share) - 1] = 0;
    cfg.share = share;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_long_path_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char path[300];
    memset(path, 'P', sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    cfg.path = path;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_long_user_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char user[300];
    memset(user, 'u', sizeof(user) - 1);
    user[sizeof(user) - 1] = 0;
    cfg.user = user;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_challenge_ti_ntlmv2_overflow()
{
    Mock m = make_mock();
    uint8_t ti[500];
    memset(ti, 0, sizeof(ti));
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_challenge_ti_authenticate_overflow()
{
    Mock m = make_mock();
    uint8_t ti[400];
    memset(ti, 0, sizeof(ti));
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_challenge_ti_spnego_overflow()
{
    Mock m = make_mock();
    uint8_t ti[360];
    memset(ti, 0, sizeof(ti));
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_av_eol_only()
{
    Mock m = make_mock();
    const uint8_t ti[4] = {0x00, 0x00, 0x00, 0x00};
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_av_skip_then_find()
{
    Mock m = make_mock();
    const uint8_t ti[] = {
        0x02, 0x00, 0x04, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x07, 0x00, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44,
        0x07, 0x00, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x00,
    };
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_av_truncated_timestamp()
{
    Mock m = make_mock();
    const uint8_t ti[4] = {0x07, 0x00, 0x08, 0x00};
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

typedef struct
{
    uint8_t resp[512];
    size_t protocore_resp_len;
    size_t pos;
    proto_bool short_send;
} Canned;

static int canned_send(void *c, const uint8_t *d, size_t n)
{
    Canned *cn = (Canned *)c;
    (void)d;
    return cn->short_send ? 1 : (int)n;
}

static int canned_recv(void *c, uint8_t *buf, size_t cap)
{
    Canned *cn = (Canned *)c;
    if (cn->pos >= cn->protocore_resp_len)
    {
        return 0;
    }
    size_t avail = cn->protocore_resp_len - cn->pos;
    size_t take = avail < cap ? avail : cap;
    memcpy(buf, cn->resp + cn->pos, take);
    cn->pos += take;
    return (int)take;
}

static uint8_t *protocore_resp_hdr(uint8_t *msg, Smb2Command cmd, uint32_t status)
{
    protocore_smb2_build_header(msg, 64, cmd, 1, 5, 0x00A1, 0x1122334455667788ULL);
    w32(msg + 8, status);
    msg[16] |= 0x01;
    return msg + 64;
}

static void canned_frame(Canned *cn, const uint8_t *msg, size_t mlen)
{
    cn->protocore_resp_len = protocore_smb2_transport_frame(cn->resp, sizeof(cn->resp), msg, mlen);
    cn->pos = 0;
}

static SmbHandle make_handle()
{
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    h.session_id = 0x1122334455667788ULL;
    h.tree_id = 0x00A1;
    for (int i = 0; i < 16; i++)
    {
        h.file_id[i] = (uint8_t)(0xE0 + i);
    }
    h.file_size = 4096;
    h.next_message_id = 5;
    return h;
}

void test_read_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_read(&h, 0, NULL, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_read(&h, 0, buf, sizeof(buf), NULL, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_read(NULL, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = PROTO_TRUE;
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_recv_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    msg[0] = 0x00;
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, 0xC0000022);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, SMB2_STATUS_SUCCESS);
    w16(b + 0, 99);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_data_too_long()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[256] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 100);
    memset(msg + 80, 0xAB, 100);
    canned_frame(&cn, msg, 180);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_zero_data()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(0, got);
}

void test_write_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_write(&h, 0, NULL, sizeof(data), &wrote, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_write(&h, 0, data, sizeof(data), NULL, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_write(NULL, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = PROTO_TRUE;
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_recv_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x20;
    cn.resp[3] = 0x00;
    cn.protocore_resp_len = 4;
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 16);
    msg[0] = 0x00;
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, 0xC0000022);
    w16(b + 0, 17);
    w32(b + 4, 16);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 99);
    w32(b + 4, 16);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_zero_count()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_write_count_too_big()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 999);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_close_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_close(NULL, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_close(&h, NULL, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_close(&h, canned_send, NULL, &cn));
}

void test_close_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = PROTO_TRUE;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0xFF;
    cn.resp[2] = 0xFF;
    cn.resp[3] = 0xFF;
    cn.protocore_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_recv_zero_len()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x00;
    cn.protocore_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_recv_trunc_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x64;
    cn.protocore_resp_len = 4 + 40;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_CLOSE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 60);
    msg[0] = 0x00;
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_CLOSE, 0xC0000022);
    w16(b + 0, 60);
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_close_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_CLOSE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 99);
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_open_arg_remaining_nulls()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(NULL, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, NULL, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, NULL, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, NULL, &m));
    cfg = make_cfg();
    cfg.pass = NULL;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    cfg = make_cfg();
    cfg.share = NULL;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.req_count);
}

void test_open_null_domain()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    cfg.domain = NULL;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_HEX64(m.session_id, h.session_id);
    TEST_ASSERT_EQUAL_HEX32(m.tree_id, h.tree_id);
}

void test_tree_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_create_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_read_write_null_seam()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_read(&h, 0, buf, sizeof(buf), &n, NULL, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_read(&h, 0, buf, sizeof(buf), &n, canned_send, NULL, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_write(&h, 0, buf, sizeof(buf), &n, NULL, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SMB_ERR_ARG, smb_write(&h, 0, buf, sizeof(buf), &n, canned_send, NULL, &cn));
}

void test_read_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x20;
    cn.resp[3] = 0x00;
    cn.protocore_resp_len = 4;
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_OVERFLOW, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

void test_read_eof_status()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_READ, SMB2_STATUS_END_OF_FILE);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(0, got);
    TEST_ASSERT_EQUAL_UINT64(6, h.next_message_id);
}

void test_write_no_extend()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = protocore_resp_hdr(msg, SMB2_WRITE, SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 16);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16];
    memset(data, 0x5A, sizeof(data));
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(16, wrote);
    TEST_ASSERT_EQUAL_HEX64(4096, h.file_size);
}

void test_close_bad_transport_prefix()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x01;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x50;
    cn.protocore_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

void test_signed_session_roundtrip()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_signing = PROTO_TRUE;
    m.creds = &cfg;
    for (int i = 0; i < 1200; i++)
    {
        m.file_data[i] = (uint8_t)(i * 17 + 5);
    }
    m.file_data_len = 1200;
    m.file_size = 1200;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.signing_active);
    TEST_ASSERT_TRUE(m.signing);

    uint8_t buf[1200];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1200, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 1200);

    uint8_t wr[500];
    for (int i = 0; i < 500; i++)
    {
        wr[i] = (uint8_t)(i ^ 0x3C);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, wr, sizeof(wr), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(500, wrote);
    TEST_ASSERT_EQUAL_MEMORY(wr, m.file_data, 500);

    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

void test_signed_response_tampered()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_signing = PROTO_TRUE;
    m.creds = &cfg;
    m.corrupt_read_sig = PROTO_TRUE;
    for (int i = 0; i < 64; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 64;
    m.file_size = 64;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    uint8_t buf[64];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
}

void test_unsigned_session_when_not_required()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_FALSE(h.signing_active);
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

void test_open_signed_311_roundtrip()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_311 = PROTO_TRUE;
    m.creds = &cfg;
    for (int i = 0; i < 1400; i++)
    {
        m.file_data[i] = (uint8_t)(i * 23 + 11);
    }
    m.file_data_len = 1400;
    m.file_size = 1400;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.signing_active);
    TEST_ASSERT_EQUAL_INT(SMB2_SIGN_ALGO_AES_CMAC, h.signing_algo);
    TEST_ASSERT_TRUE(m.signing);
    TEST_ASSERT_EQUAL_INT(SMB2_SIGN_ALGO_AES_CMAC, m.sign_algo);

    uint8_t buf[1400];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1400, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 1400);

    uint8_t wr[700];
    for (int i = 0; i < 700; i++)
    {
        wr[i] = (uint8_t)(i ^ 0x5A);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, wr, sizeof(wr), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(700, wrote);
    TEST_ASSERT_EQUAL_MEMORY(wr, m.file_data, 700);

    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

void test_signed_311_response_tampered()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_311 = PROTO_TRUE;
    m.creds = &cfg;
    m.corrupt_read_sig = PROTO_TRUE;
    for (int i = 0; i < 64; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 64;
    m.file_size = 64;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SMB2_SIGN_ALGO_AES_CMAC, h.signing_algo);
    uint8_t buf[64];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
}

void test_open_encrypted_311_roundtrip()
{
    Mock m = make_mock();
    m.file_size = 300;
    SmbConfig cfg = make_cfg();
    cfg.desired_access = SMB2_FILE_GENERIC_READ | SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = SMB2_FILE_OPEN_IF;
    m.require_311 = PROTO_TRUE;
    m.require_encrypt = PROTO_TRUE;
    m.creds = &cfg;

    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.encrypt_active);
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);

    uint8_t data[250];
    for (int i = 0; i < 250; i++)
    {
        data[i] = (uint8_t)(0xC0 ^ (i * 7));
    }
    size_t wr = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, data, sizeof(data), &wr, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), wr);

    uint8_t buf[256];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(data), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), got);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));

    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

void test_encrypted_response_tampered()
{
    Mock m = make_mock();
    for (int i = 0; i < 100; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 100;
    m.file_size = 100;
    SmbConfig cfg = make_cfg();
    m.require_311 = PROTO_TRUE;
    m.require_encrypt = PROTO_TRUE;
    m.creds = &cfg;
    m.corrupt_read_sig = PROTO_TRUE;

    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.encrypt_active);
    uint8_t buf[128];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, 100, &got, mock_send, mock_recv, &m));
}

void test_open_encrypted_all_ciphers()
{
    const uint16_t ciphers[4] = {SMB2_ENCRYPTION_AES128_GCM, SMB2_ENCRYPTION_AES256_GCM, SMB2_ENCRYPTION_AES128_CCM,
                                 SMB2_ENCRYPTION_AES256_CCM};
    SmbConfig cfg = make_cfg();
    cfg.desired_access = SMB2_FILE_GENERIC_READ | SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = SMB2_FILE_OPEN_IF;
    for (int ci = 0; ci < 4; ci++)
    {
        Mock m = make_mock();
        m.require_311 = PROTO_TRUE;
        m.require_encrypt = PROTO_TRUE;
        m.cipher = ciphers[ci];
        m.creds = &cfg;

        SmbHandle h;
        memset(&h, 0, sizeof(h));
        char cmsg[48];
        snprintf(cmsg, sizeof(cmsg), "cipher 0x%04x", ciphers[ci]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m), cmsg);
        TEST_ASSERT_TRUE(h.encrypt_active);
        TEST_ASSERT_EQUAL_UINT16(ciphers[ci], h.enc_cipher);
        TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);

        uint8_t data[200];
        for (int i = 0; i < 200; i++)
        {
            data[i] = (uint8_t)(0x11 * ci + i * 5);
        }
        size_t wr = 0;
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_write(&h, 0, data, sizeof(data), &wr, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(sizeof(data), wr);
        uint8_t buf[256];
        size_t got = 0;
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, sizeof(data), &got, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(sizeof(data), got);
        TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    }
}

void test_open_encrypted_share_requires_client_force()
{
    SmbConfig cfg = make_cfg();

    {
        Mock m = make_mock();
        m.require_311 = PROTO_TRUE;
        m.encrypt_share_only = PROTO_TRUE;
        m.creds = &cfg;
        SmbHandle h;
        memset(&h, 0, sizeof(h));
        TEST_ASSERT_EQUAL_INT(SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    }

    {
        Mock m = make_mock();
        for (int i = 0; i < 60; i++)
        {
            m.file_data[i] = (uint8_t)(i + 1);
        }
        m.file_data_len = 60;
        m.file_size = 60;
        m.require_311 = PROTO_TRUE;
        m.encrypt_share_only = PROTO_TRUE;
        m.creds = &cfg;

        SmbConfig ecfg = cfg;
        ecfg.encrypt = PROTO_TRUE;
        SmbHandle h;
        memset(&h, 0, sizeof(h));
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_open(&ecfg, &h, mock_send, mock_recv, &m));
        TEST_ASSERT_TRUE(h.encrypt_active);
        uint8_t buf[64];
        size_t got = 0;
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_read(&h, 0, buf, 60, &got, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(60, got);
        TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 60);
        TEST_ASSERT_EQUAL_INT(SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    }
}

