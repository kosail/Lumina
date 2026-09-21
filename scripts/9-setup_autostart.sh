#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 9-setup_autostart.sh — install and enable the Lúmina autostart service.
#
# RUN THIS ON THE PI (not the laptop). Safe to run more than once.
#
# WHAT THIS DOES (in order):
#   1. Renders scripts/lumina.service (substituting the user + home) and installs
#      it as /etc/systemd/system/lumina.service.
#   2. Installs a narrow sudoers rule so the runtime may power the device off when
#      it cannot find the Bluetooth audio sink (FR-06.1): only
#      `systemctl poweroff` is allowed, nothing else.
#   3. Runs `systemctl daemon-reload` and `enable`s the service (and `start`s it
#      when --start is given).
#
# It does NOT transmit or store any Bluetooth address: the service is generic.
#
# C++/shell note for Java readers: bash + `set -euo pipefail` is a strict mode
# (stop on first error / unset variable / failing pipe), like letting exceptions
# propagate. The service file is a small template whose @...@ placeholders are
# filled in with `sed` here.
#
# Usage:
#   scripts/9-setup_autostart.sh                 # install + enable for $USER
#   scripts/9-setup_autostart.sh --start         # ...and start it now
#   scripts/9-setup_autostart.sh --user lumina --home /home/lumina
#   scripts/9-setup_autostart.sh --verify        # show current state only
#   scripts/9-setup_autostart.sh --help
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="${SCRIPT_DIR}/lumina.service"
UNIT_DEST=/etc/systemd/system/lumina.service
SUDOERS_DEST=/etc/sudoers.d/lumina-poweroff

# --- Options -----------------------------------------------------------------
# When run via sudo, $SUDO_USER is the human; otherwise it is the current user.
TARGET_USER="${SUDO_USER:-$(id -un)}"
TARGET_HOME=""
DO_START=0
VERIFY_ONLY=0

print_usage() { sed -n '2,34p' "$0"; }

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

# --- Verify-only mode (no changes) -------------------------------------------
if [ "$VERIFY_ONLY" -eq 1 ]; then
  echo "== Unit file =="
  systemctl cat lumina.service 2>/dev/null || echo "  (not installed)"
  echo
  echo "== Enabled? =="
  systemctl is-enabled lumina.service 2>/dev/null || echo "  (not enabled)"
  echo
  echo "== Sudoers rule =="
  $SUDO visudo -c -f "$SUDOERS_DEST" 2>/dev/null || echo "  ($SUDOERS_DEST not installed)"
  echo
  echo "== Target user groups =="
  id "$TARGET_USER"
  exit 0
fi

# --- Sanity checks -----------------------------------------------------------
if [ ! -f "$TEMPLATE" ]; then
  echo "ERROR: template not found: $TEMPLATE" >&2
  exit 1
fi
if [ ! -x "${TARGET_HOME}/lumina" ]; then
  echo "WARNING: ${TARGET_HOME}/lumina is missing or not executable." >&2
  echo "         Deploy the runtime there before starting the service." >&2
fi

SYSTEMCTL="$(command -v systemctl)"

# --- 1. Render + install the unit --------------------------------------------
echo "==> Installing $UNIT_DEST (user=$TARGET_USER home=$TARGET_HOME)"
# `|` is the sed delimiter so the path slashes need no escaping.
$SUDO sed -e "s|@LUMINA_USER@|${TARGET_USER}|g" \
          -e "s|@LUMINA_HOME@|${TARGET_HOME}|g" \
          "$TEMPLATE" | $SUDO tee "$UNIT_DEST" >/dev/null
$SUDO chmod 0644 "$UNIT_DEST"

# --- 2. Narrow power-off sudoers rule ----------------------------------------
# The runtime may only run `systemctl poweroff` (with or without --no-wall); no
# other command and no general sudo access is granted.
echo "==> Installing $SUDOERS_DEST"
tmp_sudoers="$(mktemp)"
trap 'rm -f "$tmp_sudoers"' EXIT
cat > "$tmp_sudoers" <<EOF
# Allow the Lúmina runtime to power the device off when it cannot start (FR-06.1).
# Installed by scripts/9-setup_autostart.sh. Delete this file to revoke.
${TARGET_USER} ALL=(root) NOPASSWD: ${SYSTEMCTL} poweroff, ${SYSTEMCTL} poweroff --no-wall
EOF
# `visudo -c` validates the syntax before we put it in /etc/sudoers.d, so a bad
# rule can never lock the user out of sudo.
if ! $SUDO visudo -c -f "$tmp_sudoers" >/dev/null; then
  echo "ERROR: generated sudoers rule is invalid; not installing." >&2
  exit 1
fi
$SUDO install -m 0440 -o root -g root "$tmp_sudoers" "$SUDOERS_DEST"

# --- 3. Enable (+ optionally start) ------------------------------------------
echo "==> Reloading systemd and enabling lumina.service"
$SUDO systemctl daemon-reload
$SUDO systemctl enable lumina.service
if [ "$DO_START" -eq 1 ]; then
  echo "==> Starting lumina.service"
  $SUDO systemctl restart lumina.service
  $SUDO systemctl --no-pager --lines=20 status lumina.service || true
fi

echo
echo "Done. The runtime will start at boot and wait up to 3 minutes for the"
echo "Bluetooth sink, then power the device off if it never appears (FR-06.1)."
echo "Logs: journalctl -u lumina.service -f"
