#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 10-setup_agent.sh — install and enable the Lúmina companion agent (FR-11).
#
# RUN THIS ON THE PI (not the laptop). Safe to run more than once.
#
# WHAT THIS DOES (in order):
#   1. Creates the shared runtime directory /run/lumina via a tmpfiles.d entry so
#      the runtime can write its status file and the agent can stage enroll images.
#   2. Generates the shared control token ($HOME/agent.token, mode 0600) if absent.
#   3. Installs a narrow sudoers rule so the agent may ONLY stop/start lumina.service.
#   4. Renders scripts/lumina-agent.service and installs it.
#   5. Reloads systemd and enables the service (starts it with --start).
#
# It prints the token so you can configure the companion app.
#
# Usage:
#   scripts/10-setup_agent.sh                 # install + enable for $USER
#   scripts/10-setup_agent.sh --start         # ...and start it now
#   scripts/10-setup_agent.sh --user lumina --home /home/lumina
#   scripts/10-setup_agent.sh --verify        # show current state only
#   scripts/10-setup_agent.sh --help
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="${SCRIPT_DIR}/lumina-agent.service"
UNIT_DEST=/etc/systemd/system/lumina-agent.service
SUDOERS_DEST=/etc/sudoers.d/lumina-agent
TMPFILES_DEST=/etc/tmpfiles.d/lumina.conf
TOKEN_NAME=agent.token

TARGET_USER="${SUDO_USER:-$(id -un)}"
TARGET_HOME=""
DO_START=0
VERIFY_ONLY=0

print_usage() { sed -n '2,21p' "$0"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --user)
      [ $# -ge 2 ] || { echo "--user needs a value" >&2; exit 2; }
      TARGET_USER="$2"; shift 2 ;;
    --home)
      [ $# -ge 2 ] || { echo "--home needs a value" >&2; exit 2; }
      TARGET_HOME="$2"; shift 2 ;;
    --start) DO_START=1; shift ;;
    --verify) VERIFY_ONLY=1; shift ;;
    -h|--help) print_usage; exit 0 ;;
    *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
  esac
done

if [ -z "$TARGET_HOME" ]; then
  TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6 || true)"
fi
if [ -z "$TARGET_HOME" ]; then
  echo "ERROR: could not determine the home directory for '$TARGET_USER'." >&2
  exit 2
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi
SYSTEMCTL="$(command -v systemctl)"

TOKEN_FILE="${TARGET_HOME}/${TOKEN_NAME}"

# --- Verify-only mode (no changes) -------------------------------------------
if [ "$VERIFY_ONLY" -eq 1 ]; then
  echo "== Unit file =="
  systemctl cat lumina-agent.service 2>/dev/null || echo "  (not installed)"
  echo
  echo "== Enabled? =="
  systemctl is-enabled lumina-agent.service 2>/dev/null || echo "  (not enabled)"
  echo
  echo "== Sudoers rule =="
  $SUDO visudo -c -f "$SUDOERS_DEST" 2>/dev/null || echo "  ($SUDOERS_DEST not installed)"
  echo
  echo "== Token =="
  if [ -f "$TOKEN_FILE" ]; then echo "  present: $TOKEN_FILE"; else echo "  (missing)"; fi
  exit 0
fi

if [ ! -f "$TEMPLATE" ]; then
  echo "ERROR: template not found: $TEMPLATE" >&2
  exit 1
fi
if [ ! -x "${TARGET_HOME}/lumina_agent" ]; then
  echo "WARNING: ${TARGET_HOME}/lumina_agent is missing or not executable." >&2
  echo "         Deploy the agent binary there before starting the service." >&2
fi

# --- 1. Shared runtime directory ---------------------------------------------
echo "==> Installing $TMPFILES_DEST (creates /run/lumina)"
# A tmpfiles.d entry (not RuntimeDirectory=) so the directory survives a runtime
# stop: the agent stages enrollment images and reads the status file there.
tmp_tmpfiles="$(mktemp)"
trap 'rm -f "$tmp_tmpfiles"' EXIT
cat > "$tmp_tmpfiles" <<EOF
# Shared Lúmina runtime directory (status file + enrollment scratch). FR-11.
# Installed by scripts/10-setup_agent.sh. Delete this file to revoke.
d /run/lumina 0755 ${TARGET_USER} ${TARGET_USER} -
EOF
$SUDO install -m 0644 -o root -g root "$tmp_tmpfiles" "$TMPFILES_DEST"
$SUDO systemd-tmpfiles --create "$TMPFILES_DEST" || true

# --- 2. Shared control token -------------------------------------------------
if [ ! -f "$TOKEN_FILE" ]; then
  echo "==> Generating control token at $TOKEN_FILE"
  # 16 random bytes -> 32 hex chars. `head` reads a fixed count so nothing gets a
  # SIGPIPE under `set -o pipefail`.
  token="$(head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')"
  printf '%s\n' "$token" > "$tmp_tmpfiles"
  $SUDO install -m 0600 -o "$TARGET_USER" -g "$TARGET_USER" "$tmp_tmpfiles" "$TOKEN_FILE"
else
  echo "==> Token already present at $TOKEN_FILE (leaving it unchanged)"
fi

# --- 3. Narrow sudoers rule ---------------------------------------------------
echo "==> Installing $SUDOERS_DEST"
tmp_sudoers="$(mktemp)"
cat > "$tmp_sudoers" <<EOF
# Allow the Lúmina companion agent to stop/start the runtime for enrollment (FR-11).
# Installed by scripts/10-setup_agent.sh. Delete this file to revoke.
${TARGET_USER} ALL=(root) NOPASSWD: ${SYSTEMCTL} stop lumina, ${SYSTEMCTL} start lumina
EOF
if ! $SUDO visudo -c -f "$tmp_sudoers" >/dev/null; then
  echo "ERROR: generated sudoers rule is invalid; not installing." >&2
  exit 1
fi
$SUDO install -m 0440 -o root -g root "$tmp_sudoers" "$SUDOERS_DEST"

# --- 4. Render + install the unit --------------------------------------------
# The app always talks to its gateway (the Pi), so bind control to the hotspot
# address and point the telemetry broadcast at the hotspot subnet. Both are
# detected from the AP interface; fall back to "all interfaces" / limited broadcast.
AP_IFACE="${AP_IFACE:-wlan0}"
BIND_ADDR="$(ip -4 -o addr show "$AP_IFACE" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n1)"
BROADCAST_ADDR="$(ip -4 -o addr show "$AP_IFACE" 2>/dev/null \
  | awk '{for (i = 1; i <= NF; i++) if ($i == "brd") print $(i + 1)}' | head -n1)"
BIND_ADDR="${BIND_ADDR:-0.0.0.0}"
BROADCAST_ADDR="${BROADCAST_ADDR:-255.255.255.255}"
echo "==> Hotspot '$AP_IFACE': bind=$BIND_ADDR broadcast=$BROADCAST_ADDR"

echo "==> Installing $UNIT_DEST (user=$TARGET_USER home=$TARGET_HOME)"
$SUDO sed -e "s|@LUMINA_USER@|${TARGET_USER}|g" \
          -e "s|@LUMINA_HOME@|${TARGET_HOME}|g" \
          -e "s|@LUMINA_BIND_ADDR@|${BIND_ADDR}|g" \
          -e "s|@LUMINA_BROADCAST@|${BROADCAST_ADDR}|g" \
          "$TEMPLATE" | $SUDO tee "$UNIT_DEST" >/dev/null
$SUDO chmod 0644 "$UNIT_DEST"

# --- 5. Enable (+ optionally start) ------------------------------------------
echo "==> Reloading systemd and enabling lumina-agent.service"
$SUDO systemctl daemon-reload
$SUDO systemctl enable lumina-agent.service
if [ "$DO_START" -eq 1 ]; then
  echo "==> Starting lumina-agent.service"
  $SUDO systemctl restart lumina-agent.service
  $SUDO systemctl --no-pager --lines=20 status lumina-agent.service || true
fi

echo
echo "Done. Configure the companion app with this token:"
if [ -r "$TOKEN_FILE" ]; then cat "$TOKEN_FILE"; else echo "  (run as $TARGET_USER to read $TOKEN_FILE)"; fi
echo
echo "Telemetry: UDP 47600 broadcast · Control: TCP 47601"
echo "Logs: journalctl -u lumina-agent.service -f"
