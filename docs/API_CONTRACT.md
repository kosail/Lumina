# API_CONTRACT.md — Lúmina companion API (machine contract)

<!--
AUDIENCE: AI AGENTS / code generators building the companion client. Not written for humans.
This file is the AUTHORITATIVE wire contract for FR-11. If it disagrees with the C++ code, the
code is right and you must report the discrepancy; do not silently adapt.
-->

```
id:            lumina.companion.api
proto_version: 1
status:        frozen
owner:         Lumina-BETA-RPI-2W (runtime repo)
consumers:     Android + desktop clients (Compose Multiplatform; built in a separate project)
mirrors:       src/agent/agent.cpp, src/agent/telemetry.cpp, src/agent/control.cpp, src/agent/udp.cpp
supersedes:    docs/APP_PROTOCOL.md (short human summary; this file is authoritative)
```

**What this is.** The complete description of how a client application talks to the Lúmina device.
For every interaction it states exactly **what the app SENDS** and **what the app RECEIVES**.
The device is a Raspberry Pi Zero 2 W acting as a Wi-Fi hotspot. The **runtime** (`lumina`,
C++) is network-free; a separate agent process (`lumina_agent`, C++) owns every socket. The app
never talks to the runtime directly — only to the agent.

**Legend.** `REQ` = client→device. `RSP` = device→client. `→` means "produces". All payloads are
UTF-8 JSON. All numbers are JSON numbers (not strings). `null` is a JSON null.

---

## 1. TL;DR (read this first, then the referenced sections)

1. The app joins the Pi hotspot and uses the **gateway address** (the Pi, commonly `10.42.0.1`).
2. **Telemetry (read):** send `{"t":"subscribe"}` to `<gateway>:47600/udp`. The device replies with a
   `status` object and then unicasts one `status` per second. Re-send `subscribe` every 5 s. If no
   `status` arrives for 5 s, treat the device as offline. (§2, §3.1, §4.1)
3. **Control (write):** open a TCP connection to `<gateway>:47601`, send **one JSON object per line**
   (`\n`-terminated), and put `"token":"<shared secret>"` in **every** request. Read replies line by
   line. (§3.2, §4.10, §5)
4. There is **no request `id`** and no multiplexing. Replies are one line each, in request order, per
   connection. A long `enroll.camera.start` **blocks its connection** until it finishes — send
   `enroll.camera.cancel` on a **second** connection. (§3.2, §5.8)
5. Values can be unknown: `sensors.volume == -1`, `sensors.luma == -1`, and
   `core.tempC/load1/memAvailableKb == null`. Render these as "—", never as `0`. (§4.1)
6. Never assume the device clock: `ts` can jump once after boot (no RTC). Use arrival time. (§4.1)

---

## 2. Constants / defaults

| Name | Value | Meaning |
|---|---|---|
| `UDP_TELEMETRY_PORT` | `47600` | UDP telemetry send + subscribe receive |
| `TCP_CONTROL_PORT` | `47601` | TCP control |
| `PROTO_VERSION` | `1` | Value of the `proto` field |
| `SUBSCRIBE_INTERVAL` | `5 s` (recommended) | How often the client re-sends `subscribe` |
| `SUBSCRIBER_TTL` | `10 s` | Device forgets a subscriber after this without re-subscribe |
| `MAX_SUBSCRIBERS` | `8` | Oldest subscriber is evicted when full |
| `LIVENESS_TIMEOUT` | `5 s` | Client-side: no `status` in this window ⇒ offline |
| `ENROLL_FRAMES_DEFAULT` | `10` | Default camera enrollment target |
| `ENROLL_FRAMES_MAX` | `10` | Requested `frames` is clamped to `[1, 10]` |
| `MAX_ENROLL_IMAGES` | `12` | `enroll.images` request cap (count) |
| `MAX_ENROLL_IMAGE_BYTES` | `8388608` (8 MiB) | `enroll.images` cap (total decoded bytes) |
| `MAX_LINE_BYTES` | `16777216` (16 MiB) | Max control line; fits 8 MiB of images as base64 |
| `DAYNIGHT_LUMA_THRESHOLD` | `0.35` | `luma >= 0.35` ⇒ `"day"`, else `"night"` |
| `STATUS_STALE_SECONDS` | `5` | Status file older than this ⇒ `runtime.reachable=false` |
| `TOKEN_FILE` | `$LUMINA_HOME/agent.token` | 32 hex chars, mode 0600 |
| `BIND_ENV` | `LUMINA_AGENT_BIND_ADDR` | TCP listen address (the gateway) |
| `BROADCAST_ENV` | `LUMINA_AGENT_BROADCAST` | UDP broadcast target (subnet broadcast) |
| `ENROLL_STOP_RUNTIME_ENV` | `LUMINA_AGENT_ENROLL_STOP_RUNTIME` | `1` stops runtime during image enroll |

**Not part of this contract:** the runtime writes an internal file `/run/lumina/status`
(`key=value`) that the agent reads. The app must never read it. Only the messages in this file are
supported.

---

## 3. Transport

### 3.1 UDP telemetry (`:47600`)

- The app may simply **listen** on `0.0.0.0:47600`; the device also broadcasts `status` to the
  hotspot subnet (best effort).
- **Reliable path (use this):** send one datagram to `<gateway>:47600`:

  ```
  {"t":"subscribe"}
  ```

  - Unauthenticated (no token). Telemetry is read-only.
  - The device replies **immediately** with one `status` datagram (unicast) and continues unicasting
    one `status` every ~1 s for 10 s. Re-send `subscribe` every 5 s to stay live.
  - The source address the client bound to determines the unicast destination; use a stable local
    port for the socket lifetime.
- One `status` object per datagram; datagram size is a few hundred bytes (well under any UDP MTU).
- IPv4 only. No TLS. The hotspot has **no internet**; nothing here requires one.

### 3.2 TCP control (`:47601`)

- Connect to `<gateway>:47601` (`TCP`, IPv4). The listener is bound to the hotspot gateway address.
- Framing: **one JSON object per line, terminated by a single `\n` (0x0A).** Do not send partial
  lines; do not send a JSON array.
- **Every request object MUST contain `"token":"<string>"`.** If the token is missing or wrong the
  device replies once with `{"t":"error","code":"unauthorized",...}` and **closes the connection**.
- Replies are one JSON object per line, in the order the requests were received on that connection.
- **No `id` field** is echoed; do not send one expecting it back. Correlate by order.
- Requests on one connection are processed **sequentially**. A command that runs long
  (`enroll.camera.start`, `enroll.images`) blocks that connection until it completes.
- The device never sends unsolicited data on the control channel.
- A single connection may issue any sequence of commands. Open a second connection if you need to
  cancel an in-progress enrollment (§5.8) or query status while an enrollment runs.
- The device drops a connection whose unparsed line exceeds 16 MiB (the limit fits
  the maximum image-enrollment payload; see §2 `MAX_LINE_BYTES`).

### 3.3 Reconnect / backoff

- On `unauthorized` the connection is closed by the device: fix the token, then reconnect.
- On any other read error / EOF: reconnect with backoff (suggested 1 s, 2 s, 5 s, capped at 10 s).
- The device may restart (`systemctl`); expect brief outages and re-establish both channels.

---

## 4. Data schemas

All schemas are JSON Schema draft 2020-12. `additionalProperties` is always `true` on the client
side: **ignore unknown keys** (forward compatibility).

### 4.1 `status` (UDP telemetry, and the subscribe reply)

```json
{
  "$id": "https://lumina/contract/status.json",
  "type": "object",
  "required": ["t", "proto", "ts", "runtime", "core", "sensors", "people", "enroll"],
  "properties": {
    "t":       { "const": "status" },
    "proto":   { "const": 1 },
    "ts":      { "type": "integer", "description": "Device epoch seconds (BEST EFFORT; may jump after NTP). Use arrival time for ordering." },
    "runtime": {
      "type": "object",
      "required": ["reachable", "running", "uptimeS", "sink", "faceCount"],
      "properties": {
        "reachable":    { "type": "boolean", "description": "false when the runtime status file is older than 5 s (runtime stopped/crashed)" },
        "running":      { "type": "boolean", "description": "the runtime's own status says it is running; false while initialize is still in progress" },
        "initializing": { "type": "boolean", "description": "additive/optional; true when lumina.service is active but has not yet reported a fresh running status (model load + BlueALSA sink wait can take ~18-60 s). Treat this as 'starting': render it instead of 'stopped' and disable start/stop until it clears." },
        "uptimeS":      { "type": "integer", "minimum": 0 },
        "sink":         { "type": "string", "enum": ["ready", "waiting", "absent"] },
        "faceCount":    { "type": "integer", "minimum": 0, "description": "number of people in the enrolled store; available even when the runtime is stopped" }
      }
    },
    "core": {
      "type": "object",
      "properties": {
        "fps":            { "type": "number", "description": "inference FPS, 1 decimal; 0.0 when unreachable" },
        "rssMb":          { "type": "number", "description": "runtime resident memory MiB, 1 decimal; 0.0 when unreachable" },
        "memAvailableKb": { "type": ["integer", "null"], "description": "system MemAvailable KiB; null when unknown" },
        "tempC":          { "type": ["number", "null"], "description": "SoC temperature °C, 1 decimal; null when unknown" },
        "load1":          { "type": ["number", "null"], "description": "1-min load average, 2 decimals; null when unknown" }
      }
    },
    "sensors": {
      "type": "object",
      "required": ["volume", "muted", "luma", "dayNight"],
      "properties": {
        "volume":   { "type": "integer", "description": "0..100, or -1 when the mixer is unknown" },
        "muted":    { "type": "boolean" },
        "luma":     { "type": "number", "description": "mean frame luminance 0..1, or -1 when unknown" },
        "dayNight": { "type": "string", "enum": ["day", "night", "unknown"], "description": "derived from luma; 'unknown' when luma < 0" }
      }
    },
    "people": {
      "type": "array", "items": { "type": "string" },
      "description": "enrolled names in insertion order, read from the device's persisted face store; available even when the runtime is stopped"
    },
    "enroll": {
      "oneOf": [
        { "type": "object", "required": ["active"], "properties": { "active": { "const": false } } },
        {
          "type": "object", "required": ["active", "phase", "captured", "total"],
          "properties": {
            "active":   { "const": true },
            "phase":    { "type": "string", "enum": ["stopping_runtime", "capturing", "starting_runtime"] },
            "captured": { "type": "integer", "minimum": 0 },
            "total":    { "type": "integer", "minimum": 1 }
          }
        }
      ]
    }
  }
}
```

`APP RECEIVES` example:

```json
{"t":"status","proto":1,"ts":1690000000,"runtime":{"reachable":true,"running":true,"initializing":false,"uptimeS":62,"sink":"ready","faceCount":2},"core":{"fps":4.3,"rssMb":218.0,"memAvailableKb":65536,"tempC":46.2,"load1":1.80},"sensors":{"volume":70,"muted":false,"luma":0.420,"dayNight":"day"},"people":["David Solís"],"enroll":{"active":false}}
```

Rules:
- **Unknown values:** `volume:-1`, `luma:-1.000` (⇒ `dayNight:"unknown"`), `tempC/load1/memAvailableKb:null`.
  Render as "—"; do not coerce to 0.
- When `runtime.reachable == false`: `running=false`, `uptimeS=0`, `sink="absent"`, `fps=0.0`,
  `rssMb=0.0`, `luma=-1.000`. `people`/`faceCount` still reflect the enrolled store (they do not
  require a running runtime). `initializing` is `true` while `lumina.service` is active but has not
  yet reported a fresh running status. The `core` OS fields (mem/temp/load) stay filled when readable.
- All strings are JSON-escaped; names may contain UTF-8 (e.g. `Solís`).

### 4.2 `volume.state` (reply to `volume.get/set/mute`)

```json
{
  "$id": "https://lumina/contract/volume-state.json",
  "type": "object",
  "required": ["t", "ok", "value", "muted"],
  "properties": {
    "t":     { "const": "volume.state" },
    "ok":    { "type": "boolean", "description": "true when the mixer command succeeded (always true for volume.get)" },
    "value": { "type": "integer", "description": "0..100, or -1 when unknown" },
    "muted": { "type": "boolean" }
  }
}
```

Example: `{"t":"volume.state","ok":true,"value":70,"muted":false}`

### 4.3 `people` (reply to `people.list`)

```json
{
  "$id": "https://lumina/contract/people.json",
  "type": "object", "required": ["t", "names"],
  "properties": { "t": { "const": "people" }, "names": { "type": "array", "items": { "type": "string" } } }
}
```

Example: `{"t":"people","names":["David Solís"]}`

> Note: `people.list` lists the enrolled store, so it returns the real registered names even when the
> runtime is stopped or `runtime.reachable` is false; it only falls back to the runtime's status file
> when the store cannot be read. Telemetry `people` is sourced the same way, so both agree.

### 4.4 `runtime.state` (reply to `runtime.state/start/stop`)

```json
{
  "$id": "https://lumina/contract/runtime-state.json",
  "type": "object", "required": ["t", "running", "sink"],
  "properties": {
    "t":            { "const": "runtime.state" },
    "running":      { "type": "boolean", "description": "`systemctl is-active lumina`; true as soon as the unit is active, before the runtime is ready" },
    "initializing": { "type": "boolean", "description": "additive; true when the unit is active but a fresh running status has not arrived yet. Older clients ignore unknown keys" },
    "sink":         { "type": "string", "enum": ["ready", "waiting", "absent"], "description": "only meaningful once running and not initializing" }
  }
}
```

Example: `{"t":"runtime.state","running":true,"initializing":false,"sink":"ready"}`

> `initializing` was added additively in `proto` 1 (CHG-0091). The wire shape only gained an optional
> key, so existing clients that ignore unknown keys keep working.

### 4.5 `enroll.progress` (streamed)

```json
{
  "$id": "https://lumina/contract/enroll-progress.json",
  "type": "object", "required": ["t", "phase"],
  "properties": {
    "t":        { "const": "enroll.progress" },
    "phase":    { "type": "string", "enum": ["stopping_runtime", "capturing", "starting_runtime"] },
    "captured": { "type": "integer", "minimum": 0, "description": "present when phase == capturing" },
    "total":    { "type": "integer", "minimum": 1, "description": "present when phase == capturing; the 10-frame target for camera" },
    "message":  { "type": "string", "description": "present for a transient 'no face detected' notice" }
  }
}
```

Examples:
```json
{"t":"enroll.progress","phase":"stopping_runtime"}
{"t":"enroll.progress","phase":"capturing","captured":3,"total":10}
{"t":"enroll.progress","phase":"capturing","captured":3,"total":10,"message":"no face detected"}
{"t":"enroll.progress","phase":"starting_runtime"}
```

### 4.6 `enroll.done`

```json
{
  "$id": "https://lumina/contract/enroll-done.json",
  "type": "object", "required": ["t", "ok", "personCount", "embeddingsAdded"],
  "properties": {
    "t":               { "const": "enroll.done" },
    "ok":              { "const": true },
    "personCount":     { "type": "integer", "minimum": 0, "description": "may lag by ~1 s; refresh people.list" },
    "embeddingsAdded": { "type": "integer", "minimum": 1 }
  }
}
```

Example: `{"t":"enroll.done","ok":true,"personCount":2,"embeddingsAdded":10}`

### 4.7 `enroll.error`

```json
{
  "$id": "https://lumina/contract/enroll-error.json",
  "type": "object", "required": ["t", "exitCode", "message", "runtime"],
  "properties": {
    "t":        { "const": "enroll.error" },
    "exitCode": { "type": "integer", "description": "-1 when cancelled/killed; 1/2 for tool errors" },
    "message":  { "type": "string" },
    "runtime":  { "type": "string", "enum": ["started", "absent", "unchanged"],
                  "description": "'started' = runtime is up; 'absent' = camera route could not restart it; 'unchanged' = image route did not touch it" }
  }
}
```

Example: `{"t":"enroll.error","exitCode":2,"message":"no face was captured; no data written","runtime":"started"}`

### 4.8 `enroll.cancelled`

```json
{ "$id": "https://lumina/contract/enroll-cancelled.json", "type": "object", "required": ["t"],
  "properties": { "t": { "const": "enroll.cancelled" } } }
```

Example: `{"t":"enroll.cancelled"}` — acknowledgement of `enroll.camera.cancel` on the cancelling
connection. The **enrolling** connection still ends with `enroll.error` (§6.3).

### 4.9 `error`

```json
{
  "$id": "https://lumina/contract/error.json",
  "type": "object", "required": ["t", "code", "message"],
  "properties": {
    "t":      { "const": "error" },
    "code":   { "type": "string", "enum": ["unauthorized", "bad_request", "busy", "internal"] },
    "message": { "type": "string" }
  }
}
```

| `code` | When | Device also... | Client action |
|---|---|---|---|
| `unauthorized` | missing/wrong token | **closes the connection** | fix token; reconnect |
| `bad_request` | malformed/unknown/out-of-range | keeps connection open | fix the request |
| `busy` | an enrollment is already running | keeps connection open | wait for `enroll.done/error` |
| `internal` | device-side failure (e.g. temp dir) | keeps connection open | surface error; retry |

Recorded `message` values (do not string-match; use `code`):
`"bad or missing token"`, `"volume.set needs 'value'"`, `"volume.mute needs 'value'"`,
`"volume must be 0..100"`, `"enroll needs 'name'"`, `"unknown command"`,
`"enroll.images needs 'images'"`, `"too many images (max 12)"`,
`"images exceed 8388608 bytes total"`, `"no decodable images"`,
`"an enrollment is already running"`, `"cannot create temp dir"`.

### 4.10 Request objects (`APP SENDS`)

Every request is one line and MUST include `"token"`. All objects are JSON Schema `type: object`,
`required: ["t","token"]` plus the per-command fields below.

| `t` | extra required | extra optional | constraints |
|---|---|---|---|
| `volume.get` | — | — | — |
| `volume.set` | `value` (integer) | — | `0 <= value <= 100` |
| `volume.mute` | `value` (boolean) | — | — |
| `people.list` | — | — | — |
| `runtime.state` | — | — | — |
| `runtime.start` | — | — | starts `lumina.service` |
| `runtime.stop` | — | — | stops `lumina.service` |
| `enroll.camera.start` | `name` (string, non-empty) | `frames` (integer) | `frames` clamped to `[1,10]`; default `10` |
| `enroll.camera.cancel` | — | — | use a **separate** connection |
| `enroll.images` | `name` (string, non-empty), `images` (array of base64 strings) | — | `<= 12` items; `<= 8 MiB` decoded total |

**Client image guidance (enrollment).** Resize on the device before sending: height **≤ 1080 px**,
encode as **JPEG** (~quality 80), target **≤ 500 KB per image**, and send **3–5 photos** per person
(the tool keeps at most 10 embeddings). This keeps a batch well under the 8 MiB cap and the 16 MiB
line limit. Send raw base64 (no `data:` URL prefix).

---

## 5. Commands (`APP SENDS` → `APP RECEIVES`)

Template: every command sends one line and receives one or more lines. `<>` = placeholder.

### 5.1 `volume.get`

- APP SENDS: `{"t":"volume.get","token":"<token>"}`
- APP RECEIVES: one `volume.state` (§4.2). `ok` is always `true` here. `value` may be `-1` (unknown).

### 5.2 `volume.set`

- APP SENDS: `{"t":"volume.set","token":"<token>","value":70}`
- APP RECEIVES: one `volume.state`. `ok=false` if the mixer command failed. `value` echoes the
  resulting volume (or `-1` if it cannot be read).
- On `value` out of `0..100`: `{"t":"error","code":"bad_request","message":"volume must be 0..100"}`.

### 5.3 `volume.mute`

- APP SENDS: `{"t":"volume.mute","token":"<token>","value":true}`
- APP RECEIVES: one `volume.state`. `muted` echoes the resulting state; `ok=false` on failure.

### 5.4 `people.list`

- APP SENDS: `{"t":"people.list","token":"<token>"}`
- APP RECEIVES: one `people` (§4.3). May be last-known if the runtime is not reachable.

### 5.5 `runtime.state`

- APP SENDS: `{"t":"runtime.state","token":"<token>"}`
- APP RECEIVES: one `runtime.state` (§4.4).

### 5.6 `runtime.start` (recovery)

- APP SENDS: `{"t":"runtime.start","token":"<token>"}`
- APP RECEIVES: one `runtime.state`. Starts `lumina.service` if stopped. Use after an enrollment
  failure whose `runtime != "started"`, or to recover a dark device.

### 5.7 `runtime.stop`

- APP SENDS: `{"t":"runtime.stop","token":"<token>"}`
- APP RECEIVES: one `runtime.state`. Stops `lumina.service`. The camera is not freed for the app,
  but image enrollment becomes possible if the stop-runtime flag is set.

### 5.8 `enroll.camera.start` (streams)

- APP SENDS: `{"t":"enroll.camera.start","token":"<token>","name":"Ana","frames":10}`
- APP RECEIVES (in order): `enroll.progress` (§4.5) zero or more times, then exactly one of
  `enroll.done` (§4.6) or `enroll.error` (§4.7).
- **This call blocks its TCP connection** until it finishes (up to ~60 s) — do not pipeline other
  commands on it.
- **Cancel** by sending `{"t":"enroll.camera.cancel","token":"<token>"}` on a **second** connection
  (§6.3).
- If another enrollment is active: `{"t":"error","code":"busy",...}` and no stream starts.
- Missing/empty `name`: `{"t":"error","code":"bad_request","message":"enroll needs 'name'"}`.

### 5.9 `enroll.images` (single result)

- APP SENDS (one line):
  `{"t":"enroll.images","token":"<token>","name":"Ana","images":["<base64 jpeg/png>","..."]}`
- APP RECEIVES: optional `enroll.progress` `capturing` lines, then `enroll.done` or `enroll.error`.
- The runtime keeps running by default; on error `runtime` is `"unchanged"` unless the device was
  built with `LUMINA_AGENT_ENROLL_STOP_RUNTIME=1`, in which case `runtime` is `"started"`.
- Caps: `>12` images ⇒ `bad_request "too many images (max 12)"`; `>8 MiB` decoded ⇒
  `bad_request "images exceed 8388608 bytes total"`; nothing decodable ⇒
  `bad_request "no decodable images"`.
- The whole request is one line, so the base64 payload must fit within `MAX_LINE_BYTES`
  (16 MiB). Follow the image guidance in §4.10 (≤1080 px, JPEG, 3–5 photos, ≤500 KB each).

---

## 6. Enrollment semantics

### 6.1 Why the camera route stops the runtime

The camera (`libcamera`) allows a single client. The runtime holds it, so the camera route **stops
`lumina`**, runs the enrollment tool, and **always restarts `lumina`** — on success, tool error, or
cancel. This is why `enroll.error.runtime` exists.

### 6.2 Camera sequence

```
APP →  {"t":"enroll.camera.start","token":"T","name":"Ana","frames":10}
APP ←  {"t":"enroll.progress","phase":"stopping_runtime"}
APP ←  {"t":"enroll.progress","phase":"capturing","captured":1,"total":10}
APP ←  {"t":"enroll.progress","phase":"capturing","captured":1,"total":10,"message":"no face detected"}
APP ←  {"t":"enroll.progress","phase":"capturing","captured":2,"total":10}
      ... up to captured == total ...
APP ←  {"t":"enroll.progress","phase":"starting_runtime"}
APP ←  {"t":"enroll.done","ok":true,"personCount":3,"embeddingsAdded":10}
APP →  {"t":"people.list","token":"T"}        # optional refresh (personCount can lag)
```

Failure:
```
APP ←  {"t":"enroll.progress","phase":"starting_runtime"}
APP ←  {"t":"enroll.error","exitCode":2,"message":"no face was captured; no data written","runtime":"started"}
```

### 6.3 Cancel (two connections)

```
CONN A: APP → {"t":"enroll.camera.start",...}      # streaming; A is blocked
CONN B: APP → {"t":"enroll.camera.cancel","token":"T"}
CONN B: APP ← {"t":"enroll.cancelled"}
CONN A: APP ← {"t":"enroll.error","exitCode":-1,"message":"enrollment failed","runtime":"started"}
```

### 6.4 Image route

```
APP → {"t":"enroll.images","token":"T","name":"Ana","images":["<b64>","<b64>","<b64>"]}
APP ← {"t":"enroll.progress","phase":"capturing","captured":1,"total":3}
APP ← {"t":"enroll.progress","phase":"capturing","captured":2,"total":3}
APP ← {"t":"enroll.progress","phase":"capturing","captured":3,"total":3}
APP ← {"t":"enroll.done","ok":true,"personCount":3,"embeddingsAdded":3}
```

### 6.5 Required client recovery

If `enroll.error.runtime != "started"`, expose a **"Start Lúmina"** action bound to `runtime.start`
(§5.6). Treat `runtime` as the authoritative post-enrollment state.

---

## 7. UI mapping (the three cards)

| UI element | Source | Update cadence |
|---|---|---|
| CORE → "running" | `status.runtime.running` (+`reachable`) | per `status` |
| CORE → temperature | `status.core.tempC` (null ⇒ "—") | per `status` |
| CORE → memory | `status.core.rssMb` (runtime) / `status.core.memAvailableKb` (system, null ⇒ "—") | per `status` |
| CORE → speed (FPS) | `status.core.fps` | per `status` |
| SENSORS → volume % | `status.sensors.volume` (-1 ⇒ "—") | per `status`; `volume.set` on change |
| SENSORS → mute | `status.sensors.muted` | `volume.mute` on toggle |
| SENSORS → day/night | `status.sensors.dayNight` (+`luma`) | per `status` |
| PEOPLE → list | `status.people` (live) / `people.list` (refresh after enroll) | per `status` |
| PEOPLE → add (camera) | `enroll.camera.start` stream → progress bar | on demand |
| PEOPLE → add (photos) | `enroll.images` → result | on demand |
| SENSORS → earbud battery | **not available** (device has no BlueZ Battery1) | — |

---

## 8. Client implementation notes (Compose Multiplatform + Kotlin)

Use **Ktor** (`ktor-network`) for sockets; common Compose code cannot use `java.net` directly.

```kotlin
@Serializable data class StatusEnvelope(
    val t: String, val proto: Int, val ts: Long,
    val runtime: Runtime, val core: Core, val sensors: Sensors,
    val people: List<String>, val enroll: Enroll
)
@Serializable data class Runtime(val reachable: Boolean, val running: Boolean, val uptimeS: Int,
                                 val sink: String, val faceCount: Int)
@Serializable data class Core(val fps: Double, val rssMb: Double,
                              val memAvailableKb: Long? = null, val tempC: Double? = null,
                              val load1: Double? = null)
@Serializable data class Sensors(val volume: Int, val muted: Boolean, val luma: Double, val dayNight: String)
@Serializable data class Enroll(val active: Boolean, val phase: String? = null,
                                val captured: Int? = null, val total: Int? = null)
```

UDP subscribe + receive (pseudo-Kotlin):
```kotlin
val socket = aSocket(Selector).udp().bind(port = 0)          // stable local port
val gateway = InetSocketAddress(gatewayHost, 47600)           // e.g. 10.42.0.1
fun subscribe() = socket.send(ByteArrayPacket("""{"t":"subscribe"}""".encodeToByteArray()), gateway)
// re-send every 5 s; read datagrams and decode each as StatusEnvelope
```

TCP control (pseudo-Kotlin):
```kotlin
val socket = aSocket(Selector).tcp().connect(gatewayHost, 47601)
val out = socket.openWriteChannel(autoFlush = true)
val input = socket.openReadChannel()
fun send(line: String) { out.writeStringUtf8(line + "\n") }
fun reads(): Flow<String> = input.readUTF8LineSequence()      // one JSON object per line
```

Do / Don't:
- **Do** put `token` in every control request.
- **Do** re-`subscribe` every 5 s and declare offline after 5 s without a `status`.
- **Do** ignore unknown JSON keys; never fail on a missing optional field.
- **Don't** expect an `id` echo or pipelined correlation.
- **Don't** send another command on the connection running `enroll.camera.start`; use a second one.
- **Don't** treat `-1`/`null` as real values.

---

## 9. Resilience matrix

| Condition | Signal | Client behavior |
|---|---|---|
| Offline device | no `status` for 5 s | show "Sin conexión"; keep re-subscribing + reconnect TCP with backoff |
| Runtime starting | `runtime.initializing=true` | show "Iniciando…"; disable start/stop until it clears (up to ~60 s; the sink wait can take ~3 min). A runtime started externally (systemd `Restart=`, post-enrollment) is reported within ~30 s |
| Runtime down | `runtime.reachable=false` (and not initializing) | disable control that needs the runtime; offer `runtime.start` |
| People while stopped | `runtime.reachable=false` but `people` non-empty | still list the registered people (they come from the store) |
| Mixer unknown | `sensors.volume=-1` | show "—"; still allow `volume.set` |
| No earbud sink | `runtime.sink != "ready"` | warn "Audio no listo" |
| `unauthorized` | `error.code` | prompt for token; reconnect |
| `busy` | `error.code` | wait for the active enrollment to end |
| Enrollment failed | `enroll.error` | show message; if `runtime!="started"` show Start Lúmina |
| `ts` jumped | `ts` discontinuity | ignore; order by arrival |

---

## 10. Versioning & forward compatibility

- `proto` is `1` in every `status`. `status.t`/every reply `t` discriminates the message kind.
- Additive changes keep `proto`. Breaking changes bump `proto`.
- Clients MUST ignore unknown keys/enum values and MUST NOT assume a field is present unless listed
  as `required`.

---

## 11. Worked end-to-end transcript

```text
# 1. join hotspot; gateway = 10.42.0.1
APP → UDP 10.42.0.1:47600   {"t":"subscribe"}
APP ← UDP                    {"t":"status","proto":1,"ts":...,"runtime":{...},"sensors":{"volume":-1,...},"people":[],"enroll":{"active":false}}
APP → UDP                    {"t":"subscribe"}   # every 5 s

# 2. read live state, then set volume
APP → TCP 10.42.0.1:47601   {"t":"volume.set","token":"T","value":70}
APP ← TCP                   {"t":"volume.state","ok":true,"value":70,"muted":false}

# 3. enroll from the Pi camera (note: blocks this connection)
APP → TCP                   {"t":"enroll.camera.start","token":"T","name":"Ana","frames":10}
APP ← TCP                   {"t":"enroll.progress","phase":"stopping_runtime"}
APP ← TCP                   {"t":"enroll.progress","phase":"capturing","captured":3,"total":10}
APP ← TCP                   {"t":"enroll.progress","phase":"starting_runtime"}
APP ← TCP                   {"t":"enroll.done","ok":true,"personCount":3,"embeddingsAdded":10}

# 4. refresh people
APP → TCP                   {"t":"people.list","token":"T"}
APP ← TCP                   {"t":"people","names":["David Solís","Ana"]}

# 5. recovery if needed
APP → TCP                   {"t":"runtime.start","token":"T"}
APP ← TCP                   {"t":"runtime.state","running":true,"sink":"ready"}
```

---

## 12. Appendix

### 12.1 Enum glossary

| Enum | Values |
|---|---|
| `runtime.sink` / `runtime.state.sink` | `ready`, `waiting`, `absent` |
| `sensors.dayNight` | `day`, `night`, `unknown` |
| `enroll.progress.phase` | `stopping_runtime`, `capturing`, `starting_runtime` |
| `enroll.error.runtime` | `started`, `absent`, `unchanged` |
| `error.code` | `unauthorized`, `bad_request`, `busy`, `internal` |
| message `t` values | `status`, `subscribe`, `volume.get`, `volume.set`, `volume.mute`, `volume.state`, `people.list`, `people`, `runtime.state`, `runtime.start`, `runtime.stop`, `enroll.camera.start`, `enroll.camera.cancel`, `enroll.cancelled`, `enroll.progress`, `enroll.images`, `enroll.done`, `enroll.error`, `error` |

### 12.2 Message index (client perspective)

| APP SENDS (kind) | transport | APP RECEIVES |
|---|---|---|
| `subscribe` | UDP 47600 | `status` (immediate + 1 Hz unicast) |
| `volume.get` | TCP 47601 | `volume.state` |
| `volume.set` | TCP | `volume.state` |
| `volume.mute` | TCP | `volume.state` |
| `people.list` | TCP | `people` |
| `runtime.state` | TCP | `runtime.state` |
| `runtime.start` | TCP | `runtime.state` |
| `runtime.stop` | TCP | `runtime.state` |
| `enroll.camera.start` | TCP | `enroll.progress`* then `enroll.done`\|`enroll.error` |
| `enroll.camera.cancel` | TCP (2nd conn) | `enroll.cancelled` |
| `enroll.images` | TCP | `enroll.progress`* then `enroll.done`\|`enroll.error` |
| any malformed | TCP | `error` |

### 12.3 Traceability

| Contract element | Code |
|---|---|
| `status` fields, sentinels, `dayNight` | `src/agent/telemetry.cpp` (`buildTelemetryJson`, `deriveDayNight`, `parseStatusBlock`) |
| subscribe + unicast + broadcast | `src/agent/udp.cpp`, `src/agent/agent.cpp` (`udpLoop`, `publishTelemetry`) |
| token, request dispatch, replies, caps, cancel | `src/agent/agent.cpp` (`handleRequest`, `handleEnroll*`) |
| ports, bind, CLOEXEC | `src/agent/control.cpp`, `src/agent/agent.hpp` |
| frame clamp | `src/agent/enroll.cpp` (`clampFrameCount`) |
| runtime status file (internal) | `src/status/status.cpp`, `docs/COMPANION.md` |

### 12.4 Change log of this contract

| Version | Date | Change |
|---|---|---|
| 1 | 2026-09-21 | Initial authoritative contract (CHG-0087); supersedes the prose in `APP_PROTOCOL.md`. |
