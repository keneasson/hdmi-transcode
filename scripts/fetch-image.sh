#!/usr/bin/env bash
# Download, verify and decompress the pinned Radxa Zero 3 OS image used for bring-up.
#
# The image is NOT stored in this repo (1.4 GB, Radxa's artifact). The pins live in
# scripts/image.env; this script is the reproducible record of exactly which build
# we use.
#
# Usage: scripts/fetch-image.sh [dest-dir]      (default: ~/Downloads/radxa)
# Result: <dest>/<IMAGE_NAME>.img (the .xz is kept)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=image.env
. "$here/image.env"

dest="${1:-$IMAGE_DIR}"
mkdir -p "$dest"
xz_path="$dest/$IMAGE_ASSET"
img_path="$dest/$IMAGE_NAME.img"

if [ -f "$xz_path" ]; then
  echo "already present: $xz_path"
else
  echo "downloading $IMAGE_ASSET from $IMAGE_REPO release $IMAGE_RELEASE ..."
  if command -v gh >/dev/null 2>&1; then
    gh release download "$IMAGE_RELEASE" --repo "$IMAGE_REPO" --pattern "$IMAGE_ASSET" --dir "$dest"
  else
    curl -fL --progress-bar -o "$xz_path" \
      "https://github.com/$IMAGE_REPO/releases/download/$IMAGE_RELEASE/$IMAGE_ASSET"
  fi
fi

echo "verifying sha512 of $IMAGE_ASSET ..."
got="$(shasum -a 512 "$xz_path" | cut -d' ' -f1)"
if [ "$got" != "$IMAGE_SHA512" ]; then
  echo "SHA512 MISMATCH for $xz_path" >&2
  echo "  want $IMAGE_SHA512" >&2
  echo "  got  $got" >&2
  echo "Delete the file and re-run; if it persists, the release or the pin changed." >&2
  exit 1
fi
echo "SHA512 OK"

if [ ! -f "$img_path" ]; then
  echo "decompressing to $img_path (~8 GB) ..."
  xz -dc "$xz_path" > "$img_path.partial"
  mv "$img_path.partial" "$img_path"
fi
echo "$img_path"
