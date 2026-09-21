# Face enrollment and recognition (FR-03 / FR-04)

Lúmina recognizes a small set of enrolled people (3-4) **entirely on-device** and greets them by name
in Spanish. This document is the operator runbook: how to enroll, how to verify, and the operational
gotchas. Requirements are FR-03/FR-04 in `SPECS.md`; the implementation lives in
`src/vision/face.*`, `src/vision/face_store.*` and `src/app/face_greeter.*` behind
`IFaceRecognizer` (INV-030).

---

## 1. Pipeline at a glance

| Stage | Choice |
|---|---|
| Detector | OpenCV `FaceDetectorYN` (YuNet, model `2023mar`); input downscaled to `faceDetectionSide` (default 320) |
| Embedder | OpenCV `FaceRecognizerSF` (SFace, fp32); 128-float embeddings |
| Match | cosine >= `faceMatchThreshold` (0.363) **and** best-vs-second margin >= `faceMatchMargin` (0.05) |
| Stability | **3 stable frames** before a greeting; 30 s per-person cooldown (`faceCooldownBackoffFactor`) |
| Store | `models/face/embeddings.bin` (versioned, append-only, <= K embeddings/person, default 10) |
| Greeting | `"<nombre> está enfrente"` (Description priority, non-preempting), pre-warmed at startup |

Privacy: embeddings and photos never leave the device (FR-03.4, INV-003/INV-034).

## 2. One-time: fetch the models

```bash
scripts/8-fetch_face_models.sh     # -> models/face/{yunet,sface}.onnx (SHA-256 pinned)
```

## 3. Enroll a person

One named person per run. Two sources: **photos** (recommended for quality) and the **live camera**
(the showcase path).

### 3a. From photos (recommended)

```bash
scripts/enroll_face.sh "María" photos/Maria        # all photos in a folder
scripts/enroll_face.sh "Juan"  juan1.jpg juan2.jpg # explicit files
```

Give each person **3-5 varied, front-facing, well-lit photos** (different head angles/expressions).

### 3b. From the live camera

The runtime `lumina.service` holds the camera (libcamera allows a single client), so **stop it
first**, and run the script as the **`lumina` user** (not root): the script resolves `lumina_enroll`
and `models/` under `$HOME/lumina` (= `/home/lumina`).

```bash
# 0. Preflight
id                                          # expect groups: video, i2c, audio
ls -l /home/lumina/lumina_enroll /home/lumina/models/face/

# 1. Free the camera
sudo systemctl stop lumina
systemctl is-active lumina                  # expect: inactive

# 2. Enroll (run from wherever this repo's scripts/ lives)
scripts/enroll_face.sh "Nombre Apellido" --camera --frames 10

# 3. Restart so the new greeting is pre-warmed
sudo systemctl start lumina
journalctl -u lumina.service -b --no-pager | grep -E "FaceStore|face: YuNet"
```

Behavior (`src/tools/enroll_face.cpp`):

- Captures the **largest face** each frame; target = `min(frames, embeddings)` = **10** with
  `--frames 10` (default `--frames 8`, cap `--embeddings 10`).
- Prints `captured embedding N/10` every ~300 ms.
- **60 s deadline**: if no face is found it exits 2 with `enroll: no face was captured; no data written`.
- **Appends** to the store (existing people are preserved; the same name just adds embeddings).
- If `/home/lumina/lumina_enroll` is missing, deploy it next to `lumina` from the aarch64 build.

During capture: face the camera straight on at ~40-80 cm, in even light without a strong backlight,
and slowly vary your head angle between captures.

## 4. Verify

- Startup log: `FaceStore: loaded <N> person(s), <M> embedding(s) from 'models/face/embeddings.bin'`,
  and every enrolled greeting is pre-warmed.
- Present an **enrolled** person: after 3 stable frames the runtime speaks `"<nombre> está enfrente"`.
- Present a **non-enrolled** person: **no name** is spoken (FR-03.3).

## 5. Gotchas / troubleshooting

| Symptom | Cause / fix |
|---|---|
| `enroll: failed to start the camera` | The runtime still holds the camera (`sudo systemctl stop lumina`), or the user is not in the `video` group. |
| `enroll: no face was captured; no data written` | Nothing detected within the 60 s deadline (lighting/distance/framing). |
| `enroll: existing store ... could not be read` / `written by a different model` | Move the store aside and re-enroll; a corrupt or foreign store is never silently overwritten. |
| Wrong directory / `lumina_enroll not found` | You ran it with `sudo` (`$HOME=/root`). Run as `lumina`, or set `LUMINA_HOME=/home/lumina`. |
| Want to remove/replace a person | There is no delete command: back up `embeddings.bin` before experiments, or move it aside to start over. |

Never edit `embeddings.bin` by hand; always use the tool.

## 6. Verification log

- **2026-09-20 (CHG-0073):** enrolled David Solís from 4 photos; greeted by name; 7-min soak held
  RSS/swap flat, FPS 3.0-4.3, peak 58.5 °C, no camera timeout.
- **2026-09-21 (CHG-0083):** first on-device test of the **live `--camera`** path (stop service ->
  enroll -> restart). Capture succeeded and persisted to the store, the store reloaded on restart,
  and recognition greeted the enrolled person by name.

## References

- `SPECS.md` FR-03 / FR-04.
- OpenCV `FaceDetectorYN` / `FaceRecognizerSF`: <https://docs.opencv.org/4.x/> (OpenCV 4.10).
- Memory / FPS notes: `docs/PERFORMANCE.md`.
