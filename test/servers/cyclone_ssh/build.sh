#!/usr/bin/env bash
# Build the CycloneSSH SSH-2 interop client (host Linux).
#
# Clones Oryx Embedded's Common / CycloneCRYPTO / CycloneSSH on first run (GPLv2, into .work/, never
# vendored here), then compiles the whole CycloneSSH ssh/ engine + shell client + all of CycloneCRYPTO
# (minus hardware/ and ocsp/, which need CycloneTCP) + the Common ports + the POSIX socket shim
# (socket.c / shim/core/net.h, which stands in for CycloneTCP so no TCP/IP stack is linked), and links
# the client. Cyclone bodies are wrapped in #if (X_SUPPORT == ENABLED), so a lean config leaves the
# unused ones as empty translation units.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${CYCLONE_SSH_WORK:-$HERE/.work}"
CY="$WORK/cyclone"
SSH_VER="${CYCLONE_SSH_VERSION:-master}"
OBJ="$WORK/obj"
mkdir -p "$WORK"

# --- clone the Oryx Cyclone sources (GPL; fetched at run time, never committed here) ---
if [[ ! -d "$CY/CycloneSSH/ssh" ]]; then
  echo ">> cloning Oryx Common + CycloneCRYPTO + CycloneSSH ($SSH_VER)"
  command -v git >/dev/null || { echo "install git first"; exit 2; }
  rm -rf "$CY"; mkdir -p "$CY"
  git clone --depth 1 https://github.com/Oryx-Embedded/Common.git "$CY/Common"
  git clone --depth 1 https://github.com/Oryx-Embedded/CycloneCRYPTO.git "$CY/CycloneCRYPTO"
  git clone --depth 1 -b "$SSH_VER" https://github.com/Oryx-Embedded/CycloneSSH.git "$CY/CycloneSSH"
fi

rm -rf "$OBJ"; mkdir -p "$OBJ"
# NOTE: the shim include dir must precede CycloneCRYPTO so "core/net.h" resolves to the shim while
# "core/crypto.h" still resolves to CycloneCRYPTO.
export INCS="-I$HERE -I$HERE/shim -I$CY/Common -I$CY/CycloneCRYPTO -I$CY/CycloneSSH"
export CFLAGS="-O2 -w -DGPL_LICENSE_TERMS_ACCEPTED"
export OBJ

mapfile -t SRCS < <(
  find "$CY/CycloneSSH/ssh" -name '*.c'
  echo "$CY/CycloneSSH/shell/shell_client.c"
  echo "$CY/CycloneSSH/shell/shell_client_misc.c"
  find "$CY/CycloneCRYPTO" \( -path "$CY/CycloneCRYPTO/hardware" -o -path "$CY/CycloneCRYPTO/ocsp" \) \
       -prune -o -name '*.c' -print
  echo "$CY/Common/os_port_posix.c"
  echo "$CY/Common/cpu_endian.c"
  echo "$CY/Common/debug.c"
  echo "$CY/Common/date_time.c"
  echo "$HERE/socket.c"
)
echo ">> compiling ${#SRCS[@]} Cyclone/shim source files"
compile_one() { local src="$1"; gcc $CFLAGS $INCS -c "$src" -o "$OBJ/$(echo "$src" | sed 's#/#_#g').o" \
    || { echo "COMPILE FAILED: $src" >&2; exit 1; }; }
export -f compile_one
printf '%s\n' "${SRCS[@]}" | xargs -P"$(nproc)" -I{} bash -c 'compile_one "$@"' _ {}

echo ">> compiling client + linking"
gcc $CFLAGS $INCS -c "$HERE/cyclone_ssh_client.c" -o "$OBJ/client.o"
gcc "$OBJ"/*.o -o "$WORK/cyclone_ssh_client" -lpthread -lm -lrt
echo ">> built $WORK/cyclone_ssh_client"
