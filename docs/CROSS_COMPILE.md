# Cross-compiling Lúmina for the Raspberry Pi Zero 2 W

A from-zero, copy-paste guide for a first-timer. It covers **building the sysroot**, **where it
goes**, **the symlinks it needs**, **how to verify it**, and finally **how to build and run**.

- **Host:** your laptop/desktop (here: x86_64, Arch Linux, GCC 14).
- **Target:** Raspberry Pi Zero 2 W, aarch64, Raspberry Pi OS Lite 64-bit (Debian 13 "trixie").
- **Toolchain:** `aarch64-linux-gnu-g++` 14.2.1. It runs on the host and emits aarch64 code.
- **Sysroot:** `cmake/rpi-sysroot/` — a copy of the target's `/usr` (headers + libraries). It is
  **gitignored (~1.4 GB)**. Regenerate it from the Pi; never commit it.

> **C++/build note for Java readers.** The *host* is where you compile; the *target* is where the
> program runs. Because the host is x86-64 and the target is aarch64, you cannot use the host's
> libraries: the compiler would produce a binary linked against the wrong machine code. A
> **sysroot** is a folder holding the target's headers and libraries so the cross-compiler links
> against *those*. Think of it as a frozen copy of the Pi's `/usr`.

---

## TL;DR (repeat users)

```sh
scripts/1-sync_sysroot.sh --host pi@<pi-host>      # one-time / whenever the Pi changes
cmake --preset aarch64 && cmake --build --preset aarch64
scp build/aarch64/lumina pi@<pi-host>:~/
```

First time? Read Part A once. It explains every step and how to verify it.

---

# Part A — One-time setup: build a sysroot

## A1. Prerequisites

### On the host

```sh
aarch64-linux-gnu-g++ --version   # must print GCC 14.x   (INV-020/INV-025)
cmake --version                   # must be >= 3.21 (presets schema v3)
ninja --version                   # the preset generator is Ninja
pkg-config --version              # needed to locate libcamera/OpenCV
rsync --version                   # used to copy the Pi's /usr
ssh -V                            # used to reach the Pi
```

Install anything missing, e.g.:

```sh
# Arch
sudo pacman -S --needed cmake ninja pkgconf rsync openssh
# Debian/Ubuntu
sudo apt install cmake ninja-build pkg-config rsync openssh-client
```

### On the Pi

The sysroot can only contain headers/`.pc`/CMake files that the **Pi actually has installed**, so
install the development packages there first. SSH in:

```sh
ssh pi@<pi-host>
```

Then:

```sh
sudo apt update
# Headers + metadata for the libraries we link against.
#   libcamera-dev  -> libcamera headers + libcamera.pc   (camera capture, INV-011)
#   libopencv-dev  -> OpenCV headers + OpenCVConfig.cmake (face/dnn/objdetect)
#   libasound2-dev -> ALSA headers                        (audio out, later)
sudo apt install -y libcamera-dev libopencv-dev libasound2-dev
```

Verify the packages exist (do not trust guessed names — INV-001):

```sh
apt-cache policy libcamera-dev libopencv-dev libasound2-dev
```

Record the exact versions so they match `INVARIANTS.md` → INV-025:

```sh
dpkg-query -W -f='${Package} ${Version}\n' \
  libcamera-dev libopencv-dev libasound2-dev libc6
```

> If `libcamera-dev`/`libopencv-dev` are missing from your OS image, stop here and fix the Pi
> first: without them the sysroot will not contain the pieces the build needs.

## A2. Extract the sysroot from the Pi (and where it goes)

The sysroot lives at the repository-relative path **`cmake/rpi-sysroot/`**. We copy the Pi's
`/usr` into **`cmake/rpi-sysroot/usr/`** (not into the sysroot root), which keeps the symlinks we
add in A3 out of the copy's way.

The easy path is the helper script (it is just the manual command below plus A3 and A4):

```sh
scripts/1-sync_sysroot.sh --host pi@<pi-host>
```

<details>
<summary>Manual equivalent (what the script does, if you prefer by hand)</summary>

From the **repository root, on the host**:

```sh
mkdir -p cmake/rpi-sysroot

rsync -rlptD --no-owner --no-group --info=progress2 \
  --exclude 'share/doc' --exclude 'share/man' \
  --exclude 'share/locale' --exclude 'share/info' \
  --rsync-path="sudo rsync" \
  pi@<pi-host>:/usr/ cmake/rpi-sysroot/usr/
```

</details>

Why these exact flags:

| Flag | Meaning |
|------|---------|
| `-r -l -p -t -D` | Recurse; copy symlinks as symlinks; preserve permissions, timestamps, device files. |
| `--no-owner --no-group` | Do **not** copy ownership. The Pi's `/usr` is root-owned; preserving it would make rsync demand local root. We only read these files. |
| `--rsync-path="sudo rsync"` | Run the remote side under `sudo`, in case some files are not world-readable. |
| `--exclude share/{doc,man,locale,info}` | Documentation/locales are useless for compiling; skipping them saves space. |
| **(no `--delete`)** | Never remove anything from the sysroot. This protects the symlinks added in A3. |

Expected result: roughly **7,000 files, ~1.4 GB**, containing `usr/include`, `usr/lib`,
`usr/share`. The path does not need to be on a fast disk, but it does need the free space.

> **Alternative without SSH:** you can mount the Pi's SD card on the host and copy its `usr/`
> the same way. This is rarely needed; prefer rsync.

## A3. Make it a usable sysroot (symlinks)

Debian makes `/lib` a **symlink** into `/usr`. A sysroot containing only `usr/` therefore cannot
resolve absolute paths like `/lib/aarch64-linux-gnu/libc.so.6` that glibc's linker scripts use.
Two symlinks fix this. They live **inside** the sysroot and are re-created on every refresh:

```sh
cd cmake/rpi-sysroot
ln -sfn usr/lib lib
ln -sfn aarch64-linux-gnu/ld-linux-aarch64.so.1 usr/lib/ld-linux-aarch64.so.1
```

- `lib -> usr/lib` restores the merged-`/usr` layout.
- `usr/lib/ld-linux-aarch64.so.1 -> aarch64-linux-gnu/ld-linux-aarch64.so.1` lets the linker find
  the ELF interpreter, which the compiler references as `/lib/ld-linux-aarch64.so.1`.

`ln -sfn` is idempotent, so running it again is safe. `../scripts/1-sync_sysroot.sh` does this for you.

## A4. Verify the sysroot — the gate (do this BEFORE building)

A broken sysroot should fail here with a clear message, not deep inside CMake. Quick check with
the script:

```sh
scripts/1-sync_sysroot.sh --check
```

Manual anchor check (all of these must exist):

```sh
SYS=cmake/rpi-sysroot

readlink "$SYS/lib"                                                    # -> usr/lib
ls "$SYS/usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1"              # program loader
ls "$SYS/usr/lib/aarch64-linux-gnu/libc.so.6"                          # glibc
ls "$SYS/usr/lib/aarch64-linux-gnu/libstdc++.so.6"                     # C++ runtime
ls "$SYS/usr/lib/aarch64-linux-gnu/crt1.o"                             # startup object
ls "$SYS/usr/lib/aarch64-linux-gnu/pkgconfig/libcamera.pc"             # libcamera
ls "$SYS/usr/lib/aarch64-linux-gnu/pkgconfig/opencv4.pc"               # OpenCV
ls "$SYS/usr/lib/aarch64-linux-gnu/cmake/opencv4/OpenCVConfig.cmake"   # find_package(OpenCV)
ls "$SYS/usr/include/libcamera/libcamera/camera.h"                     # libcamera headers

find "$SYS" -type f | wc -l    # ~7000
du -sh "$SYS"                  # ~1.4G
```

Then prove the toolchain + sysroot can produce a working aarch64 binary, using a throwaway
hello-world:

```sh
cat > /tmp/hello.cpp <<'EOF'
#include <cstdio>
int main() { std::puts("hello from aarch64"); return 0; }
EOF

SYS="$(pwd)/cmake/rpi-sysroot"
aarch64-linux-gnu-g++ -std=c++23 \
  --sysroot="$SYS" \
  -L"$SYS/usr/lib/aarch64-linux-gnu" \
  -L"$SYS/lib/aarch64-linux-gnu" \
  -Wl,--sysroot,"$SYS" \
  -Wl,-rpath-link,"$SYS/usr/lib/aarch64-linux-gnu" \
  -o /tmp/hello /tmp/hello.cpp

file /tmp/hello                                            # ELF 64-bit ... ARM aarch64
readelf -h /tmp/hello | grep -E "Class|Machine"            # ELF64, AArch64
readelf -l /tmp/hello | grep -A1 interpreter               # /lib/ld-linux-aarch64.so.1
```

If that all passes, you are done with one-time setup.

---

# Part B — Build and run

## B1. Cross-compile with CMake (day-to-day)

From the repository root:

```sh
cmake --preset aarch64     # uses cmake/toolchain-aarch64.cmake + cmake/rpi-sysroot
cmake --build --preset aarch64
```

Output: **`build/aarch64/lumina`**.

## B2. Manual compile (no CMake; debugging only)

Useful when a link error is coming from CMake and you want to isolate it. Same flags as A4, on any
source file, e.g. `src/main.cpp`:

```sh
SYS="$(pwd)/cmake/rpi-sysroot"
aarch64-linux-gnu-g++ -std=c++23 --sysroot="$SYS" \
  -L"$SYS/usr/lib/aarch64-linux-gnu" -L"$SYS/lib/aarch64-linux-gnu" \
  -Wl,--sysroot,"$SYS" -Wl,-rpath-link,"$SYS/usr/lib/aarch64-linux-gnu" \
  -o /tmp/lumina src/main.cpp
```

Flag meanings:

- `--sysroot` — where the target's **headers** live.
- `-L...` — where the target's **libraries** live (required here; see Part C, "Arch gotcha").
- `-Wl,--sysroot,...` — tell the linker to resolve absolute script paths inside the sysroot.
- `-Wl,-rpath-link,...` — let the linker resolve indirect dependencies (e.g. `libm` → `libmvec`).

## B3. Inspect the binary

```sh
file build/aarch64/lumina
readelf -h build/aarch64/lumina | grep -E "Class|Machine"     # ELF64, AArch64
readelf -l build/aarch64/lumina | grep -A1 interpreter         # /lib/ld-linux-aarch64.so.1
readelf -d build/aarch64/lumina | grep NEEDED                  # libstdc++.so.6, libc.so.6, ...
```

## B4. Deploy and run on the Pi

Build on the laptop, never on the device: native builds on a 512 MB board are slow and can run out
of memory (INV-023).

```sh
scp build/aarch64/lumina pi@<pi-host>:~/
ssh pi@<pi-host>
chmod +x ~/lumina
ldd ~/lumina        # every shared library must resolve ("not found" => a missing package)
./lumina            # placeholder binary until the pipeline is implemented
```

---

# Part C — Maintenance, gotchas, troubleshooting

## C1. Refreshing after the Pi changes

After `apt upgrade` on the Pi (or if you install another `-dev` package), re-run:

```sh
scripts/1-sync_sysroot.sh --host pi@<pi-host>
```

This re-copies `/usr` and re-creates the A3 symlinks. We deliberately do **not** pass `--delete`,
so an existing sysroot is only ever added to, never emptied. A failed/aborted sync therefore
leaves you with a working old sysroot rather than a half-empty one.

## C2. Known gotcha: the Arch cross toolchain ignores `--sysroot` for libraries

On this host (Arch's `aarch64-linux-gnu-*`), `--sysroot` redirects headers but **not** the library
search: the toolchain's built-in sysroot is `/usr/aarch64-linux-gnu` with a `lib64` layout, so
`-lm` resolves to the host's x86-64 `/usr/lib64/libm.so` and the link fails with:

```
ld: cannot find /usr/lib64/libm.so.6: file in wrong format
```

`cmake/toolchain-aarch64.cmake` already compensates by adding the same `-L` / `-Wl,--sysroot` /
`-Wl,-rpath-link` flags you see in A4/B2 (to both the compiler and linker flags, so CMake's
compiler probe also succeeds). **Do not remove them.** This is recorded as `CHG-0008`.

## C3. Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `rsync: command not found` | Install `rsync` on the host (A1). |
| `rsync` remote `Permission denied` / `sudo: a password is required` | Configure passwordless sudo on the Pi, or temporarily use `--rsync-path=rsync` if all of `/usr` is readable. |
| `rsync` prints `chown ... Operation not permitted` | You ran a plain `-a` instead of `--no-owner --no-group`; ownership cannot be preserved without local root (A2). |
| `cannot find /usr/lib64/libm.so.6: file in wrong format` | `--sysroot` alone is not enough on this host; use the preset (B1) or add the `-L`/`-Wl` flags (A4/B2). See C2. |
| `cannot find /lib/aarch64-linux-gnu/libc.so.6` | The `lib -> usr/lib` symlink is missing (A3). |
| `libcamera.pc` / `opencv4.pc` not found | The Pi is missing `libcamera-dev` / `libopencv-dev`; install them (A1) then re-sync (C1). |
| `find_package(OpenCV)` fails | `OpenCVConfig.cmake` absent from the sysroot (see A4). |
| `unrecognized command-line option '-std=c++23'` | Cross toolchain too old; must be GCC 14+. |
| Link error about `GLIBCXX_3.x` at run time on the Pi | Host libstdc++ is newer than the Pi's; install a matching cross GCC (INV-025). |
| `cmake --preset aarch64` fails at the compiler probe | Sysroot missing/broken; run `scripts/1-sync_sysroot.sh --check`. |
| `Host key verification failed` | First connection to the Pi; run `ssh pi@<pi-host>` once to accept the key. |
| `scripts/1-sync_sysroot.sh: bad substitution` / `[[` errors | Run it with `bash`, not `sh` (it uses bash arrays). |

## C4. Related

- `../scripts/1-sync_sysroot.sh` — extract/refresh + verify the sysroot.
- `cmake/toolchain-aarch64.cmake` — toolchain file (auto-detects `cmake/rpi-sysroot`).
- `CMakePresets.json` — the `host` and `aarch64` presets.
- `INVARIANTS.md` — INV-023 (cross-compile), INV-025 (verified environment).
- `CHANGELOG.md` — CHG-0008 (Arch `--sysroot` fix), CHG-0009 (this guide + script).
