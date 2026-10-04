#!/usr/bin/env bash
# Download and verify the pinned Radxa Zero 3 OS image used for bring-up.
#
# The image is NOT stored in this repo (318 MB, Radxa's artifact). This script is
# the reproducible record of exactly which build we use. Bump the pins together.
#
# Usage: scripts/fetch-image.sh [dest-dir]      (default: ~/Downloads/radxa)
set -euo pipefail

# --- pins -------------------------------------------------------------------
REPO="radxa-build/radxa-zero3"
RELEASE="b6"                                         # 2024-01-10, Debian Bullseye, Rockchip BSP kernel
IMAGE="radxa-zero3_debian_bullseye_cli_b6.img"       # CLI = headless, no display server
# sha512 of the DECOMPRESSED .img, as published in ${IMAGE}.sha512 (verified 2026-10-04)
SHA512="5fbc0c478785b8928fbe986cd5d004d8bc59bfe373ccacd9112f707a5ff455660114b340ca60e67d4155cb18f18daf3d52beb630c9804c42753003e4ce1932e4"
# ----------------------------------------------------------------------------

dest="${1:-$HOME/Downloads/radxa}"
mkdir -p "$dest"
xz_path="$dest/$IMAGE.xz"

if [ -f "$xz_path" ]; then
  echo "already present: $xz_path"
else
  echo "downloading $IMAGE.xz from $REPO release $RELEASE ..."
  if command -v gh >/dev/null 2>&1; then
    gh release download "$RELEASE" --repo "$REPO" --pattern "$IMAGE.xz" --dir "$dest"
  else
    curl -fL --progress-bar -o "$xz_path" \
      "https://github.com/$REPO/releases/download/$RELEASE/$IMAGE.xz"
  fi
fi

echo "verifying sha512 of the decompressed image (streams ~2.4 GB, takes a minute) ..."
got="$(xz -dc "$xz_path" | shasum -a 512 | cut -d' ' -f1)"
if [ "$got" != "$SHA512" ]; then
  echo "SHA512 MISMATCH for $xz_path" >&2
  echo "  want $SHA512" >&2
  echo "  got  $got" >&2
  echo "Delete the file and re-run; if it persists, the release or the pin changed." >&2
  exit 1
fi
echo "SHA512 OK"
echo "$xz_path"
