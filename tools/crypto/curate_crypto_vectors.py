# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Curate external crypto known-answer-test (KAT) vectors into compact, auditable
# JSON subsets under test/vectors/. These ground the library's crypto primitives
# against vectors produced OUTSIDE the codebase:
#   - Project Wycheproof (C2SP/wycheproof) for HMAC-SHA256/512, AES-128-GCM,
#     X25519, and Ed25519 - including its adversarial edge cases (wrong tags,
#     modified IVs, low-order points, signature malleability), and
#   - the RFC appendix vectors for HKDF-SHA256 (RFC 5869), the ChaCha20 block
#     (RFC 8439 2.4.2), and Poly1305 (RFC 8439 2.5.2).
#
# Run this only to (re)fetch/refresh the vendored subsets; it needs the network
# and a git checkout of Wycheproof. The committed test/vectors/*.json are then
# consumed offline by tools/gen_crypto_vectors.py. Selection is deterministic:
# every vector whose result is "invalid" or that carries a Wycheproof flag (the
# security-relevant edge cases) is kept up to CAP_FLAGGED, plus the first
# CAP_PLAIN plain-"valid" vectors, so both outcomes are always represented.
#
# Usage:  python3 tools/curate_crypto_vectors.py [path-to-wycheproof-checkout]
# If no path is given it shallow-clones a pinned commit into a temp dir.

import glob
import json
import os
import subprocess
import sys
import tempfile

from tools.ci_tooling.lib import doc_region as dr

# Pinned Wycheproof revision for reproducible provenance. Refreshing the vectors
# is a deliberate act: bump this, re-run, and review the JSON diff.
WYCHEPROOF_REPO = "https://github.com/C2SP/wycheproof"
# A commit, not a branch. The comment above has always said "pinned" while this named a moving
# ref, so a refresh was never reproducible - and upstream has since renamed its default branch,
# so cloning "master" failed outright and the refresh could not run at all.
WYCHEPROOF_BRANCH = "main"  # upstream renamed this from master; cloning the old name failed outright
WYCHEPROOF_REF = "b61843a9a5115bb758134b6a1f5d5e502d445342"

CAP_INVALID = 20  # rejection cases kept per primitive, taken before anything else
CAP_FLAGGED = 40  # rejection + edge-case vectors kept per primitive, CAP_INVALID of them reserved
CAP_PLAIN = 12  # plain happy-path vectors kept per primitive

ROOT = dr.repo_root(__file__)
OUT_DIR = os.path.join(ROOT, "test", "vectors")


def _clone_pinned():
    # Shallow-clone the branch, then refuse to curate unless its head is the recorded commit. A
    # refresh that silently picked up whatever upstream had moved to would not be reproducible, and
    # the corpus is evidence.
    d = tempfile.mkdtemp(prefix="wycheproof_")
    subprocess.run(["git", "clone", "--depth", "1", "--branch", WYCHEPROOF_BRANCH, WYCHEPROOF_REPO, d], check=True)
    head = _rev(d)
    if head != WYCHEPROOF_REF:
        raise SystemExit(
            "wycheproof %s is at %s, not the recorded %s - review the upstream diff, then update "
            "WYCHEPROOF_REF" % (WYCHEPROOF_BRANCH, head, WYCHEPROOF_REF)
        )
    return d


def _rev(checkout):
    return subprocess.run(["git", "-C", checkout, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()


def _find(checkout, name):
    hits = glob.glob(os.path.join(checkout, "**", name), recursive=True)
    if not hits:
        raise SystemExit("cannot find %s under %s" % (name, checkout))
    # Prefer the testvectors_v1 schema when both are present.
    hits.sort(key=lambda p: ("testvectors_v1" not in p, len(p)))
    return hits[0]


def _select(tests):
    """Keep the rejection cases first, then the flagged edge cases, then plain-valid ones.

    A "result": "invalid" vector and a "valid" one carrying a flag are not interchangeable: the
    first is the only kind that proves the code REFUSES something. Sharing one bucket let a corpus
    whose valid vectors are all flagged - AES-GCM, where every one carries Pseudorandom, Ktv or
    SpecialCase - fill the cap before a single rejection case was reached, and ship zero of them.
    """
    invalid, flagged, plain = [], [], []
    for t in tests:
        if t["result"] == "invalid":
            invalid.append(t)
        elif t["result"] == "valid" and not t.get("flags"):
            plain.append(t)
        else:  # flagged-valid, or "acceptable"
            flagged.append(t)
    keep_invalid = invalid[:CAP_INVALID]
    return keep_invalid + flagged[: CAP_FLAGGED - len(keep_invalid)] + plain[:CAP_PLAIN]


def curate_wycheproof(checkout, filename, group_filter, field_map):
    """Load one Wycheproof file, flatten selected group/test fields to hex."""
    path = _find(checkout, filename)
    with open(path, "r") as f:
        doc = json.load(f)
    out = []
    for g in doc["testGroups"]:
        if group_filter and not group_filter(g):
            continue
        picked = _select(g["tests"])
        for t in picked:
            v = {"tcId": t["tcId"], "comment": t.get("comment", ""), "result": t["result"], "flags": t.get("flags", [])}
            for dst, src in field_map.items():
                # src may be "group:key" or a dotted "group:key.sub" (pull from
                # the group), otherwise it is a test-level key.
                if src.startswith("group:"):
                    node = g
                    for part in src[6:].split("."):
                        node = node.get(part, "") if isinstance(node, dict) else ""
                    v[dst] = str(node)
                else:
                    v[dst] = t.get(src, "")
            out.append(v)
    return {
        "source": WYCHEPROOF_REPO,
        "commit": _rev(checkout),
        "file": os.path.relpath(path, checkout),
        "vectors": out,
    }


def write(name, doc):
    os.makedirs(OUT_DIR, exist_ok=True)
    p = os.path.join(OUT_DIR, name)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=False)
        f.write("\n")
    print("wrote %s (%d vectors)" % (os.path.relpath(p, ROOT), len(doc["vectors"])))


# --- RFC appendix vectors (small, canonical; transcribed from the RFC text and
#     independently checkable with a reference tool). ------------------------
def rfc_vectors():
    # RFC 5869 Appendix A - HKDF-SHA256. Extract: PRK = HMAC-SHA256(salt, IKM). Expand: the same
    # cases publish info, L and OKM, and A.2 (L=82) is the only published multi-block chain.
    hkdf = {
        "source": "RFC 5869 Appendix A",
        "commit": "",
        "file": "rfc5869",
        "vectors": [
            {  # A.1
                "tcId": 1,
                "comment": "RFC 5869 A.1 basic",
                "result": "valid",
                "flags": [],
                "salt": "000102030405060708090a0b0c",
                "ikm": "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                "prk": "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5",
                "info": "f0f1f2f3f4f5f6f7f8f9",
                "l": 42,
                "okm": "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf" "34007208d5b887185865",
            },
            {  # A.2 longer inputs
                "tcId": 2,
                "comment": "RFC 5869 A.2 longer inputs",
                "result": "valid",
                "flags": [],
                "salt": "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f"
                "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
                "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                "ikm": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
                "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
                "404142434445464748494a4b4c4d4e4f",
                "prk": "06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244",
                "info": "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
                "d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeef"
                "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
                "l": 82,
                "okm": "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
                "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
                "cc30c58179ec3e87c14c01d5c1f3434f1d87",
            },
            {  # A.3 zero-length salt
                "tcId": 3,
                "comment": "RFC 5869 A.3 zero-length salt",
                "result": "valid",
                "flags": [],
                "salt": "",
                "ikm": "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                "prk": "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04",
                "info": "",
                "l": 42,
                "okm": "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d" "9d201395faa4b61a96c8",
            },
        ],
    }
    # RFC 8439 2.4.2 - ChaCha20 block: 64-byte keystream for the given key/nonce
    # at the given 32-bit block counter (little-endian counter word).
    chacha = {
        "source": "RFC 8439 sec 2.4.2",
        "commit": "",
        "file": "rfc8439",
        "vectors": [
            {
                "tcId": 1,
                "comment": "RFC 8439 2.4.2 keystream",
                "result": "valid",
                "flags": [],
                "key": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                "nonce": "000000000000004a00000000",
                "counter": 1,
                "keystream": "224f51f3401bd9e12fde276fb8631ded8c131f823d2c06e27e4fcaec9ef3cf788a3b0aa372600a92b57974cded2b9334794cba40c63e34cdea212c4cf07d41b7",
            }
        ],
    }
    # RFC 8439 2.5.2 - Poly1305: one-time MAC of the message under the 32-byte key.
    poly = {
        "source": "RFC 8439 sec 2.5.2",
        "commit": "",
        "file": "rfc8439",
        "vectors": [
            {
                "tcId": 1,
                "comment": "RFC 8439 2.5.2 tag",
                "result": "valid",
                "flags": [],
                "key": "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b",
                "msg": "43727970746f6772617068696320466f72756d2052657365617263682047726f7570",
                "tag": "a8061dc1305136c6c22b8baf0c0127a9",
            }
        ],
    }
    # RFC 8032 sec 7.1 - Ed25519 signing: the deterministic signature (and derived
    # public key) for a given 32-byte secret seed + message. Covers the SSH host-key
    # signing path (verify is covered by the Wycheproof vectors).
    ed_sign = {
        "source": "RFC 8032 Section 7.1",
        "commit": "",
        "file": "rfc8032",
        "vectors": [
            {
                "tcId": 1,
                "comment": "RFC 8032 7.1 TEST 1 (empty message)",
                "result": "valid",
                "flags": [],
                "seed": "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
                "pub": "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
                "msg": "",
                "sig": "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
            },
            {
                "tcId": 2,
                "comment": "RFC 8032 7.1 TEST 2 (1-byte message)",
                "result": "valid",
                "flags": [],
                "seed": "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
                "pub": "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
                "msg": "72",
                "sig": "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
            },
            {
                "tcId": 3,
                "comment": "RFC 8032 7.1 TEST 3 (2-byte message)",
                "result": "valid",
                "flags": [],
                "seed": "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
                "pub": "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
                "msg": "af82",
                "sig": "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
            },
            {
                "tcId": 5,
                "comment": "RFC 8032 7.1 TEST 1024 (1023-byte message, multi-block SHA-512)",
                "result": "valid",
                "flags": [],
                "seed": "f5e5767cf153319517630f226876b86c8160cc583bc013744c6bf255f5cc0ee5",
                "pub": "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e",
                "msg": "08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d879de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4feba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbefefd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed185ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f27088d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b0707e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128bab27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51addd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429ec96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb751fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34dff7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e488acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a32ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5fb93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b50d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380db2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0",
                "sig": "0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03",
            },
            {
                "tcId": 4,
                "comment": "RFC 8032 7.1 TEST SHA(abc) (64-byte message, full SHA-512 block)",
                "result": "valid",
                "flags": [],
                "seed": "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
                "pub": "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
                "msg": "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
                "sig": "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704",
            },
        ],
    }
    return hkdf, chacha, poly, ed_sign


def main():
    checkout = sys.argv[1] if len(sys.argv) > 1 else _clone_pinned()
    rev = _rev(checkout)
    print("wycheproof @ %s" % rev)

    aes_gcm_128 = lambda g: g.get("keySize") == 128 and g.get("ivSize") == 96 and g.get("tagSize") == 128
    write(
        "wycheproof_aes_128_gcm.json",
        curate_wycheproof(
            checkout,
            "aes_gcm_test.json",
            aes_gcm_128,
            {"key": "key", "iv": "iv", "aad": "aad", "msg": "msg", "ct": "ct", "tag": "tag"},
        ),
    )
    write(
        "wycheproof_hmac_sha256.json",
        curate_wycheproof(
            checkout,
            "hmac_sha256_test.json",
            None,
            {"key": "key", "msg": "msg", "tag": "tag", "tagSize": "group:tagSize"},
        ),
    )
    write(
        "wycheproof_hmac_sha512.json",
        curate_wycheproof(
            checkout,
            "hmac_sha512_test.json",
            None,
            {"key": "key", "msg": "msg", "tag": "tag", "tagSize": "group:tagSize"},
        ),
    )
    write(
        "wycheproof_x25519.json",
        curate_wycheproof(
            checkout,
            "x25519_test.json",
            None,
            {"public": "public", "private": "private", "shared": "shared"},
        ),
    )
    # Ed25519 lives under an "eddsa"-style file; pull the group public key.
    ed_name = "ed25519_test.json"
    try:
        _find(checkout, ed_name)
    except SystemExit:
        ed_name = "eddsa_test.json"
    write(
        "wycheproof_ed25519.json",
        curate_wycheproof(
            checkout,
            ed_name,
            None,
            {"public": "group:publicKey.pk", "msg": "msg", "sig": "sig"},
        ),
    )

    hkdf, chacha, poly, ed_sign = rfc_vectors()
    write("rfc5869_hkdf_sha256.json", hkdf)
    write("rfc8439_chacha20.json", chacha)
    write("rfc8439_poly1305.json", poly)
    write("rfc8032_ed25519_sign.json", ed_sign)


if __name__ == "__main__":
    main()
