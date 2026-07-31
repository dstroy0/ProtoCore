#!/usr/bin/env bash
# DTLS 1.3 real-peer interop: drive the library's dtls_conn server with wolfSSL's DTLS 1.3 client.
# Builds wolfSSL (with DTLS 1.3) on first run, generates a throwaway Ed25519 certificate, compiles
# the harness (test/servers/dtls_wolfssl/dtls_interop_server.cpp) against the library sources, then
# runs a full handshake + application-data round trip. Prints "INTEROP OK" on success.
#
# Linux only (needs a POSIX UDP socket + a native g++). Tested on Debian/aarch64 (Raspberry Pi).
#
#   usage: test/servers/dtls_wolfssl/run_interop.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)" # repo root (contains src/)
WORK="${DTLS_INTEROP_WORK:-$HERE/.work}"
WOLF_VER="${WOLFSSL_VERSION:-v5.9.2-stable}"
PORT="${DTLS_INTEROP_PORT:-11150}"
mkdir -p "$WORK"

# --- 1. wolfSSL with DTLS 1.3 (release tarballs ship a pre-built configure, but only the source
#        tarball is a GitHub asset here, so build from the git tag; needs autoconf/automake/libtool) ---
CLIENT="$WORK/wolfssl/examples/client/client"
# Rebuild if the client is missing or was built without DTLS connection-id (RFC 9146, --enable-dtlscid)
# or Raw Public Keys (RFC 7250, --enable-rpk) support.
if [[ ! -x "$CLIENT" ]] || ! grep -q 'WOLFSSL_DTLS_CID' "$WORK/wolfssl/wolfssl/options.h" 2>/dev/null \
   || ! grep -q 'HAVE_RPK' "$WORK/wolfssl/wolfssl/options.h" 2>/dev/null; then
  echo ">> building wolfSSL $WOLF_VER (DTLS 1.3 + connection ID + Raw Public Keys)"
  command -v autoconf >/dev/null || { echo "install autoconf automake libtool first"; exit 2; }
  rm -rf "$WORK/wolfssl"
  git clone --depth 1 -b "$WOLF_VER" https://github.com/wolfSSL/wolfssl.git "$WORK/wolfssl"
  ( cd "$WORK/wolfssl" && ./autogen.sh && \
    ./configure --enable-dtls --enable-dtls13 --enable-ed25519 --enable-curve25519 \
                --enable-aesgcm --enable-dtlscid --enable-rpk --disable-shared && make -j"$(nproc)" )
fi

# --- 2. throwaway Ed25519 leaf certificate + its raw 32-byte seed ---
if [[ ! -f "$WORK/cert.der" ]]; then
  echo ">> generating Ed25519 certificate"
  openssl genpkey -algorithm ed25519 -out "$WORK/ed.key" 2>/dev/null
  openssl pkey -in "$WORK/ed.key" -outform DER 2>/dev/null | tail -c 32 > "$WORK/seed.bin" # PKCS#8 tail = raw seed
  openssl req -x509 -new -key "$WORK/ed.key" -out "$WORK/cert.pem" -days 3650 -subj '/CN=dtls-interop' 2>/dev/null
  openssl x509 -in "$WORK/cert.pem" -outform DER -out "$WORK/cert.der" 2>/dev/null
fi

# --- 3. compile the harness against the library sources (PC_ENABLE_DTLS) ---
echo ">> compiling harness"
D="$ROOT/src/network_drivers/presentation"
# -I test/mocks supplies the host Arduino.h shim (millis()) that services/system/clock.h pulls in - the same
# shim the pio host tests use; the harness is likewise a host build.
g++ -O2 -std=gnu++17 -DPC_ENABLE_DTLS=1 -DPC_ENABLE_TLS_RPK=1 -I"$ROOT/src" -I"$ROOT/test/mocks" "$HERE/dtls_interop_server.cpp" \
  "$D/dtls/dtls_conn.cpp" "$D/dtls/dtls_record.cpp" "$D/dtls/dtls_handshake.cpp" \
  "$D/http3/tls13_msg.cpp" "$D/http3/tls13_kdf.cpp" "$D/http3/quic_hkdf.cpp" "$D/http3/quic_aead.cpp" \
  "$D/ssh/crypto/ssh_sha256.cpp" "$D/ssh/crypto/ssh_hmac_sha256.cpp" "$D/ssh/crypto/ssh_sha512.cpp" \
  "$D/ssh/crypto/ssh_curve25519.cpp" "$D/ssh/crypto/ssh_ed25519.cpp" -o "$WORK/harness"

# --- 4. run the harness + the wolfSSL DTLS 1.3 client against it, twice ---
#   Common client flags: -u DTLS/UDP, -v 4 DTLS 1.3, -d skip cert-chain verification of the throwaway
#   cert (the CertificateVerify signature is still checked). The client runs from the wolfSSL dir so
#   its default ./certs/ resolves.
#   Run A (default groups): wolfSSL leads with a non-X25519 key_share, so the server answers with a
#     HelloRetryRequest and renegotiates the group to X25519 (RFC 9147 sec 5.1) - the HRR interop path.
#   Run B (-t): wolfSSL offers an X25519 key_share up front - the one-round-trip path.
run_once() { # <label> <client-extra-flags> <require-hrr:0|1> <require-cid:0|1>
  local label="$1" extra="$2" require_hrr="$3" require_cid="${4:-0}"
  echo ">> running interop [$label] on udp/$PORT"
  pkill -f "$WORK/harness" 2>/dev/null && sleep 1 # never talk to a stale harness holding the port
  "$WORK/harness" "$PORT" "$WORK/cert.der" "$WORK/seed.bin" >"$WORK/harness.log" 2>&1 &
  local hpid=$!
  sleep 1
  if ! grep -q 'listening' "$WORK/harness.log"; then
    echo ">> FAIL [$label]: harness did not bind udp/$PORT"; cat "$WORK/harness.log"; return 1
  fi
  ( cd "$WORK/wolfssl" && echo 'hello wolfssl!' |
    timeout 10 ./examples/client/client -u -v 4 -d $extra -h 127.0.0.1 -p "$PORT" ) >"$WORK/client.log" 2>&1 || true
  kill "$hpid" 2>/dev/null || true
  echo "--- harness log [$label] ---"
  grep -E 'HANDSHAKE|APPDATA|INTEROP|FAIL' "$WORK/harness.log" || true
  grep -q 'INTEROP OK' "$WORK/harness.log" || { echo ">> FAIL [$label]"; return 1; }
  if [[ "$require_hrr" = 1 ]] && ! grep -q 'via HelloRetryRequest' "$WORK/harness.log"; then
    echo ">> FAIL [$label]: expected a HelloRetryRequest but the handshake was 1-RTT"
    return 1
  fi
  if [[ "$require_cid" = 1 ]] && ! grep -q 'with connection ID' "$WORK/harness.log"; then
    echo ">> FAIL [$label]: expected a connection ID but none was negotiated"
    return 1
  fi
  echo ">> PASS [$label]"
}

run_once "HRR (default groups)" "" 1
run_once "direct X25519 (-t)" "-t" 0
# Connection ID (RFC 9146 / RFC 9147 sec 9): the client offers a CID, so the server places it in the
# records it sends and chooses its own CID for the client's records; the full exchange runs with CIDs.
run_once "connection ID (--cid)" "-t --cid feedc0de" 0 1
# Raw Public Keys (RFC 7250): wolfSSL --rpk offers server_certificate_type = RawPublicKey ONLY, so the
# server MUST present its Ed25519 SubjectPublicKeyInfo (not the X.509 chain) or the client rejects it -
# a completed handshake + app-data echo is proof the server sent a conformant RawPublicKey.
run_once "Raw Public Keys (--rpk)" "-t --rpk" 0
echo ">> ALL PASS"
