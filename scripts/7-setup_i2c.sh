#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 7-setup_i2c.sh — prepare the Raspberry Pi for the (front) VL53L0X sensor.
#
# RUN THIS ON THE PI (not the laptop). Safe to run more than once.
#
# WHAT THIS DOES (in order):
#   1. Installs `i2c-tools` (i2cdetect/i2cget/i2cset) and `gpiod` (gpioset/gpioget).
#   2. Enables the I2C hardware interface (raspi-config, else /boot/firmware/config.txt).
#   3. Ensures the i2c-dev kernel module loads at boot (/etc/modules-load.d).
#   4. Adds your user to the `i2c` group so no sudo is needed to use the bus.
#   5. Prints the front-sensor wiring/pin map (INV-075) and verification commands.
#
# NOTE (verified 2026-09-18): on this trixie image, `raspi-config` + reboot was
# enough — /dev/i2c-1 and i2cdetect worked WITHOUT steps 3-4. They are kept as
# belt-and-braces for other images.
#
# Scope: ONE front VL53L0X at 0x29. The rear sensor is DEFERRED (docs/PROXIMITY.md
# Appendix). Never assume — full walkthrough: docs/PROXIMITY.md
#
# C++/shell note for Java readers: this is bash. `set -euo pipefail` aborts on the
# first failed command, on an unset variable, and on a failure inside a pipeline —
# like letting exceptions propagate instead of being swallowed.
#
# Usage:
#   scripts/7-setup_i2c.sh            # install + enable + print next steps
#   scripts/7-setup_i2c.sh --verify   # skip changes; only show the bus and devices
# ---------------------------------------------------------------------------

set -euo pipefail

# --- Constants (edit only with an INV-075 change + CHANGELOG entry) ----------
SDA_GPIO=2       # I2C1 SDA = physical pin 3
SCL_GPIO=3       # I2C1 SCL = physical pin 5
XSHUT_GPIO=17    # front sensor XSHUT = physical pin 11 (active-low reset)
I2C_BUS=1        # /dev/i2c-1 on the 40-pin header
FRONT_ADDR=0x29  # power-on default address (single sensor: no reassignment)

# --- Options -----------------------------------------------------------------
VERIFY_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --verify) VERIFY_ONLY=1 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
  esac
done

# `sudo` only when not already root.
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

print_map() {
  cat <<'EOF'

  Wiring (INV-075) — single FRONT VL53L0X
  ---------------------------------------------------------------------------
    VIN    -> pin 1  (3V3)
    GND    -> pin 6  (GND)
    SDA    -> pin 3  (GPIO2)
    SCL    -> pin 5  (GPIO3)
    XSHUT  -> pin 11 (GPIO17)   active-low reset; drive HIGH to run
    GPIO1  -> not connected     (optional interrupt; we poll)
    Address: 0x29 (default) — no reassignment needed for one sensor

  Verify (before writing any driver — INV-001):
    i2cdetect -y 1             # expect "29"
    i2cget -y 1 0x29 0xc0      # expect 0xee  (confirms VL53L0X)
    (Optional reset check) gpioset gpiochip0 17=0 ; gpioset gpiochip0 17=1
    Details and troubleshooting: docs/PROXIMITY.md
EOF
}

# --- Verify-only mode --------------------------------------------------------
if [ "$VERIFY_ONLY" -eq 1 ]; then
  echo "== I2C devices =="
  ls -l /dev/i2c-* 2>/dev/null || echo "  (none — I2C is not enabled yet)"
  if command -v i2cdetect >/dev/null 2>&1; then
    echo
    echo "== Bus $I2C_BUS =="
    i2cdetect -y "$I2C_BUS" || echo "  (scan failed — see docs/PROXIMITY.md troubleshooting)"
  else
    echo "i2c-tools not installed; run without --verify first."
  fi
  print_map
  exit 0
fi

# --- 1. Packages -------------------------------------------------------------
echo "==> Installing i2c-tools and gpiod (skips if already present)"
$SUDO apt-get update -y
$SUDO apt-get install -y i2c-tools gpiod

# --- 2. Enable I2C -----------------------------------------------------------
echo "==> Enabling I2C"
if command -v raspi-config >/dev/null 2>&1; then
  # nonint do_i2c 0 == enable (0 is the "on" argument; 1 would disable).
  $SUDO raspi-config nonint do_i2c 0 || true
fi

# Belt-and-braces: make sure config.txt carries the overlay line. Raspberry Pi OS
# trixie uses /boot/firmware/config.txt; older images use /boot/config.txt.
CFG=""
for candidate in /boot/firmware/config.txt /boot/config.txt; do
  if [ -f "$candidate" ]; then CFG="$candidate"; break; fi
done
if [ -n "$CFG" ]; then
  if grep -q '^dtparam=i2c_arm=on' "$CFG"; then
    echo "    $CFG already has dtparam=i2c_arm=on"
  else
    echo "    adding dtparam=i2c_arm=on to $CFG"
    echo 'dtparam=i2c_arm=on' | $SUDO tee -a "$CFG" >/dev/null
  fi
else
  echo "    WARNING: could not find config.txt; enable I2C manually (docs/PROXIMITY.md)" >&2
fi

# --- 3. Load the i2c-dev module at boot (usually already handled) ------------
echo "==> Ensuring the i2c-dev module loads at boot"
echo 'i2c-dev' | $SUDO tee /etc/modules-load.d/i2c.conf >/dev/null
$SUDO modprobe i2c-dev || true

# --- 4. Let this user use the bus without sudo -------------------------------
if [ "$(id -u)" -ne 0 ] && getent group i2c >/dev/null 2>&1; then
  echo "==> Adding $USER to the i2c group"
  $SUDO adduser "$USER" i2c || true
fi

# --- 5. Report ---------------------------------------------------------------
echo
echo "Done. A REBOOT is required for the I2C interface to appear."
echo "After rebooting, run:  scripts/7-setup_i2c.sh --verify"
print_map
