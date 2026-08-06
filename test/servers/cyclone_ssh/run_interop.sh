#!/usr/bin/env bash
# SSH-2 real-peer interop: the Oryx CycloneSSH client (a from-scratch second SSH stack) drives an SSH
# server through a full KEX + user auth + an encrypted-channel exchange. Two modes:
#
#   (default) self-test against a throwaway localhost OpenSSH sshd - proves the CycloneSSH client is
#             functional (public-key AND password auth). Self-contained + reproducible.
#
#   (rig)     CYCLONE_SSH_RIG_HOST=<ip> [CYCLONE_SSH_RIG_USER=admin CYCLONE_SSH_RIG_PASS=s3cret]
#             drive OUR SSH server on a device rig. Our server echoes channel DATA, so the client runs
#             in --echo mode (write a token, read it back). This is the real CycloneSSH<->pc interop;
#             it is HW-verified on an ESP32-P4 (see README).
#
# This is the SSH counterpart to test/servers/cyclone_dtls (CycloneSSL DTLS 1.3). The bug it exposed -
# server-preference algorithm negotiation, a RFC 4253 §7.1 violation - is fixed (see docs/BUGS.md).
#
# Linux only (POSIX sockets + native gcc). Needs openssh-server + ssh-keygen for the self-test.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${CYCLONE_SSH_WORK:-$HERE/.work}"
CLIENT="$WORK/cyclone_ssh_client"
mkdir -p "$WORK"

echo "=================== building CycloneSSH client ==================="
bash "$HERE/build.sh"

# --- rig mode: drive our SSH server on a device ---
if [[ -n "${CYCLONE_SSH_RIG_HOST:-}" ]]; then
  RUSER="${CYCLONE_SSH_RIG_USER:-admin}"
  RPASS="${CYCLONE_SSH_RIG_PASS:-s3cret}"
  echo ">> running interop [rig $CYCLONE_SSH_RIG_HOST] as $RUSER (--echo)"
  timeout 40 "$CLIENT" "$CYCLONE_SSH_RIG_HOST" 22 "$RUSER" "$RPASS" --echo 2>&1 | tee "$WORK/rig.log"
  grep -q 'interop OK' "$WORK/rig.log" && { echo ">> PASS [rig]"; exit 0; } || { echo ">> FAIL [rig]"; exit 1; }
fi

# --- self-test: throwaway localhost OpenSSH sshd ---
PORT="${SSH_INTEROP_PORT:-2222}"
USER_NAME="$(id -un)"
sudo_run() { echo "${WSL_SUDO_PASS:-dstroy0}" | sudo -S "$@"; return; }
command -v sshd >/dev/null 2>&1 || sudo_run apt-get install -y openssh-server >/dev/null 2>&1

[[ -f "$WORK/hostkey" ]]   || ssh-keygen -t ed25519 -f "$WORK/hostkey"   -N '' -q -C throwaway-host
[[ -f "$WORK/clientkey" ]] || ssh-keygen -t ed25519 -f "$WORK/clientkey" -N '' -q -C throwaway-client
cp "$WORK/clientkey.pub" "$WORK/authorized_keys"
chmod 600 "$WORK/hostkey" "$WORK/clientkey" "$WORK/authorized_keys"

printf '%s\n' \
  "Port $PORT" "ListenAddress 127.0.0.1" "HostKey $WORK/hostkey" "PidFile $WORK/sshd.pid" \
  "StrictModes no" "PubkeyAuthentication yes" "PasswordAuthentication yes" "UsePAM yes" \
  "AuthorizedKeysFile $WORK/authorized_keys" "AllowUsers $USER_NAME" > "$WORK/sshd_config"

sudo_run mkdir -p /run/sshd
sudo_run pkill -f "sshd -f $WORK/sshd_config" 2>/dev/null || true
sleep 0.5
sudo_run "$(command -v sshd)" -f "$WORK/sshd_config" -E "$WORK/sshd.stderr"
sleep 1

rc=0
run_variant() { # <label> <cyclone-client-args...>
  local label="$1"; shift
  echo ">> running interop [$label] on localhost:$PORT"
  timeout 30 "$CLIENT" 127.0.0.1 "$PORT" "$USER_NAME" "$@" >"$WORK/client.log" 2>&1
  local crc=$?
  grep -q 'interop OK' "$WORK/client.log" && [[ $crc -eq 0 ]] && echo ">> PASS [$label]" || { echo ">> FAIL [$label] rc=$crc"; sed -n '1,40p' "$WORK/client.log"; rc=1; }
  return
}
# Public-key auth in exec mode (OpenSSH runs the echo command). Password auth against a localhost
# sshd needs the host login password, so it is covered by the rig mode (and is HW-verified on the P4).
run_variant "public key" --key "$WORK/clientkey" "$WORK/clientkey.pub"

sudo_run pkill -f "sshd -f $WORK/sshd_config" 2>/dev/null || true
[[ $rc -eq 0 ]] && echo ">> ALL PASS" || echo ">> SOME FAILED"
exit $rc
