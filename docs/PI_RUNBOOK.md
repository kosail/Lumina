# PI_RUNBOOK.md — bring up a real Lúmina device (runtime + agent + hotspot)

Operational spine for turning a fresh Raspberry Pi Zero 2 W into a working Lúmina device that a
companion app can see and control. It is the **end-to-end order of operations**; each step links to
the deep document instead of repeating it.

Related documents (do not duplicate; follow the links):

| Topic | Document |
|-------|----------|
| Highest source of truth | [`../INVARIANTS.md`](../INVARIANTS.md) |
| Build, sysroot, cross toolchain, first-timer walkthrough | [`CROSS_COMPILE.md`](CROSS_COMPILE.md) |
| Companion architecture, agent internals, on-device probes | [`COMPANION.md`](COMPANION.md) |
| Companion wire contract (schemas, commands, client notes) | [`API_CONTRACT.md`](API_CONTRACT.md) |
| Bluetooth earbuds, sink watchdog | [`BLUETOOTH.md`](BLUETOOTH.md) |
| Face enrollment tool | [`FACE.md`](FACE.md) |
| Proximity wiring (optional) | [`PROXIMITY.md`](PROXIMITY.md) |

## 0. Mental model (why there are two processes)

The runtime (`lumina`, C++) is **network-free** (INV-003): it only writes the local status file
`/run/lumina/status` at ~1 Hz. A separate, opt-in process (`lumina_agent`, C++) owns **every**
socket — the UDP status broadcast (`:47600`) and the token-gated TCP control channel (`:47601`).
Nothing in the app talks to the runtime directly; it only talks to the agent.

```
   PHONE / DESKTOP  (Compose Multiplatform client, separate project)
      |  UDP 47600 subscribe + receive        |  TCP 47601 control (JSON + token)
      v                                       v
   Pi hotspot (wlan0; no internet)  <-- the app's gateway is the Pi itself
      lumina        --writes--> /run/lumina/status      (~1 Hz, local file only)
      lumina_agent  --reads---  status + OS stats
                    --serves--  UDP broadcast/unicast + TCP control
                    --shells--  amixer -D bluealsa | systemctl | lumina_enroll
```

Because of this split, "setting up the Pi server" is three independent pieces: the **runtime**
service, the **agent** service, and the **Wi-Fi hotspot** the phone joins. The runtime half is what
you already ran by hand; the agent and the hotspot are what make the app work.

---

## 1. Prerequisites

- Raspberry Pi Zero 2 W on Raspberry Pi OS **Lite 64-bit** (Debian 13 "trixie"), headless
  (INV-021/INV-024). Reachable over SSH from the laptop.
- Camera (OV5647) and the bone-conduction earbuds (single A2DP sink).
- Laptop cross toolchain + `cmake/rpi-sysroot` (see `CROSS_COMPILE.md`).
- Models fetched into the repo (`scripts/3-export_models.sh`, `scripts/4-fetch_onnxruntime.sh`,
  `scripts/5-build_libpiper.sh`, `scripts/6-fetch_voices.sh`, `scripts/8-fetch_face_models.sh`).

`$LUMINA_HOME` below means the deploy directory that holds the binaries and `models/`; the setup
scripts default it to the service user's home, i.e. **`~/lumina`**. The systemd units use it as
`WorkingDirectory=` and `ExecStart=`.

---

## 2. Build on the laptop (not on the Pi)

The `aarch64` preset keeps the hardware paths **off** by default, so enable the ones you need —
`LUMINA_BUILD_AGENT` and `LUMINA_ENABLE_STATUS` are on by default and produce `lumina_agent`:

```bash
cmake --preset aarch64 \
  -DLUMINA_ENABLE_FACE=ON -DLUMINA_ENABLE_NCNN=ON \
  -DLUMINA_ENABLE_AUDIO=ON -DLUMINA_ENABLE_LIBCAMERA=ON
cmake --build --preset aarch64
# produces: build/aarch64/lumina, build/aarch64/lumina_agent, build/aarch64/lumina_enroll
```

Add `-DLUMINA_ENABLE_PROXIMITY=ON` only if the front VL53L0X is wired (`docs/PROXIMITY.md`).
`lumina_enroll` is built only when `LUMINA_ENABLE_FACE=ON`. Full walkthrough: `CROSS_COMPILE.md`.

---

## 3. Deploy to `$LUMINA_HOME` (`~/lumina`)

Build on the laptop, never natively on the 512 MB board (INV-023). Copy the artifacts the two unit
files expect:

| Artifact | Destination | Needed by |
|----------|-------------|-----------|
| `build/aarch64/lumina` | `~/lumina/lumina` | `lumina.service` |
| `build/aarch64/lumina_agent` | `~/lumina/lumina_agent` | `lumina-agent.service` |
| `build/aarch64/lumina_enroll` | `~/lumina/lumina_enroll` | agent enrollment (FACE only) |
| `models/` (`yolo11n_ncnn_320x256/`, `face/`, `voices/`) | `~/lumina/models/` | runtime + agent |
| `third_party/libpiper/lib/` | `~/lumina/third_party/libpiper/lib/` | `LD_LIBRARY_PATH` |
| compiled `espeak-ng-data/` (from `scripts/5-build_libpiper.sh`) | `~/lumina/espeak-ng-data/` | runtime TTS (3rd CLI arg) |
| `scripts/` (and optionally `docs/`) | `~/lumina/scripts/` | the setup scripts run on the Pi |

```bash
scp build/aarch64/lumina build/aarch64/lumina_agent build/aarch64/lumina_enroll pi@<pi-host>:~/
# plus models/, third_party/libpiper/lib/, espeak-ng-data/ (rsync is friendlier for trees)
ssh pi@<pi-host> 'chmod +x ~/lumina ~/lumina_agent ~/lumina_enroll && ldd ~/lumina'  # no "not found"
```

The runtime invocation baked into `lumina.service` is:
`~/lumina models/yolo11n_ncnn_320x256 models/voices/es_MX-claude-high.onnx espeak-ng-data`.

---

## 4. Pi OS preparation

Groups (the unit's `User=` picks up supplementary groups, which is how the camera, I²C and ALSA are
reached without root): the service user must be in `video`, `i2c`, `audio`.

Memory tuning for the 512 MB board (recommended, from `README.md` §"Memory on the Pi Zero 2 W"):
`gpu_mem=32` in `/boot/firmware/config.txt`; zram instead of SD swap; the `vm.*` sysctl drop-in;
disable unneeded services but keep `bluetooth`.

```bash
sudo usermod -aG video,i2c,audio "$USER"        # re-login for it to take effect
sudo apt install -y libcamera-dev libopencv-dev # dev packages only needed for the sysroot sync
```

---

## 5. Pair the earbuds (before starting the runtime)

```bash
scripts/bt_setup.sh
```

The runtime waits up to 3 minutes for the BlueALSA sink at startup; if it never appears it powers the
device off (or stops the runtime where power-off is not permitted). Power the earbuds on before the
Pi to avoid the wait. Details: `BLUETOOTH.md`.

---

## 6. Wi-Fi hotspot (create this BEFORE the agent setup)

The Pi is the access point and the client's **gateway**. Create the hotspot first: `10-setup_agent.sh`
(in §8) reads `wlan0`'s IPv4 address at install time and bakes it into the agent unit as
`LUMINA_AGENT_BIND_ADDR`.

```bash
# 1) Create + activate the hotspot (official nmcli(1) form:
#    ifname / con-name / ssid / band / channel / password).
sudo nmcli device wifi hotspot ifname wlan0 con-name lumina-hotspot \
  ssid "Lumina" password "<8-63 char WPA passphrase>"
```

**Pin the AP address to `10.42.0.1/24` (recommended).** The `device wifi hotspot` subcommand has
**no IP argument** — its synopsis stops at `password` — so the address is set on the profile it
created. That profile uses `ipv4.method=shared`, and NetworkManager assigns `10.42.x.1/24` **only if
`ipv4.addresses` is unset**; setting the address therefore pins it:

```bash
# 2) Pin the AP address, then re-activate so it takes effect.
sudo nmcli connection modify lumina-hotspot ipv4.addresses 10.42.0.1/24
sudo nmcli connection up lumina-hotspot

# 3) Optional: start it on every boot, ahead of other networks.
sudo nmcli connection modify lumina-hotspot connection.autoconnect yes connection.autoconnect-priority 100

# 4) Confirm what the AP actually got — the app must use THIS address.
nmcli -g IP4.ADDRESS,IP4.GATEWAY device show wlan0       # expect 10.42.0.1/24
```

Fully declarative alternative (address pinned from creation; useful for scripting/provisioning):

```bash
sudo nmcli connection add type wifi ifname wlan0 con-name lumina-hotspot ssid "Lumina" \
  802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 7 \
  wifi-sec.key-mgmt wpa-psk wifi-sec.psk "<8-63 char WPA passphrase>" \
  ipv4.method shared ipv4.addresses 10.42.0.1/24 \
  connection.autoconnect yes connection.autoconnect-priority 100
sudo nmcli connection up lumina-hotspot
```

- Pinning `10.42.0.1/24` makes the Android app's default Host always correct. Without it the third
  octet is not guaranteed: the official text documents only `10.42.x.1/24` (we observed `10.42.0.1`,
  `COMPANION.md` §2). The `ipv4.addresses` property is aliased `ip4`.
- Make sure no other interface already holds `10.42.0.0/24`; NetworkManager avoids an address
  conflict by choosing a different subnet.
- We do **not** want internet sharing. `shared` enables forwarding/NAT automatically, but with no
  upstream connection there is nothing to share; the client only needs the Pi on-link. The hotspot
  has no internet and the app requires none.
- Changing the AP address later means re-running `10-setup_agent.sh` so the agent rebinds.

---

## 7. Install the runtime service

```bash
scripts/9-setup_autostart.sh --start   # installs+enables lumina.service
```

Installs `/etc/systemd/system/lumina.service` and a narrow `/etc/sudoers.d/lumina-poweroff` rule
(only `systemctl poweroff`, used by FR-06.1). Exit code 2 is the deliberate "no audio sink" result
and is not restart-looped.

This script is **independent of the hotspot**: it substitutes only the user/home and never reads an
IP, so its order relative to §6/§8 does not matter and it does not need re-running when the AP address
changes.

## 8. Install the agent service

```bash
scripts/10-setup_agent.sh --start
```

This, in order: creates `/run/lumina` via `/etc/tmpfiles.d/lumina.conf` (so it survives a runtime
stop); generates `~/lumina/agent.token` (32 hex, mode `0600`) if absent; installs a narrow
`/etc/sudoers.d/lumina-agent` rule allowing only `systemctl stop/start lumina`; renders and installs
`/etc/systemd/system/lumina-agent.service` with the detected bind/broadcast addresses; then enables
and starts it. It **prints the token** at the end — that is the value you paste into the app.

Because it **detects `wlan0`'s IPv4 at install time and bakes the bind/broadcast addresses into the
unit** (systemd reads them only at service start), run this **after §6**, and **re-run it with
`--start` if the AP address later changes**. Re-running is safe: the token is left unchanged, so the
app keeps working. If you run it before the hotspot exists it falls back to `0.0.0.0` /
`255.255.255.255`; with the address pinned in §6 the AP address is stable, so this normally runs once.

At boot the agent now **retries the control bind for up to ~60 s** (`LUMINA_AGENT_BIND_RETRIES` ×
`LUMINA_AGENT_BIND_RETRY_MS`), so it no longer matters whether the hotspot gateway address exists
before the service starts (CHG-0094); `Restart=on-failure` remains as a backstop.

```bash
cat ~/lumina/agent.token        # same token, if you need it again
```

---

## 9. Verify

```bash
systemctl status lumina.service lumina-agent.service
journalctl -u lumina-agent -b -f          # agent log
ss -lunp | grep 47600                     # UDP telemetry listening
ss -ltnp | grep 47601                     # TCP control listening
cat /run/lumina/status                    # running=1, fps=..., face_count=... (local file)
```

`/run/lumina/status` is the private runtime→agent handoff; the app must never read it. If the agent
reports `reachable:false`, the file's mtime is older than ~5 s (runtime stopped).

---

## 9b. Pre-show checklist (showcase day)

Run this on the **exact demo unit** shortly before the presentation.

```bash
# 1. Models + phrase cache are hot (needs no earbuds or camera). Expect exit 0 and
#    "cache '...' ready"; a "newly rendered" of 0 means it was already complete.
#    Exit 1 means the cache directory is not writable (fix before the show).
cd ~/lumina && LUMINA_WARM_ONLY=1 ./lumina \
  models/yolo11n_ncnn_320x256 models/voices/es_MX-claude-high.onnx espeak-ng-data

# 2. Both services are up.
systemctl is-active lumina.service lumina-agent.service

# 3. The hotspot address is the one the app uses (default 10.42.0.1).
ip -4 addr show wlan0 | grep inet

# 4. Earbuds are connected and the sink opened.
amixer -D bluealsa scontrols                 # prints the A2DP control name
journalctl -u lumina -b | grep -i 'audio sink ready'
```

- The deployed `lumina.service` sets `LUMINA_AUDIO_SHUTDOWN_ON_FAILURE=0` and
  `LUMINA_AUDIO_SINK_MAX_RETRIES=10000`, so a missing earbud makes the runtime **wait, not power
  the Pi off** (CHG-0098). Pair the earbuds before (or at) the Pi to avoid the wait.
- Confirm the app connects and shows the enrolled people (**Personas**) and the runtime controls.
- Optional soak (30–60 min): leave it running and watch `journalctl -u lumina -f` for
  `speech failed`, and `/run/lumina/status` for a stalled FPS.

---

## 10. Connect a client

1. Join the **Lumina** hotspot from the phone/laptop.
2. In the app's **Ajustes** (Settings): Host = the AP address from §6 (default `10.42.0.1`), UDP
   `47600`, TCP `47601`, and the token from §8 — then **Guardar**.
3. The app subscribes over UDP (`{"t":"subscribe"}`, re-sent every 5 s) and sends token-gated control
   commands over TCP. Full schemas and the worked end-to-end transcript are in `API_CONTRACT.md`
   (§4, §5, §11).

---

## 11. Enroll a person (optional, needs the face build)

Enrollment is issued by the app through the agent, which shells out to `lumina_enroll` as the
`lumina` user (so `models/face/embeddings.bin` stays runtime-readable):

- **Camera route** stops `lumina` first (libcamera is single-client), captures up to 10 frames, then
  **always** restarts the runtime. The call blocks its TCP connection — cancel on a second connection.
- **Images route** (phone photos) keeps the runtime running unless
  `LUMINA_AGENT_ENROLL_STOP_RUNTIME=1`.

Caps and the state machine: `API_CONTRACT.md` §6. Manual tool usage and tips: `FACE.md`.

---

## 12. Operations and troubleshooting

| Symptom | Check |
|---------|-------|
| App shows "Sin conexión" | `systemctl status lumina-agent`; `ss -lunp \| grep 47600`; phone on the Lumina hotspot |
| Control commands rejected (`unauthorized`) | token mismatch: compare `~/lumina/agent.token` with the app's Token |
| Volume command fails | `amixer -D bluealsa scontrols` (the control name varies per earbud); earbuds connected? |
| Runtime not reachable (`reachable:false`) | runtime stopped, or `/run/lumina/status` older than 5 s |
| Runtime not restarted after enroll | the agent restarts it; if it reports `runtime:"absent"`, send `runtime.start` |
| Camera enroll `error exitCode 2` | no frame captured within 60 s — lighting/distance/framing |
| `lumina` exits at boot | no BlueALSA sink within the retry budget (FR-06.1) — pair first (§5); exit code 2 is intentional. With the demo unit env (CHG-0098) it waits ~8.3 h instead of exiting |
| Cross-build link errors | see `CROSS_COMPILE.md` §C2/C3 (sysroot and the Arch `--sysroot` gotcha) |

## 13. Stop / rotate

```bash
# Stop the app-facing side and the runtime.
sudo systemctl disable --now lumina-agent.service lumina.service

# Rotate the control token: remove the old file first, then re-run the agent setup.
rm ~/lumina/agent.token && scripts/10-setup_agent.sh --start   # prints the new token
# then update the token in the app.
```

To fully uninstall: remove `/etc/systemd/system/lumina*.service`, `/etc/sudoers.d/lumina-*`,
`/etc/tmpfiles.d/lumina.conf`, then `sudo systemctl daemon-reload`.

---

## Appendix A — installed files

| Path | Installed by | Purpose |
|------|--------------|---------|
| `~/lumina/` | you (§3) | `$LUMINA_HOME`: binaries, `models/`, `third_party/libpiper/lib`, `espeak-ng-data` |
| `/etc/systemd/system/lumina.service` | `9-setup_autostart.sh` | runtime autostart |
| `/etc/systemd/system/lumina-agent.service` | `10-setup_agent.sh` | agent autostart |
| `/etc/sudoers.d/lumina-poweroff` | `9-setup_autostart.sh` | allows only `systemctl poweroff` |
| `/etc/sudoers.d/lumina-agent` | `10-setup_agent.sh` | allows only `systemctl stop/start lumina` |
| `/etc/tmpfiles.d/lumina.conf` | `10-setup_agent.sh` | creates `/run/lumina` (0755, service user) |
| `~/lumina/agent.token` | `10-setup_agent.sh` | shared control secret (0600) |
| `/run/lumina/status` | runtime | runtime→agent status file (`key=value`, ~1 Hz) |

## Appendix B — sources

Facts about NetworkManager/hotspot behavior were checked against the official sources below
(accessed **2026-09-22**):

- NetworkManager `nmcli` reference — `nmcli device wifi hotspot` synopsis and flags (no IP argument);
  `ipv4.method=shared` auto-assigns `10.42.x.1/24` **only when `ipv4.addresses` is unset**; `shared`
  always enables forwarding (no manual NAT); `ipv4.addresses` (alias `ip4`); `connection.autoconnect`
  / `connection.autoconnect-priority`:
  <https://networkmanager.dev/docs/api/latest/nmcli.html> and
  <https://networkmanager.dev/docs/api/latest/nm-settings-nmcli.html>.
- Raspberry Pi official tutorial "Host a hotel Wi-Fi hotspot" — the `nmcli device wifi hotspot` example
  and enabling autoconnect so the hotspot starts on boot:
  <https://www.raspberrypi.com/tutorials/host-a-hotel-wifi-hotspot/>.

The `10.42.0.1` gateway we use is the value observed on our own device (`COMPANION.md` §2). The
official text documents only the `10.42.x.1/24` shape, so §6 **pins** the address with
`ipv4.addresses` instead of assuming the third octet; still confirm with `nmcli device show wlan0`.
