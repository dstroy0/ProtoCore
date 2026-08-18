# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# HKDF-SHA384 and TLS 1.3 HKDF-Expand-Label answers from openssl, for crypto/kdf/hkdf_sha384.h.
#
# RFC 5869 tabulates HKDF only for SHA-256 and SHA-1, and RFC 8448's worked TLS 1.3 trace is
# SHA-256 throughout, so there is no published SHA-384 vector to read. The inputs here are still
# the RFC's - Appendix A's three SHA-256 input triples, re-run at SHA-384 - and the answers come
# from openssl, an implementation outside this tree.
#
# The invocation is calibrated before it is trusted: --check re-runs the SHA-256 forms and compares
# them to the two published values this tree can verify against - RFC 5869 A.1's OKM for the HKDF
# KDF, and the RFC 8448 sec 3 "derived" secret for the TLS13-KDF one. If either disagrees, the CLI
# is not doing what this tool believes and no SHA-384 row is written.
#
#   python tools/harness.py crypto hkdf384
#   python tools/harness.py crypto hkdf384 --check
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "test", "vectors")

# RFC 5869 Appendix A.1 - A.3: the salt / IKM / info triples, verbatim. The RFC runs them at
# SHA-256; the digest below is the only thing that changes.
RFC5869 = [
    {
        "comment": "RFC 5869 A.1 inputs at SHA-384",
        "ikm": "0b" * 22,
        "salt": "000102030405060708090a0b0c",
        "info": "f0f1f2f3f4f5f6f7f8f9",
        "l": 42,
    },
    {
        "comment": "RFC 5869 A.2 inputs at SHA-384 (80-octet salt, IKM and info)",
        "ikm": "".join("%02x" % i for i in range(0x00, 0x50)),
        "salt": "".join("%02x" % i for i in range(0x60, 0xB0)),
        "info": "".join("%02x" % i for i in range(0xB0, 0x100)),
        "l": 82,
    },
    {
        "comment": "RFC 5869 A.3 inputs at SHA-384 (zero-length salt and info)",
        "ikm": "0b" * 22,
        "salt": "",
        "info": "",
        "l": 42,
    },
]

# The labels TLS 1.3 actually derives under (RFC 8446 sec 7.1 and sec 7.3), at the lengths the
# SHA-384 suites use: a 48-octet secret, a 48-octet Transcript-Hash context, and the AES-256-GCM
# key and IV widths for the two record-key labels.
SECRET48 = "".join("%02x" % ((i * 7 + 3) & 0xFF) for i in range(48))
CONTEXT48 = "".join("%02x" % ((i * 11 + 5) & 0xFF) for i in range(48))
LABELS = [
    ("derived", CONTEXT48, 48),
    ("c hs traffic", CONTEXT48, 48),
    ("s hs traffic", CONTEXT48, 48),
    ("c ap traffic", CONTEXT48, 48),
    ("s ap traffic", CONTEXT48, 48),
    ("finished", "", 48),
    ("key", "", 32),
    ("iv", "", 12),
]

# The two published values that calibrate the CLI. RFC 5869 A.1 OKM, and the RFC 8448 sec 3
# early-secret "derived" step, whose inputs are the early secret and Transcript-Hash("").
CAL_HKDF = "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"
CAL_TLS13_SECRET = "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a"
CAL_TLS13_CTX = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
CAL_TLS13 = "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba"


def _kdf(args):
    """Run one openssl kdf and return its output as lowercase hex."""
    out = subprocess.run(["openssl", "kdf"] + args, capture_output=True, text=True, check=True)
    return out.stdout.strip().replace(":", "").lower()


def hkdf(mode, digest, keylen, key, salt="", info=""):
    args = [
        "-keylen",
        str(keylen),
        "-kdfopt",
        "mode:" + mode,
        "-kdfopt",
        "digest:" + digest,
        "-kdfopt",
        "hexkey:" + key,
    ]
    if mode != "EXPAND_ONLY":
        args += ["-kdfopt", "hexsalt:" + salt]
    if mode != "EXTRACT_ONLY":
        args += ["-kdfopt", "hexinfo:" + info]
    return _kdf(args + ["HKDF"])


def expand_label(digest, keylen, secret, label, context):
    return _kdf(
        [
            "-keylen",
            str(keylen),
            "-kdfopt",
            "mode:EXPAND_ONLY",
            "-kdfopt",
            "digest:" + digest,
            "-kdfopt",
            "prefix:tls13 ",
            "-kdfopt",
            "label:" + label,
            "-kdfopt",
            "hexkey:" + secret,
            "-kdfopt",
            "hexdata:" + context,
            "TLS13-KDF",
        ]
    )


def calibrate():
    """Refuse to emit anything unless the CLI reproduces both published SHA-256 answers."""
    got = hkdf("EXTRACT_AND_EXPAND", "SHA256", 42, RFC5869[0]["ikm"], RFC5869[0]["salt"], RFC5869[0]["info"])
    if got != CAL_HKDF:
        raise SystemExit("openssl kdf HKDF disagrees with RFC 5869 A.1:\n  got %s\n  want %s" % (got, CAL_HKDF))
    got = expand_label("SHA256", 32, CAL_TLS13_SECRET, "derived", CAL_TLS13_CTX)
    if got != CAL_TLS13:
        raise SystemExit("openssl TLS13-KDF disagrees with RFC 8448 sec 3:\n  got %s\n  want %s" % (got, CAL_TLS13))
    print("calibrated: RFC 5869 A.1 and RFC 8448 sec 3 both reproduced at SHA-256")


def write(name, rows, note):
    ver = subprocess.run(["openssl", "version"], capture_output=True, text=True).stdout.strip()
    doc = {"source": "openssl kdf (%s) - %s" % (ver, note), "commit": "", "file": "openssl", "vectors": rows}
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=False)
    print("wrote %s (%d vectors)" % (path, len(rows)))


def main():
    calibrate()
    if "--check" in sys.argv[1:]:
        return

    extract, expand, label = [], [], []
    for i, v in enumerate(RFC5869, 1):
        prk = hkdf("EXTRACT_ONLY", "SHA384", 48, v["ikm"], v["salt"])
        extract.append(
            {
                "tcId": i,
                "comment": v["comment"],
                "result": "valid",
                "flags": [],
                "salt": v["salt"],
                "ikm": v["ikm"],
                "prk": prk,
            }
        )
        expand.append(
            {
                "tcId": i,
                "comment": v["comment"],
                "result": "valid",
                "flags": [],
                "prk": prk,
                "info": v["info"],
                "l": v["l"],
                "okm": hkdf("EXPAND_ONLY", "SHA384", v["l"], prk, info=v["info"]),
            }
        )

    for i, (name, ctx, l) in enumerate(LABELS, 1):
        label.append(
            {
                "tcId": i,
                "comment": 'HKDF-Expand-Label "%s" at SHA-384' % name,
                "result": "valid",
                "flags": [],
                "secret": SECRET48,
                "label": name,
                "context": ctx,
                "l": l,
                "out": expand_label("SHA384", l, SECRET48, name, ctx),
            }
        )

    write("openssl_hkdf_sha384_extract.json", extract, "HKDF-SHA384 mode EXTRACT_ONLY over the RFC 5869 A.1-A.3 inputs")
    write("openssl_hkdf_sha384_expand.json", expand, "HKDF-SHA384 mode EXPAND_ONLY over the same inputs")
    write("openssl_hkdf_sha384_label.json", label, "TLS13-KDF HKDF-Expand-Label at SHA-384 (RFC 8446 sec 7.1)")


if __name__ == "__main__":
    main()
