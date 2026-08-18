#!/bin/sh
# Regenerate test/unit/src/crypto/x509/test_x509/x509_fixture.h from real certificates.
#
# The X.509 suite is checked against certificates OpenSSL made, and against the values OpenSSL
# itself reports for them. A parser checked only against DER its own author assembled proves the
# author's idea of the encoding twice over - which is how a parser and its suite come to agree on
# the same bug. This keeps the fixture reproducible rather than a blob nobody can re-derive.
#
#   tools/dev_env/gen_x509_fixture.sh
#
# The certificates carry fixed serials and a fixed SAN so the suite can assert on them; the keys and
# validity dates change per run, which is why the header records the dates alongside the bytes.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/test/unit/src/crypto/x509/test_x509/x509_fixture.h"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/san.cnf" <<'CNF'
[req]
distinguished_name = dn
req_extensions = ext
prompt = no
[dn]
CN = leaf.example.com
[ext]
subjectAltName = DNS:leaf.example.com, DNS:*.wild.example.com, DNS:other.example.net
CNF

cat >"$TMP/ca.cnf" <<'CNF'
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = Test Root CA
[ext]
basicConstraints = critical,CA:TRUE,pathlen:1
keyUsage = critical,keyCertSign,cRLSign
CNF

cd "$TMP"

# A key of the named algorithm.
newkey() {
    case "$1" in
        ed25519) openssl genpkey -algorithm ED25519 -out "$2" 2>/dev/null ;;
        p256) openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$2" 2>/dev/null ;;
        rsa) openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$2" 2>/dev/null ;;
    esac
}

# One CA per algorithm, and one leaf under each. A leaf's signature is made with its CA's key, so
# each verification path is reached by a certificate an independent implementation signed - three
# leaves under one Ed25519 CA would have exercised the Ed25519 verifier three times and the others
# never.
for alg in ed25519 p256 rsa; do
    # RFC 8032 sec 4: Ed25519 names no separate digest; the other two sign under SHA-256.
    DIGEST=sha256
    [ "$alg" = ed25519 ] && DIGEST=""
    newkey "$alg" "ca_$alg.key"
    openssl req -new -x509 -key "ca_$alg.key" -out "ca_$alg.pem" -days 3650 -config ca.cnf 2>/dev/null
    openssl x509 -in "ca_$alg.pem" -outform DER -out "ca_$alg.der"

    newkey "$alg" "$alg.key"
    openssl req -new -key "$alg.key" -out "$alg.csr" -config san.cnf 2>/dev/null
    openssl x509 -req -in "$alg.csr" -CA "ca_$alg.pem" -CAkey "ca_$alg.key" -out "$alg.pem" -days 365 \
        -extfile san.cnf -extensions ext -set_serial 4919 ${DIGEST:+-$DIGEST} 2>/dev/null
    openssl x509 -in "$alg.pem" -outform DER -out "$alg.der"
done

# The Ed25519 CA is also the one the parse cases name, so it keeps the plain name.
cp ca_ed25519.der ca.der
cp ca_ed25519.pem ca.pem

python "$ROOT/tools/dev_env/gen_x509_fixture.py" "$TMP" "$OUT"
