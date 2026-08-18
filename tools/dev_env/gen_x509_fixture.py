"""Turn the OpenSSL-made certificates into the C fixture the X.509 suite reads.

Invoked by gen_x509_fixture.sh, which makes the certificates. Not run directly.

The expected values are read back out of OpenSSL too, not written by hand: a suite whose author
supplies both the input and the answer only proves the author is consistent with themselves.
"""

import io
import os
import subprocess
import sys

SRC = sys.argv[1]
OUT = sys.argv[2]


def openssl(pem, *args):
    r = subprocess.run(
        ["openssl", "x509", "-in", os.path.join(SRC, pem), "-noout", *args], capture_output=True, text=True
    )
    return r.stdout.strip()


def carray(name, data, indent="    "):
    out = ["static const uint8_t %s[] = {" % name]
    line = indent
    for i, b in enumerate(data):
        line += "0x%02X," % b
        if len(line) > 108:
            out.append(line)
            line = indent
        elif i != len(data) - 1:
            line += " "
    if line.strip():
        out.append(line)
    out.append("};")
    return "\n".join(out)


import datetime as dt


def epoch(s):
    # OpenSSL prints "Aug 18 11:44:12 2026 GMT"
    return int(dt.datetime.strptime(s, "%b %d %H:%M:%S %Y %Z").replace(tzinfo=dt.timezone.utc).timestamp())


parts = ["""// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// GENERATED - real certificates, produced by OpenSSL and dumped here with the values OpenSSL itself
// reports for them. Regenerate with tools/dev_env/gen_x509_fixture.sh.
//
// A parser tested only against bytes its own author assembled proves the author's idea of DER
// twice. These come from an independent encoder, so a field read wrong here is a disagreement with
// the implementation the rest of the world uses.

#ifndef PROTOCORE_TEST_X509_FIXTURE_H
#define PROTOCORE_TEST_X509_FIXTURE_H

#include <stddef.h>
#include <stdint.h>
"""]

for stem, label in [
    ("ca", "CA"),
    ("ed25519", "ED25519"),
    ("p256", "P256"),
    ("rsa", "RSA"),
    ("ca_ed25519", "CA_ED25519"),
    ("ca_p256", "CA_P256"),
    ("ca_rsa", "CA_RSA"),
]:
    der = io.open(os.path.join(SRC, stem + ".der"), "rb").read()
    nb = epoch(openssl(stem + ".pem", "-startdate").split("=", 1)[1])
    na = epoch(openssl(stem + ".pem", "-enddate").split("=", 1)[1])
    parts.append(
        "\n// %s\n%s\n#define X509_%s_NOT_BEFORE %dULL\n#define X509_%s_NOT_AFTER %dULL"
        % (openssl(stem + ".pem", "-subject"), carray("X509_" + label + "_DER", der), label, nb, label, na)
    )

parts.append("\n#endif // PROTOCORE_TEST_X509_FIXTURE_H\n")
io.open(OUT, "w", encoding="utf-8", newline="").write("\n".join(parts))
print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))
