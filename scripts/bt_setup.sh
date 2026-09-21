#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bt_setup.sh — prepare the Raspberry Pi's single Bluetooth A2DP output.
#
# RUN THIS ON THE PI (not the laptop). Safe to run more than once.
#
# WHAT THIS DOES (in order):
#   1. Ensures BlueZ, the BlueALSA PCM plugin and the ALSA utilities are present.
#   2. Ensures bluetooth.service is enabled/running and the radio is unblocked.
#   3. Ensures BlueZ auto-enables the adapter at boot ([Policy] AutoEnable=true).
#   4. Marks the ONE earbud as trusted so it reconnects without re-pairing.
#      By default it discovers the single already-paired device; pass --pair the
#      first time to scan/pair, or --mac to target a specific device.
#   5. Optionally installs a boot-time reconnect unit (--with-connect-unit).
#   6. Prints verification commands (--verify shows state only, changes nothing).
#
# INVARIANTS: INV-014 (exactly one Bluetooth audio device — the bone-conduction
# earbuds, A2DP) and RAW_PLAN.md §9. The runtime never hardcodes a MAC: it writes
# to the `bluealsa` PCM, which resolves to whatever single device is connected.
# This script may still be told a MAC, but only to configure BlueZ — never the
# runtime.
#
# C++/shell note for Java readers: this is bash with `set -euo pipefail`, a strict
# mode: stop on the first error, on an unset variable, and on a failure inside a
# pipe — analogous to letting exceptions propagate instead of swallowing them.
#
# Usage:
#   scripts/bt_setup.sh                      # discover + trust the paired earbud
#   scripts/bt_setup.sh --pair               # scan/pair first, then trust
#   scripts/bt_setup.sh --mac AA:BB:CC:DD:EE:FF
#   scripts/bt_setup.sh --with-connect-unit  # also install a boot reconnect unit
#   scripts/bt_setup.sh --verify             # show current state only
#   scripts/bt_setup.sh --help
# ---------------------------------------------------------------------------

set -euo pipefail

# --- Options -----------------------------------------------------------------
TARGET_MAC=""
DO_PAIR=0
WITH_CONNECT_UNIT=0
VERIFY_ONLY=0

print_usage() { sed -n '2,40p' "$0"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --mac)
      [ $# -ge 2 ] || { echo "--mac needs a value (AA:BB:CC:DD:EE:FF)" >&2; exit 2; }
      TARGET_MAC="$2"
      shift 2
      ;;
    --pair) DO_PAIR=1; shift ;;
    --with-connect-unit) WITH_CONNECT_UNIT=1; shift ;;
    --verify) VERIFY_ONLY=1; shift ;;
    -h|--help) print_usage; exit 0 ;;
    *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
  esac
done

# `sudo` only when not already root.
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

# True when a command exists on PATH.
need() { command -v "$1" >/dev/null 2>&1; }

# --- Verify-only mode (no changes) -------------------------------------------
if [ "$VERIFY_ONLY" -eq 1 ]; then
  echo "== Adapter =="
  need bluetoothctl && bluetoothctl show || echo "  (bluetoothctl not installed)"
  echo
  echo "== Paired devices =="
  need bluetoothctl && bluetoothctl devices Paired || true
  echo
  echo "== BlueALSA PCMs (present only while the earbud is connected) =="
  need bluealsa-aplay && bluealsa-aplay -L || echo "  (bluealsa-aplay not installed / no PCM)"
  echo
  echo "== Boot reconnect unit =="
  systemctl is-enabled lumina-bt-connect.service 2>/dev/null \
    || echo "  (lumina-bt-connect.service not installed; the runtime retries on its own)"
  exit 0
fi

# --- 1. Packages -------------------------------------------------------------
echo "==> Ensuring BlueZ, BlueALSA and ALSA utilities are installed"
$SUDO apt-get update -y
# bluez provides bluetoothd + bluetoothctl; alsa-utils provides aplay/speaker-test.
$SUDO apt-get install -y bluez alsa-utils || true
# The BlueALSA package name changed across releases (bluealsa vs bluealsad);
# install whichever exists and then verify the PCM plugin is actually present.
if ! need bluealsa-aplay; then
  $SUDO apt-get install -y bluealsa || $SUDO apt-get install -y bluealsad || true
fi
if ! need bluetoothctl; then
  echo "ERROR: bluetoothctl is missing after installing bluez." >&2
  exit 1
fi
if ! need bluealsa-aplay; then
  echo "WARNING: bluealsa-aplay not found. Install the BlueALSA package for this" >&2
  echo "         image (package 'bluealsa' or 'bluealsad') before running Lúmina." >&2
fi

# --- 2. Bluetooth service + radio --------------------------------------------
echo "==> Enabling and starting bluetooth.service"
$SUDO systemctl enable --now bluetooth.service
$SUDO rfkill unblock bluetooth || true

# --- 3. Auto-enable the adapter at boot --------------------------------------
echo "==> Ensuring [Policy] AutoEnable=true in /etc/bluetooth/main.conf"
MAIN_CONF=/etc/bluetooth/main.conf
if [ -f "$MAIN_CONF" ]; then
  if grep -Eq '^[[:space:]]*AutoEnable[[:space:]]*=' "$MAIN_CONF"; then
    echo "    AutoEnable already present"
  else
    $SUDO cp "$MAIN_CONF" "${MAIN_CONF}.lumina.bak"
    if grep -q '^\[Policy\]' "$MAIN_CONF"; then
      # Insert right after the [Policy] header, keeping the rest of the file.
      $SUDO awk '/^\[Policy\]/{print; if(!done){print "AutoEnable = true"; done=1}; next} {print}' \
        "${MAIN_CONF}.lumina.bak" | $SUDO tee "$MAIN_CONF" >/dev/null
      echo "    added AutoEnable = true under the existing [Policy] section"
    else
      printf '\n[Policy]\nAutoEnable = true\n' | $SUDO tee -a "$MAIN_CONF" >/dev/null
      echo "    appended a [Policy] section with AutoEnable = true"
    fi
  fi
else
  echo "    WARNING: $MAIN_CONF not found; BlueZ defaults to AutoEnable=true anyway" >&2
fi

# --- Device discovery (no hardcoded MAC) -------------------------------------
if [ -n "$TARGET_MAC" ] && ! [[ "$TARGET_MAC" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then
  echo "ERROR: '$TARGET_MAC' is not a valid MAC address." >&2
  exit 2
fi

if [ -z "$TARGET_MAC" ]; then
  # `bluetoothctl devices Paired` prints "Device AA:BB:.. Name" lines.
  mapfile -t MACS < <(bluetoothctl devices Paired 2>/dev/null | awk '/^Device /{print $2}')
  if [ "${#MACS[@]}" -eq 1 ]; then
    TARGET_MAC="${MACS[0]}"
    echo "==> Using the single paired device: $TARGET_MAC"
  elif [ "${#MACS[@]}" -eq 0 ]; then
    echo "No paired Bluetooth device yet. Put the earbuds in pairing mode and" >&2
    echo "re-run with --pair, or pass --mac AA:BB:CC:DD:EE:FF." >&2
    exit 1
  else
    echo "More than one paired device found, but INV-014 allows exactly one:" >&2
    printf '  %s\n' "${MACS[@]}" >&2
    echo "Pass --mac AA:BB:CC:DD:EE:FF to pick the earbuds." >&2
    exit 1
  fi
fi

# --- 4. Pair / trust / connect -----------------------------------------------
bluetoothctl power on >/dev/null 2>&1 || true
if [ "$DO_PAIR" -eq 1 ]; then
  echo "==> Pairing $TARGET_MAC (earbuds must be in pairing mode)"
  bluetoothctl --timeout 30 scan on >/dev/null 2>&1 || true
  bluetoothctl --timeout 20 pair "$TARGET_MAC" || true
  bluetoothctl --timeout 20 trust "$TARGET_MAC" || true
  bluetoothctl --timeout 20 connect "$TARGET_MAC" || true
  bluetoothctl scan off >/dev/null 2>&1 || true
else
  echo "==> Trusting $TARGET_MAC"
  bluetoothctl trust "$TARGET_MAC"
  echo "==> Connecting $TARGET_MAC"
  bluetoothctl connect "$TARGET_MAC" || true
fi

# --- 5. Optional boot-time reconnect unit ------------------------------------
if [ "$WITH_CONNECT_UNIT" -eq 1 ]; then
  BLUETOOTHCTL="$(command -v bluetoothctl)"
  echo "==> Installing lumina-bt-connect.service for $TARGET_MAC"
  $SUDO tee /etc/systemd/system/lumina-bt-connect.service >/dev/null <<EOF
[Unit]
Description=Reconnect the Lúmina Bluetooth earbuds at boot
After=bluetooth.service
Wants=bluetooth.service

[Service]
Type=oneshot
# Best-effort: try to connect for up to ~60 s, then give up (the runtime also
# retries, and the earbuds often reconnect on their own when the case opens).
ExecStart=/bin/sh -c 'for i in \$(seq 1 20); do ${BLUETOOTHCTL} connect ${TARGET_MAC} && exit 0; sleep 3; done; exit 0'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
  $SUDO systemctl daemon-reload
  $SUDO systemctl enable lumina-bt-connect.service
fi

# --- 6. Next steps -----------------------------------------------------------
echo
echo "Done."
echo "Verify the audio path to the earbuds with a short test tone:"
echo "  speaker-test -D bluealsa -c 1 -t sine -l 1"
echo "Then run the runtime. If the earbuds are not connected yet, Lúmina waits up"
echo "to 3 minutes and only then powers the device off (see docs/BLUETOOTH.md)."
