#!/usr/bin/env bash
# Enroll one named person into the on-device face store (FR-04).
#
# Thin wrapper around the `lumina_enroll` tool. Run it on the Pi, once per person.
#
#   scripts/enroll_face.sh "María" photos/Maria        # all photos in a folder
#   scripts/enroll_face.sh "Juan"  a.jpg b.jpg         # explicit photo files
#   scripts/enroll_face.sh "Ana"   --camera            # live camera capture
#
# Any other flag (--frames N, --embeddings K, --model-dir DIR, --store PATH) is
# forwarded unchanged. Photo files/folders are turned into --image/--images-dir.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <nombre> <photo|folder|--camera> [more photos] [flags]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Default run directory on the Pi (~/lumina). Override with LUMINA_HOME if set.
LUMINA_HOME="${LUMINA_HOME:-$HOME/lumina}"

BIN="$LUMINA_HOME/lumina_enroll"
if [ ! -x "$BIN" ]; then
  echo "error: $BIN not found or not executable; build with LUMINA_ENABLE_FACE=ON" >&2
  exit 1
fi

cd "$LUMINA_HOME"

name="$1"
shift

# Classify each remaining argument: an existing directory -> --images-dir, an
# existing file -> --image, anything else (e.g. --camera, --frames) is a flag.
args=()
for arg in "$@"; do
  if [ -d "$arg" ]; then
    args+=(--images-dir "$arg")
  elif [ -f "$arg" ]; then
    args+=(--image "$arg")
  else
    args+=("$arg")
  fi
done

# libpiper is not needed by the enrollment tool unless the runtime was built with
# audio; keep the shared loader path uniform in that case.
export LD_LIBRARY_PATH="$PWD/third_party/libpiper/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec "$BIN" --name "$name" "${args[@]}"
