# Lúmina — Bluetooth Audio (single A2DP earbuds)

This guide covers the Bluetooth layer of the beta runtime: how the single
bone-conduction earbud is set up on the Pi, and how the runtime behaves when it
is (or is not) available. Read with `INVARIANTS.md` (INV-014, INV-053),
`SPECS.md` (FR-06) and `RAW_PLAN.md` §9.

> **Invariant:** exactly **one** Bluetooth audio device is ever connected, and it
> is used as an **A2DP sink** via `bluealsa`. The runtime never manages another
> device, and it **never hardcodes a MAC address** (INV-014).

---

## 1. The two layers

| Layer | What it does | Where |
|---|---|---|
| **System** | Pair + trust the earbud; auto-enable the adapter; optional boot reconnect | `scripts/bt_setup.sh` |
| **Runtime** | Route TTS to the `bluealsa` PCM; wait/retry if the sink is absent; power off if it never appears | `src/audio/bluealsa_sink.*`, `src/audio/sink_watchdog.*`, `src/core/power.*` |

They are deliberately independent: the runtime does not care *which* device is
connected, only that a `bluealsa` PCM exists.

---

## 2. Why the runtime used to fail, and what changed

`bluealsa` exposes the ALSA PCM named `bluealsa` **only while a Bluetooth audio
device is connected**. On a cold boot the earbud may connect a few seconds after
the runtime starts, so opening the PCM could fail. Previously that made
`Pipeline::start()` return false and the process exit non-zero — the runtime
"crashed" at startup whenever the earbuds were not ready.

Now (`FR-06.1`):

1. `main` installs its SIGINT/SIGTERM handlers, then runs a **sink watchdog**
   before starting the pipeline.
2. The watchdog calls `IAudioSink::open()` every `audioSinkRetryIntervalMs`
   (default **3000 ms**) up to `audioSinkMaxRetries` times (default **60** →
   **3 minutes**), logging each failed attempt.
3. If the sink opens, the runtime continues normally.
4. If the budget is exhausted and `audioSinkShutdownOnFailure` is true, the
   runtime asks the OS to **power the device off** (`systemctl poweroff`). If it
   is not permitted, it logs and **stops the runtime** instead (exit code 2).
5. A SIGINT/SIGTERM during the wait stops cleanly (exit code 0).

The tunables live in `core::Config` (`audioSinkRetryIntervalMs`,
`audioSinkMaxRetries`, `audioSinkShutdownOnFailure`) with clamp/validation in
`src/core/config.hpp`.

> **History — CHG-0079 (important).** An earlier revision of `SinkWatchdog` read its `running`
> argument with inverted polarity (it treated `true` = "keep running" as "stop requested"). Because
> `main` passes the process-wide `g_running` (which is `true` while the runtime runs), the very
> first check returned `Interrupted` before any `open()`, and `main` mapped that to exit code 0:
> the runtime exited silently on every launch, with no ALSA open, no retries and no power-off.
> The flag is now named `running` and the code checks `!running`; `tests/test_sink_watchdog.cpp`
> asserts the correct polarity so this regression is guarded.

### Verified on-device (CHG-0081)

- **Earbuds connected:** `audio sink wait: up to 60 attempt(s), 3000 ms apart` →
  `ALSA sink ready: pcm='bluealsa' 22050 Hz 1 ch float32` → `audio sink ready (attempt 1/60)`,
  then the proximity/camera logs and `Lúmina running`. Detections, Spanish speech and the
  `Priority::Safety` proximity phrase all work (the ToF alert showed `event→speech-start 147 ms`).
- **Earbuds disconnected:** `audio sink unavailable (attempt n/60); retrying in 3000 ms` for all
  60 attempts (180 s), then `audio sink unavailable after 60 attempts (180 s)` →
  `requesting power-off: sudo -n systemctl poweroff --no-wall` → the Pi powered off. This also
  confirms the sudoers rule in "Power-off permission" below is installed correctly.
- **Reboot:** `lumina.service` autostarts the runtime; with the earbuds connected it comes up and
  runs end-to-end.
- **SIGINT/SIGTERM during the wait:** logged as `audio sink wait interrupted by signal N` (exit 0).
- **Cold boot (CHG-0082):** with the earbuds not yet reconnected, the runtime polled the `bluealsa`
  PCM every 3 s and succeeded on **attempt 8/60** (~21.5 s after the wait began, at monotonic
  t=60.3 s); the A2DP sink appeared the moment the earbuds reconnected (`dev_41_42_27_16_4A_5F`).
  The watchdog covered the gap with no runtime failure. **Demo tip:** power the earbuds on before
  (or at) the Pi so this wait is avoided (ready ~40 s instead of ~62 s; see `docs/PERFORMANCE.md`
  section 14.4).

### Power-off permission

When the runtime is autostarted by `lumina.service` it runs as root, so
`systemctl poweroff` works directly. For **manual** runs the installer adds a
narrow rule:

```
/etc/sudoers.d/lumina-poweroff
<user> ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff, /usr/bin/systemctl poweroff --no-wall
```

Only `systemctl poweroff` is allowed — nothing else. The runtime invokes it with
`sudo -n`, which fails fast (instead of prompting) when the rule is missing.

---

## 3. System setup — `scripts/bt_setup.sh`

Run **on the Pi**, safe to repeat.

```bash
scripts/bt_setup.sh                       # discover + trust the single paired earbud
scripts/bt_setup.sh --pair                # first-time: scan/pair, then trust
scripts/bt_setup.sh --mac AA:BB:CC:DD:EE:FF
scripts/bt_setup.sh --with-connect-unit   # also install a boot reconnect unit
scripts/bt_setup.sh --verify              # show current state, change nothing
```

What it does:

1. Installs `bluez`, `alsa-utils` and the BlueALSA package (`bluealsa` or
   `bluealsad`, whichever the image ships).
2. Enables/starts `bluetooth.service` and unblocks the radio with `rfkill`.
3. Ensures `[Policy] AutoEnable=true` in `/etc/bluetooth/main.conf` (BlueZ
   defaults to true; the script only adds the line if missing and keeps a
   `.lumina.bak` backup).
4. Discovers the **single** paired device (`bluetoothctl devices Paired`) — or
   uses `--mac` / pairs with `--pair` — and marks it **trusted**, which is what
   lets it reconnect without re-pairing. If more than one device is paired it
   stops and asks for `--mac` (INV-014).
5. With `--with-connect-unit`, installs `lumina-bt-connect.service`, a best-effort
   oneshot that calls `bluetoothctl connect <MAC>` for ~60 s at boot. The runtime
   retries on its own, so this is optional belt-and-braces.

> The Pi already auto-reconnects to the trusted earbud when the case opens; the
> connect unit mainly helps a cold boot where the runtime starts first.

### Disabling suspend-on-idle

Pure BlueALSA has no suspend-on-idle. If the image also runs PipeWire/WirePlumber
(check `systemctl is-active wireplumber`), disable the headset auto-switch to
avoid the first-word delay:

```bash
wpctl settings --save bluetooth.autoswitch-to-headset-profile false
```

---

## 4. Autostart — `scripts/9-setup_autostart.sh` + `scripts/lumina.service`

```bash
scripts/9-setup_autostart.sh              # install + enable for the current user
scripts/9-setup_autostart.sh --start      # ...and start it now
scripts/9-setup_autostart.sh --verify     # show unit + sudoers + groups
```

- `lumina.service` is a **template**; the installer substitutes the user and home
  directory (default `/home/lumina`), writes `/etc/systemd/system/lumina.service`,
  validates and installs the sudoers rule, then `daemon-reload` + `enable`.
- The unit is ordered `After=bluetooth.service bluealsa.service` but does **not**
  require an already-connected earbud — the runtime waits for it.
- `Restart=on-failure` restarts on a real crash; `RestartPreventExitStatus=2`
  stops the deliberate "no audio sink" exit from restart-looping.
- `User=` inherits the user's supplementary groups (`video`, `i2c`, `audio`), so
  the camera, ToF sensor and bluealsa are reachable without root.

Logs: `journalctl -u lumina.service -f`.

---

## 5. Routing (how audio reaches the earbuds)

`AlsaSink` opens the ALSA PCM `bluealsa` and streams mono float32 PCM at the
Piper voice rate (22050 Hz). BlueALSA forwards it to the connected A2DP sink. No
sound server (PulseAudio/PipeWire) sits in the path.

- Config: `AlsaConfig` in `src/audio/bluealsa_sink.hpp`.
- Optional per-device pinning would use `defaults.bluealsa.device "MAC"` in
  `~/.asoundrc`, but the beta deliberately does **not** pin a device (INV-014).
- If the voice sample rate changes, `AlsaSink::write()` reopens the PCM at the new
  rate so playback is not pitched.

---

## 6. Verify

```bash
# 1. Adapter and paired device
bluetoothctl show
bluetoothctl devices Paired
bluetoothctl info AA:BB:CC:DD:EE:FF     # Connected: yes; A2DP UUID present

# 2. BlueALSA sees a PCM only while connected
bluealsa-aplay -L

# 3. A test tone through the bluealsa PCM
speaker-test -D bluealsa -c 1 -t sine -l 1

# 4. Runtime autostart
systemctl is-enabled lumina.service
journalctl -u lumina.service -f
```

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `ALSA: snd_pcm_open('bluealsa') failed` | No earbud connected yet. The runtime retries for 3 min; connect the buds (open the case). `bluealsa-aplay -L` should then list a PCM. |
| Runtime stops with exit code 2 | Sink never appeared within 3 min and power-off was requested. Check `journalctl -u bluetooth`, the earbud battery, and `bluetoothctl connect <MAC>`. |
| Runtime keeps powering the Pi off at boot | The earbuds are off / out of range. Either connect them before boot or raise the budget / disable power-off via the `core::Config` fields. |
| No sound although connected | `speaker-test -D bluealsa` and `bluealsa-aplay -L`. If PipeWire is installed, disable headset auto-switch (§3). |
| Slow first word | Suspend-on-idle (PipeWire) or a cold codec negotiation; see §3 and `RAW_PLAN.md` §9. |
| `[Policy] AutoEnable` edit unwanted | Restore `/etc/bluetooth/main.conf.lumina.bak`. |

---

## 8. References (consulted per INV-001, 2026-09-21)

- BlueALSA (`bluealsa` PCM available when a device is connected; daemon must run;
  `bluealsa-aplay -L`): <https://github.com/arkq/bluez-alsa>
- Arch Wiki, *Bluetooth headset* (trust for auto-reconnect; disable
  suspend-on-idle): <https://wiki.archlinux.org/title/Bluetooth_headset>
- `bluetoothd(8)` / `/etc/bluetooth/main.conf` (BlueZ 5.82 on trixie):
  <https://manpages.debian.org/trixie/bluez/bluetoothd.8.en.html>
