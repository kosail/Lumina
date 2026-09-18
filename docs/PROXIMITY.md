# Lúmina — Proximity Sensor (VL53L0X, front) Guide

> **Audience:** someone who has never wired a GPIO header. Nothing here is assumed; each
> step says what to do and how to check it.
>
> **Scope:** **one VL53L0X, mounted facing forward.** A second sensor is kept as a spare for a
> **rear** sensor that is **fully deferred** to the very end of the project — see the
> **Appendix**. The runtime driver is **Phase C (planned, after Day 4)** and is not implemented
> yet (`INVARIANTS.md` → INV-013, INV-033, INV-075; `CHANGELOG.md`).

---

## 1. What you have

- **2× VL53L0X time-of-flight distance sensors** (laser "ToF" ranging, ~30–2000 mm). **One is
  deployed in front; one is a spare.**
- Each board has pins labeled **VIN, GND, SCL, SDA, GPIO1, XSHUT** (your boards show 8; the two
  extra pins are usually a second `GND` plus `3V3`/`5V` — **identify them with a multimeter before
  trusting the silkscreen**).
- All units were **confirmed VL53L0X** on the Pi (model ID register `0xC0` → `0xEE`), so the
  confusing "VL53L0/1XV2" silkscreen does not matter (INV-001 satisfied).

**Why only one:** deploying two identical sensors requires resolving an I²C address collision,
which adds schedule risk. Front-only still delivers the safety alert; the rear is deferred.

---

## 2. Safety and ground rules

1. **Power off and unplug** the Pi before wiring.
2. **3.3 V logic.** The Pi's I²C pins are not 5 V tolerant; feed `VIN` from **3.3 V** unless the
   board clearly requires 5 V (verify — INV-001).
3. **Do not use GPIO0/GPIO1** (physical pins 27/28): reserved for HAT EEPROM.
4. The **camera ribbon (CSI)** is unaffected.

---

## 3. Enable I²C on the Raspberry Pi

> **Verified on this device (2026-09-18):** after `raspi-config` enabled I²C and the Pi was
> rebooted, `/dev/i2c-1` appeared and `i2cdetect -y 1` worked **without** the `i2c`-group or
> `modules-load.d` steps. Those steps are kept below as belt-and-braces for other images.

```sh
sudo apt update && sudo apt install -y i2c-tools gpiod
sudo raspi-config nonint do_i2c 0          # Interface Options -> I2C -> Yes
sudo reboot
```

Optional (only if `/dev/i2c-1` is missing or permission is denied):

```sh
echo i2c-dev | sudo tee /etc/modules-load.d/i2c.conf
sudo adduser "$USER" i2c                    # log out/in afterwards
```

Verify after reboot:

```sh
ls /dev/i2c-*        # must include /dev/i2c-1
i2cdetect -y 1       # lists devices on the bus
```

Assisted setup: `scripts/7-setup_i2c.sh` (and `scripts/7-setup_i2c.sh --verify`).

---

## 4. Wiring the front sensor

| Sensor signal | Connects to | Notes |
|---------------|-------------|-------|
| `VIN` | **pin 1** (`3V3`) | verify board rating (INV-001) |
| `GND` | **pin 6** (`GND`) | common ground |
| `SDA` | **pin 3** (`GPIO2`) | I²C1 data |
| `SCL` | **pin 5** (`GPIO3`) | I²C1 clock |
| `XSHUT` | **pin 11** (`GPIO17`) | reset line (active-low) |
| `GPIO1` | not connected | optional interrupt; we poll |

### How to read a pin number

Find **pin 1** first: it is the pad with the **square solder joint** at one end of the 40-pin
header (the other 39 pads are round). Pin 1 is **3.3 V (`3V3`)**; pin 2 (directly beside it, across
the header) is **5 V**. From there the numbers zig-zag — odd pins down one row, even pins down the
other, 1→40. If already soldered, confirm with a multimeter: **pin 1 to a GND pin reads ~3.3 V**
(never 5 V).

```
   Pi Zero 2 W                     VL53L0X (front)
   3V3  (1)  ───────────────────────  VIN
   GND  (6)  ───────────────────────  GND
   GPIO2/SDA (3) ───────────────────  SDA
   GPIO3/SCL (5) ───────────────────  SCL
   GPIO17 (11) ─────────────────────  XSHUT
   (GPIO1 not connected)
```

No two sensors share a pin now, so **no jumper "sharing" is needed**. (For the future rear
sensor, see the Appendix.)

---

## 5. Address and `XSHUT` (single sensor)

- The sensor answers at its power-on default, **`0x29`**. With only one sensor there is **no
  collision and no re-addressing**.
- `XSHUT` is an **active-low reset**: pull it LOW to hold the sensor in reset, HIGH (or floating,
  if the board has a pull-up) to run. We keep it on **GPIO17** so the driver can pulse it to
  recover a hung sensor; at boot the driver drives it HIGH.

Manual reset check (optional), with libgpiod. Note that `gpioset` only drives a line **while it
runs**, so keep it alive for the reset pulse (syntax differs between libgpiod v1 and v2 on trixie;
check `gpioset --help`):

```sh
gpioset gpiochip0 17=0 &        # start driving XSHUT LOW (reset)
sleep 0.002                     # hold LOW ~2 ms
kill %1 2>/dev/null || true     # release -> pull-up returns the line HIGH
gpioset gpiochip0 17=1          # (optional) explicitly drive HIGH; sensor boots at 0x29
```

---

## 6. Verify

```sh
i2cdetect -y 1                 # expect 29
i2cget -y 1 0x29 0xc0          # expect 0xee  -> confirmed VL53L0X
```

- `0xee` → good. Anything else / bus error → see §9 (it may be a VL53L1X or held in reset).
- **Verified on this hardware (2026-09-18):** all units returned `0xEE`.

If you leave `XSHUT` disconnected and the board lacks a pull-up, the sensor may stay in reset;
if so, drive GPIO17 high (or temporarily tie `XSHUT` to 3V3).

---

## 7. Software integration (Phase C — planned)

```
src/sensors/
  proximity.hpp                # IProximitySensor, NullProximitySensor, factory, ProximityReading
  vl53l0x_proximity.{hpp,cpp}  # i2c-dev (open/ioctl/read) + XSHUT (GPIO17) reset
```

- **Interface-first (INV-030):** the pipeline depends only on `IProximitySensor`. With
  `LUMINA_ENABLE_PROXIMITY=OFF` (host build) the factory returns `NullProximitySensor` and
  behavior is identical to the current beta (INV-033).
- **No new dependency (INV-022):** talk to `/dev/i2c-1` via `<linux/i2c-dev.h>` + `ioctl`, and to
  the GPIO character device for `XSHUT`.
- **Polling:** read the sensor a few times per second and expose the freshest reading.
- **Alert:** a front obstacle within threshold triggers a short Spanish proximity phrase through
  the arbiter (priority/cooldown/preemption, FR-10/INV-032).
- **Fusion:** `processing/distance` combines the true ToF metres (short range) with the
  bbox-size heuristic (long range / camera-only backup).

Config knobs to add: enable flag, bus path, address (`0x29`), threshold, poll rate.

---

## 8. Phase B checklist

> **Status: COMPLETE (verified 2026-09-18).** All items pass; the front sensor is ready for
> Phase C. Recorded in `CHANGELOG.md` (CHG-0040, CHG-0042).

- [x] 40-pin header present.
- [x] I²C enabled; `/dev/i2c-1` exists; `i2cdetect -y 1` works.
- [x] Chip confirmed VL53L0X (`0xc0` → `0xee`) on all units.
- [x] Front sensor wired per §4 (VIN/GND/SDA/SCL/XSHUT→GPIO17).
- [x] `i2cdetect -y 1` sees `0x29` with `XSHUT` released.
- [x] `XSHUT` reset verified via `gpioset`.
- [x] Wired front sensor verified — proceed to Phase C (after Day 4).

---

## 9. Troubleshooting

| Symptom | Likely cause / fix |
|---------|--------------------|
| No `0x29` | `XSHUT` held low, swapped SDA/SCL, no common GND, wrong `VIN`, I²C not enabled |
| `i2cget` returns `0x00` / bus error | sensor in reset; wrong register; bad wiring; not a VL53L0X |
| Permission denied on `/dev/i2c-1` | user not in the `i2c` group (log out/in) or run with `sudo` |
| Unstable at higher speeds | long wires / weak pull-ups; keep leads short, lower the bus clock |

---

## 10. Appendix — future rear sensor (DEFERRED)

> Do **not** wire this until every pending task, nice-to-have, and telemetry is done (INV-013,
> INV-040). It is recorded here so the dual-sensor details are not re-derived later.

The rear sensor will share I²C1 with the front one. Two identical VL53L0X both power up at
`0x29`, so they need **different addresses**.

**Reserved rear mapping (INV-075):** `XSHUT` → GPIO27 = pin 13, address `0x30`.

**Sharing the bus lines (`3V3`, `GND`, `SDA`, `SCL`).** "Sharing" just means making an
electrical node with three connections (Pi + front + rear). Options:

1. **Breadboard (best):** Pi pin 3 → one row; both sensors' `SDA` into that row; same for `SCL`.
2. **Y-splitter jumpers** (1 female → 2 female).
3. **Terminal block / Wago 221 lever nuts.**

**Address-assignment sequence at boot:**

1. Hold the **front** `XSHUT` LOW → only the rear is on the bus.
2. Assign the rear `0x30`: `i2cset -y 1 0x29 0x8a 0x30` (register `0x8a`; confirm against the ST
   API/datasheet before coding — INV-001).
3. Release the front (`XSHUT` HIGH) → front boots at `0x29`.
4. `i2cdetect -y 1` shows **both `0x29` and `0x30`**. The address is **not persistent**; repeat
   each boot (the driver will automate it).

Then front/rear obstacles can speak **distinct** Spanish phrases so the user knows the direction.

---

## 11. References (consulted per INV-001)

- ST **VL53L0X** product page and datasheet/API —
  https://www.st.com/en/imaging-and-photonics-solutions/vl53l0x.html (ToF ranging, device-address
  register, `XSHUT`/`GPIO1` behavior). Access date 2026-09-18.
- Raspberry Pi **GPIO pinout** — https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
  (physical pin numbering, 3.3 V logic, I²C1 on GPIO2/GPIO3). Access date 2026-09-18.
- Raspberry Pi **I²C** configuration (`raspi-config`, `dtparam=i2c_arm=on`). Access date 2026-09-18.
- `i2c-tools` (`i2cdetect`, `i2cget`, `i2cset`) and `libgpiod` (`gpioset`) man pages. Access
  date 2026-09-18.
