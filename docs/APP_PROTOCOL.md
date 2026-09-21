# Lúmina companion protocol (FR-11) — superseded

> **The authoritative, machine-readable contract is [`API_CONTRACT.md`](API_CONTRACT.md).**
> This file is kept only as a short pointer so existing links keep resolving. Do not implement
> against this summary; implement against `API_CONTRACT.md`.

Quick summary of the wire protocol:

- The Pi is the Wi-Fi hotspot; the app always talks to its **gateway** (commonly `10.42.0.1`).
- **Telemetry:** UDP `47600`. Send `{"t":"subscribe"}`; receive one `status` object per datagram at
  ~1 Hz (broadcast + unicast), and treat the device as offline after 5 s of silence. Re-subscribe
  every 5 s.
- **Control:** TCP `47601`. One JSON object per line (`\n`), and `"token"` in **every** request.
  One reply line per request, in order; no `id` echo. `enroll.camera.start` blocks its connection
  until it finishes — send `enroll.camera.cancel` on a **second** connection.
- **Unknown values:** `sensors.volume == -1`, `sensors.luma == -1`, and
  `core.tempC` / `core.load1` / `core.memAvailableKb` are `null`. Render "—", never `0`.

See `API_CONTRACT.md` for the full JSON schemas, per-command `APP SENDS`/`APP RECEIVES` blocks,
enrollment state machines, UI mapping, the resilience matrix and Kotlin/Ktor notes.
