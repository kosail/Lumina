# Lúmina companion protocol (FR-11)

**Status:** frozen, version `proto: 1` · **Owner:** runtime repo · **Consumers:** the Android and
desktop clients (Compose Multiplatform, built separately).

This is the contract between the **`lumina_agent`** process on the Pi and the app. The runtime
itself is network-free (INV-003); the agent owns all sockets. Implement the app against this file.

---

## 1. Topology

```
phone / desktop client  ──UDP:47600 (receive telemetry)──▶  Pi hotspot
                        ──TCP:47601 (send commands)──────▶  lumina_agent
```

- The **Pi is the Wi-Fi hotspot** (`ssid "lumine_et_terra"`; 2.4 GHz only). Clients join it and
  reach the Pi at its hotspot address (commonly `10.42.0.1`; discover it from the telemetry UDP
  source address).
- The hotspot has **no internet**; nothing here depends on one (INV-003).
- The agent binds TCP control to the hotspot interface and requires a shared **token**.

## 2. Telemetry (UDP 47600, broadcast, 1 Hz)

The agent broadcasts one JSON object per datagram. There is no request: clients just listen on
`0.0.0.0:47600`. **Liveness:** if no datagram arrives for ~5 s, show "Sin conexión".

```jsonc
{
  "t": "status",
  "proto": 1,
  "ts": 1690000000,              // agent epoch seconds (best effort; see RTC note)
  "runtime": {
    "reachable": true,           // false when the runtime status file is stale
    "running": true,
    "uptimeS": 62,
    "sink": "ready",             // ready | waiting | absent
    "faceCount": 2
  },
  "core": {
    "fps": 4.3,
    "rssMb": 218.0,              // runtime resident memory
    "memAvailableKb": 65536,     // system MemAvailable
    "tempC": 46.2,
    "load1": 1.80
  },
  "sensors": {
    "volume": 70,                // 0..100, or -1 when unknown
    "muted": false,
    "luma": 0.42,                // mean frame luminance 0..1, or -1 when unknown
    "dayNight": "day"            // "day" | "night" | "unknown"  (derived from luma)
  },
  "people": ["David Solís"],     // enrolled names, in insertion order
  "enroll": { "active": false }  // { "active": true, "phase": "...", "captured": n, "total": 10 }
}
```

Field notes:
- `runtime.reachable` is false when `/run/lumina/status` has not been updated for > 5 s (runtime
  stopped or crashed). The other `core` fields are still filled (they come from the OS).
- `sensors.dayNight` is a **derived** value (`luma >= 0.35` → `"day"`); the camera has no physical
  day/night control (recorded in `docs/COMPANION.md`).
- **RTC note:** the Pi has no real-time clock, so `ts` can jump once NTP corrects the wall clock
  shortly after boot. Use arrival time for ordering; never assume `ts` is monotonic.

## 3. Control (TCP 47601, newline-delimited JSON)

- Connect and send **one JSON object per line** (`\n` terminated).
- **Every request must carry `"token": "<string>"`.** A bad/missing token gets
  `{"t":"error","code":"unauthorized"}` and the connection is closed.
- The agent replies with one or more JSON lines (see each command). Synchronous commands reply
  once; enrollment streams events.

### 3.1 Commands

| `t` | Fields | Reply |
|---|---|---|
| `volume.get` | — | `{"t":"volume.state","value":70,"muted":false}` |
| `volume.set` | `value` (0..100) | `{"t":"volume.state",...}` |
| `volume.mute` | `value` (bool) | `{"t":"volume.state",...}` |
| `people.list` | — | `{"t":"people","names":["David Solís"]}` |
| `runtime.state` | — | `{"t":"runtime.state","running":true,"sink":"ready"}` |
| `runtime.start` | — | `{"t":"runtime.state",...}` — start `lumina.service` (recovery) |
| `runtime.stop` | — | `{"t":"runtime.state",...}` |
| `enroll.camera.start` | `name`, `frames` (default 10) | streams (see 3.2) |
| `enroll.camera.cancel` | — | `{"t":"enroll.cancelled"}` |
| `enroll.images` | `name`, `images` (array of base64 JPEG/PNG) | `{"t":"enroll.done",...}` or `{"t":"enroll.error",...}` |

Every reply echoes `"id"` when the request provides one, so a client may pipeline requests.

### 3.2 Enrollment event stream (`enroll.camera.*`)

The camera route **stops `lumina`** (libcamera is single-client), runs
`lumina_enroll --camera --frames <N>`, then **always restarts** `lumina` — success, failure or
cancel. Progress is parsed from the tool's own output.

```jsonc
{"t":"enroll.progress","phase":"stopping_runtime"}
{"t":"enroll.progress","phase":"capturing","captured":3,"total":10}
{"t":"enroll.progress","phase":"capturing","captured":3,"total":10,"message":"no face detected"}
{"t":"enroll.progress","phase":"starting_runtime"}
{"t":"enroll.done","ok":true,"personCount":2,"embeddingsAdded":10}
```

On failure the runtime is restarted **first**, then:

```jsonc
{"t":"enroll.error","exitCode":2,"message":"no face was captured; no data written","runtime":"started"}
```

The client should treat `runtime` as the authoritative state and offer **"Start Lúmina"** (i.e.
`runtime.start`) whenever it is not `"started"`.

The image route never stops the runtime: capture on the phone, send the frames, get a single
`enroll.done`/`enroll.error`.

### 3.3 Errors

```jsonc
{"t":"error","code":"unauthorized","message":"..."}   // bad token
{"t":"error","code":"bad_request","message":"..."}     // malformed/unknown command
{"t":"error","code":"internal","message":"..."}        // command failed
```

## 4. Security

- **Token:** a shared secret in `~/lumina/agent.token` (mode 0600) on the Pi; the app stores the
  same value. Provisioning is `scripts/10-setup_agent.sh`.
- The control port is reachable only from the hotspot subnet. Keep it that way; do not expose it.
- Face **names** travel over the LAN (telemetry `people`). That is the user's own device on their
  own hotspot; no data leaves the LAN (INV-003/INV-034).

## 5. Versioning

`proto` is the integer in every datagram/reply. Bump it only for breaking field changes; add new
optional fields freely (clients must ignore unknown keys).

## 6. Client implementation notes (Kotlin)

- Use **Ktor** (`ktor-network`) for UDP + TCP, or `expect/actual` over `java.net`. Common Compose
  Multiplatform code cannot use `java.net` directly.
- Receive UDP on `0.0.0.0:47600`; the sender address is the Pi — store it for the TCP connection.
- Read TCP lines with a buffered reader; split on `\n`; parse each line as JSON.
- Enroll UX: show a `captured/total` bar from `enroll.progress`, a clear failure state, and a
  **Start Lúmina** button bound to `runtime.start`.
