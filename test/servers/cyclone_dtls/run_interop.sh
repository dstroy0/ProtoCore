#!/usr/bin/env bash
# DTLS 1.3 real-peer interop: drive the library's pc_dtls_conn server with the CycloneSSL client.
# Clones Oryx Embedded's CycloneSSL / CycloneCRYPTO / Common on first run (GPL; NOT vendored here),
# builds a DTLS 1.3 client (test/servers/cyclone_dtls/cyclone_dtls_client.c) against them, compiles
# the pc_dtls_conn UDP harness, then runs a full handshake + application-data round trip - twice:
# once with a plain X.509 certificate, once with an RFC 7250 RawPublicKey (--rpk). Prints PASS/FAIL.
#
# This is a second independent DTLS 1.3 reference peer alongside wolfSSL (test/servers/dtls_wolfssl):
# CycloneSSL is an unrelated from-scratch stack, so a byte-exact round trip against it is strong
# evidence our wire format (and the RFC 7250 RawPublicKey path) is correct, not self-consistent.
#
# Linux only (POSIX UDP sockets + native gcc/g++). Tested against CycloneSSL v2.6.4.
#
#   usage: test/servers/cyclone_dtls/run_interop.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)" # repo root (contains src/)
WORK="${CYCLONE_INTEROP_WORK:-$HERE/.work}"
CY="$WORK/cyclone"
SSL_VER="${CYCLONE_SSL_VERSION:-v2.6.4}"
PORT="${CYCLONE_INTEROP_PORT:-11160}"
mkdir -p "$WORK"

# --- 1. clone the Oryx Cyclone sources (GPL; fetched at run time, never committed here) ---
if [[ ! -d "$CY/CycloneSSL/tls" ]]; then
  echo ">> cloning Oryx CycloneSSL $SSL_VER + CycloneCRYPTO + Common"
  command -v git >/dev/null || { echo "install git first"; exit 2; }
  rm -rf "$CY"
  mkdir -p "$CY"
  git clone --depth 1 https://github.com/Oryx-Embedded/Common.git "$CY/Common"
  git clone --depth 1 https://github.com/Oryx-Embedded/CycloneCRYPTO.git "$CY/CycloneCRYPTO"
  git clone --depth 1 -b "$SSL_VER" https://github.com/Oryx-Embedded/CycloneSSL.git "$CY/CycloneSSL"
fi

# --- 2. build the CycloneSSL DTLS 1.3 client ---
# Cyclone module bodies are wrapped in #if (X_SUPPORT == ENABLED), so compiling the whole
# tls/tls13/dtls/dtls13/quic tree + all of CycloneCRYPTO (minus hardware/ and ocsp/, which needs
# CycloneTCP) with our lean config leaves the unused units empty. GPL_LICENSE_TERMS_ACCEPTED is
# required by Cyclone's headers for a GPL build.
OBJ="$WORK/obj"
if [[ ! -x "$WORK/cyclone_dtls_client" ]]; then
  echo ">> compiling CycloneSSL client"
  rm -rf "$OBJ"; mkdir -p "$OBJ"
  INCS="-I$HERE -I$CY/Common -I$CY/CycloneCRYPTO -I$CY/CycloneSSL \
        -I$CY/CycloneSSL/tls -I$CY/CycloneSSL/tls13 -I$CY/CycloneSSL/dtls \
        -I$CY/CycloneSSL/dtls13 -I$CY/CycloneSSL/quic"
  CFLAGS="-O2 -w -DGPL_LICENSE_TERMS_ACCEPTED"
  mapfile -t SRCS < <(
    find "$CY/CycloneSSL/tls" "$CY/CycloneSSL/tls13" "$CY/CycloneSSL/dtls" \
         "$CY/CycloneSSL/dtls13" "$CY/CycloneSSL/quic" -name '*.c'
    find "$CY/CycloneCRYPTO" \( -path "$CY/CycloneCRYPTO/hardware" -o -path "$CY/CycloneCRYPTO/ocsp" \) \
         -prune -o -name '*.c' -print
    echo "$CY/Common/os_port_posix.c"
    echo "$CY/Common/cpu_endian.c"
    echo "$CY/Common/debug.c"
    echo "$CY/Common/date_time.c"
  )
  export OBJ CFLAGS INCS
  compile_one() { local src="$1"; gcc $CFLAGS $INCS -c "$src" -o "$OBJ/$(echo "$src" | sed 's#/#_#g').o" \
      || { echo "COMPILE FAILED: $src" >&2; exit 1; }; }
  export -f compile_one
  printf '%s\n' "${SRCS[@]}" | xargs -P"$(nproc)" -I{} bash -c 'compile_one "$@"' _ {}
  gcc $CFLAGS $INCS -c "$HERE/cyclone_dtls_client.c" -o "$OBJ/client.o"
  gcc "$OBJ"/*.o -o "$WORK/cyclone_dtls_client" -lpthread -lm -lrt
fi

# --- 3. build the pc_dtls_conn server harness (same units as test/servers/dtls_wolfssl) ---
echo ">> compiling pc_dtls_conn harness"
D="$ROOT/src/network_drivers/presentation"
g++ -O2 -std=gnu++17 -DPC_ENABLE_DTLS=1 -DPC_ENABLE_TLS_RPK=1 -I"$ROOT/src" -I"$ROOT/test/mocks" \
  "$ROOT/test/servers/dtls_wolfssl/dtls_interop_server.cpp" \
  "$D/dtls/dtls_conn.cpp" "$D/dtls/dtls_record.cpp" "$D/dtls/dtls_handshake.cpp" \
  "$D/http3/tls13_msg.cpp" "$ROOT/src/network_drivers/tls/tls13_kdf.c" "$D/http3/quic_hkdf.cpp" "$D/http3/quic_aead.cpp" \
  "$D/ssh/crypto/ssh_sha256.cpp" "$D/ssh/crypto/ssh_hmac_sha256.cpp" "$D/ssh/crypto/ssh_sha512.cpp" \
  "$D/ssh/crypto/ssh_curve25519.cpp" "$D/ssh/crypto/ssh_ed25519.cpp" -o "$WORK/harness"

# --- 4. throwaway Ed25519 leaf certificate + its raw 32-byte seed ---
if [[ ! -f "$WORK/cert.der" ]]; then
  echo ">> generating Ed25519 certificate"
  openssl genpkey -algorithm ed25519 -out "$WORK/ed.key" 2>/dev/null
  openssl pkey -in "$WORK/ed.key" -outform DER 2>/dev/null | tail -c 32 > "$WORK/seed.bin" # PKCS#8 tail = raw seed
  openssl req -x509 -new -key "$WORK/ed.key" -out "$WORK/cert.pem" -days 3650 -subj '/CN=dtls-interop' 2>/dev/null
  openssl x509 -in "$WORK/cert.pem" -outform DER -out "$WORK/cert.der" 2>/dev/null
fi

# --- 5. run one interop variant ---
run_once() { # <label> <client-extra-flags> <require-rpk:0|1>
  local label="$1" extra="$2" require_rpk="${3:-0}"
  echo ">> running interop [$label] on udp/$PORT"
  pkill -f "$WORK/harness" 2>/dev/null && sleep 1 # never talk to a stale harness holding the port
  "$WORK/harness" "$PORT" "$WORK/cert.der" "$WORK/seed.bin" >"$WORK/harness.log" 2>&1 &
  local hpid=$!
  sleep 1
  if ! grep -q 'listening' "$WORK/harness.log"; then
    echo ">> FAIL [$label]: harness did not bind udp/$PORT"; cat "$WORK/harness.log"; return 1
  fi
  timeout 30 "$WORK/cyclone_dtls_client" 127.0.0.1 "$PORT" $extra >"$WORK/client.log" 2>&1
  local crc=$?
  sleep 0.3
  kill "$hpid" 2>/dev/null || true
  echo "--- server harness log [$label] ---"
  grep -E 'HANDSHAKE|APPDATA|INTEROP|FAIL' "$WORK/harness.log" || true
  echo "--- client output [$label] ---"
  cat "$WORK/client.log"
  grep -q 'INTEROP OK' "$WORK/harness.log" && [[ $crc -eq 0 ]] || { echo ">> FAIL [$label] (client rc=$crc)"; return 1; }
  if [[ "$require_rpk" = 1 ]] && ! grep -q 'RawPublicKey (RFC 7250)' "$WORK/client.log"; then
    echo ">> FAIL [$label]: expected the server to present a RawPublicKey but it did not"
    return 1
  fi
  echo ">> PASS [$label]"
}

run_once "plain X.509" "" 0
# Raw Public Keys (RFC 7250): --rpk makes CycloneSSL advertise server_certificate_type = RawPublicKey,
# and its rpkVerifyCallback fires only if the server actually sends a bare SubjectPublicKeyInfo - so a
# pass proves our RFC 7250 server credential interops with a second independent stack.
run_once "Raw Public Keys (--rpk)" "--rpk" 1
echo ">> ALL PASS"
