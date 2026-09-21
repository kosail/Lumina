# Companion app ecosystem (FR-11)

How the Lúmina runtime reports status to, and accepts commands from, the Android/desktop companion
app. The **public contract** is `docs/APP_PROTOCOL.md`; this file covers the internals, the
on-device probe results that shaped the panel, and the operations runbook.

INV-003 is preserved: the runtime makes **no network calls**. The only runtime change is a local
status file; a separate, opt-in **`lumina_agent`** process owns every socket.

---

## 1. Architecture

```
   PHONE / DESKTOP  (Compose Multiplatform, separate project)
      │  UDP 47600  subscribe + receive   │  TCP 47601 control (JSON + token)
      ▼                                    ▼
   Pi hotspot (wlan0; 2.4 GHz; no internet)  ◀── the app's gateway = the Pi
      lumina (C++ runtime)  ──writes──▶  /run/lumina/status   (~1 Hz, local only)
      lumina_agent (C++)    ──reads───▶  status + OS stats
                              ├─ broadcast + unicast to subscribers (UDP 47600)
                              └─ shells─▶ amixer -D bluealsa | systemctl | lumina_enroll
```

- The Pi is the hotspot (`sudo nmcli device wifi hotspot ifname wlan0 ssid ...`). The app always
  talks to its **gateway** (the Pi), so control binds that address and telemetry is unicast to
  subscribers. No laptop relay server.
- The agent is a separate binary from the runtime, built from the same CMake project, so it never
  shares threads or memory with the detection/speech path (INV-031/INV-050).

## 2. On-device probe results (2026-09-21)

| Item | Result | Consequence |
|---|---|---|
| Hotspot | `nmcli device wifi hotspot` works on the Pi (2.4 GHz) | Pi is the AP; accept the single-antenna/A2DP coexistence cost |
| Volume | `amixer -D bluealsa sset 'TWS A2DP' 100%` controls volume | agent discovers the control name dynamically (`scontrols`), never hardcodes it |
| Thermal | `/sys/class/thermal/thermal_zone0/temp` = `46160` m°C | read sysfs, divide by 1000 |
| Earbud battery | `bluetoothctl info` shows **no** `Battery1`/percentage | card **dropped** (not faked) |
| Camera day/night | `v4l2-ctl --list-ctrls` empty; no rpicam controls | no switch → `dayNight` derived from mean frame luminance |
| Camera | OV5647 IR-CUT, no software control | `luma >= 0.35` → `"day"`, else `"night"` |

## 3. Runtime side: the status file

`LUMINA_ENABLE_STATUS` (default ON) adds a low-priority thread that writes
`/run/lumina/status` (override with `LUMINA_STATUS_PATH`) atomically once per second. It is a
**local file only** — not telemetry, no sockets. `/run` is tmpfs, so there is no SD-card wear.

Format (one `key=value` per line; private interface between two of our own processes — the public
JSON lives in `docs/APP_PROTOCOL.md`):

```
running=1
uptime_s=62
fps=4.3
rss_mb=218.0
mean_luma=0.420
face_count=2
sink_ready=1
```

The agent treats the file as stale (runtime not reachable) when its mtime is older than ~5 s.

The shared `/run/lumina` directory is created by the agent setup script via
`/etc/tmpfiles.d/lumina.conf` (so it survives a runtime stop, which a `RuntimeDirectory=` would
not). Without it the status writer fails harmlessly (it logs once and the runtime keeps running).

## 4. Agent (`lumina_agent`)

| Responsibility | How |
|---|---|
| Telemetry | read status file + `/sys/class/thermal/thermal_zone0/temp` + `/proc/meminfo` + `/proc/loadavg`; broadcast the JSON and unicast it to subscribers (UDP 47600) @1 Hz |
| Subscriptions | listen on UDP 47600; a `{"t":"subscribe"}` datagram registers the sender for 10 s of unicast and gets an immediate `status` reply (bounded to 8 clients) |
| Volume | `amixer -D bluealsa`: discover control name, `sget` to read, `sset '<ctrl>' <n>%` / `mute` / `unmute`; the reply carries `ok` |
| People | names come from the runtime status file's one-per-line `person=` entries (the runtime knows the enrolled store), surfaced in the telemetry `people` array |
| Runtime control | `systemctl stop/start lumina` (narrow sudoers) |
| Enrollment (camera) | stop runtime → `lumina_enroll --camera --frames <1..10>` as user `lumina` → **always** start runtime; stream `captured N/10` |
| Enrollment (images) | decode + stage frames on disk (caps: 12 images / 8 MiB) → `lumina_enroll --image ...`; runtime keeps running unless `LUMINA_AGENT_ENROLL_STOP_RUNTIME=1` |
| Shutdown | `stop()` cancels any in-flight enrollment so the service stops promptly, then the orchestrator still restarts the runtime |

Processes are launched with `posix_spawnp` (multi-thread-safe), and every socket is closed-on-exec so
children never inherit the agent's descriptors.
| Control server | token-gated, newline JSON, binds the hotspot interface |

The enrollment tool runs as the **`lumina` user** so `models/face/embeddings.bin` stays owned and
readable by the runtime (running it as root would break the next runtime load).

## 5. Install / run

```bash
# On the Pi, from the repo checkout (aarch64 build already present in ~/lumina):
scripts/10-setup_agent.sh            # installs the unit + narrow sudoers + generates the token
systemctl --user ... # (no: it is a system unit, see below)

sudo systemctl enable --now lumina-agent.service
sudo systemctl status lumina-agent.service
journalctl -u lumina-agent -b -f
```

The script prints the token; configure it in the app. The service is a **system** unit because it
must `systemctl stop/start lumina`; it runs as user `lumina` and is allowed exactly those two
commands via `/etc/sudoers.d/lumina-agent`.

## 6. Troubleshooting

| Symptom | Check |
|---|---|
| App shows "Sin conexión" | `systemctl status lumina-agent`; `ss -lunp \| grep 47600`; phone on the Pi hotspot |
| Volume command fails | `amixer -D bluealsa scontrols` (control name changes per earbud); earbuds connected? |
| `enroll.error exitCode 2` | no face captured within 60 s — lighting/distance/framing |
| Runtime not restarted after enroll | agent restarts it in `finally`; if it reports `runtime:"absent"`, call `runtime.start` |
| `unauthorized` | token mismatch between `~/lumina/agent.token` and the app |
| Telemetry stale (`reachable:false`) | runtime stopped; `/run/lumina/status` mtime older than 5 s |

## 7. Related

- `docs/APP_PROTOCOL.md` — the frozen wire contract.
- `docs/FACE.md` — enrollment tool details.
- `docs/PERFORMANCE.md` — the AP-vs-A2DP cost measurement (pending).
