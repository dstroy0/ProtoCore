# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# RSASSA-PSS signatures over the same key and messages openssl_rsa_2048_sign.json already carries,
# signed by openssl rather than by anything in this tree. PSS draws a random salt, so a signature
# cannot be recomputed and compared - a verifier is the only thing that can check one, which is
# exactly why the signer has to be an implementation we did not write.
#
# The key is read out of the committed PKCS#1 v1.5 vectors, so this needs no network and no
# checkout: same modulus, same messages, different padding.
#
#   python tools/harness.py crypto pss
import base64
import json
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "test", "vectors", "openssl_rsa_2048_sign.json")
OUT = os.path.join(ROOT, "test", "vectors", "openssl_rsa_2048_pss.json")


def _der_len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b


def _der_int(v):
    b = v.to_bytes((v.bit_length() + 8) // 8, "big")
    return b"\x02" + _der_len(len(b)) + b


def _rsa_private_pem(n, e, d, p, q):
    """PKCS#1 RSAPrivateKey (RFC 8017 A.1.2) as PEM, built without a crypto library."""
    body = b"".join(_der_int(v) for v in (0, n, e, d, p, q, d % (p - 1), d % (q - 1), pow(q, -1, p)))
    der = b"\x30" + _der_len(len(body)) + body
    b64 = base64.b64encode(der).decode("ascii")
    rows = "\n".join(b64[i : i + 64] for i in range(0, len(b64), 64))
    return "-----BEGIN RSA PRIVATE KEY-----\n%s\n-----END RSA PRIVATE KEY-----\n" % rows


def main():
    with open(SRC, encoding="utf-8") as f:
        src = json.load(f)
    rows = [v for v in src["vectors"] if v["hash"] == "SHA-256"]
    if not rows:
        raise SystemExit("no SHA-256 rows in %s to take the key and messages from" % SRC)

    k = rows[0]
    n, e, d = int(k["n"], 16), int(k["e"], 16), int(k["d"], 16)
    p, q = int(k["p"], 16), int(k["q"], 16)

    ver = subprocess.run(["openssl", "version"], capture_output=True, text=True).stdout.strip()
    tmp = tempfile.mkdtemp(prefix="rsa_pss_")
    key = os.path.join(tmp, "key.pem")
    with open(key, "w", encoding="ascii", newline="\n") as f:
        f.write(_rsa_private_pem(n, e, d, p, q))

    out, tc = [], 1
    for row in rows:
        mf = os.path.join(tmp, "msg.bin")
        sf = os.path.join(tmp, "sig.bin")
        with open(mf, "wb") as f:
            f.write(bytes.fromhex(row["msg"]))
        # rsa_pss_saltlen:32 is the salt length rsa_pss_rsae_sha256 fixes (RFC 8446 sec 4.2.3
        # names the scheme; RFC 8017 sec 9.1 makes the salt a parameter, and TLS pins it to hLen).
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sigopt",
                "rsa_padding_mode:pss",
                "-sigopt",
                "rsa_pss_saltlen:32",
                "-sign",
                key,
                "-out",
                sf,
                mf,
            ],
            check=True,
        )
        with open(sf, "rb") as f:
            sig = f.read()
        out.append(
            {
                "tcId": tc,
                "comment": "PSS SHA-256 over %s" % row["comment"].split(" over ", 1)[-1],
                "result": "valid",
                "flags": [],
                "n": k["n"],
                "e": k["e"],
                "msg": row["msg"],
                "sig": sig.hex(),
            }
        )
        tc += 1

    # One row the verifier must REFUSE: a good signature with one flipped octet. PSS rejects it in
    # the data block, not by a compare, so it exercises the recovery path rather than an equality.
    bad = bytearray(bytes.fromhex(out[0]["sig"]))
    bad[0] ^= 0x01
    out.append(
        {
            "tcId": tc,
            "comment": "a flipped octet in the signature",
            "result": "invalid",
            "flags": [],
            "n": k["n"],
            "e": k["e"],
            "msg": out[0]["msg"],
            "sig": bytes(bad).hex(),
        }
    )

    doc = {
        "source": "openssl dgst -sigopt rsa_padding_mode:pss (%s)" % ver,
        "commit": "",
        "file": "openssl",
        "vectors": out,
    }
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=False)
    print("wrote %s (%d vectors)" % (OUT, len(out)))


if __name__ == "__main__":
    main()
