#!/usr/bin/env bash
# Write the pinned Radxa image to a microSD card. macOS only.
#
# [hardware-touching] This destroys everything on the target disk. The script
# refuses anything that is not an external, physical disk, shows you what it is
# about to erase, and makes you type the disk identifier back before writing.
#
# Usage: scripts/flash-sd.sh /dev/diskN [path/to/image.img | image.img.xz]
#   image defaults to the -firstboot.img from scripts/prepare-firstboot.sh if it
#   exists, else the pinned .img.xz from scripts/fetch-image.sh.
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script uses diskutil and is macOS-only." >&2; exit 1
fi
if [ $# -lt 1 ]; then
  echo "usage: $0 /dev/diskN [image.img.xz]" >&2
  echo; diskutil list external physical; exit 1
fi

disk="$1"
base="$HOME/Downloads/radxa/radxa-zero3_debian_bullseye_cli_b6"
if [ $# -ge 2 ]; then image="$2"
elif [ -f "$base-firstboot.img" ]; then image="$base-firstboot.img"
else image="$base.img.xz"
fi

case "$disk" in
  /dev/disk[0-9]|/dev/disk[0-9][0-9]) ;;
  *) echo "refusing: '$disk' is not a whole-disk identifier like /dev/disk5" >&2; exit 1 ;;
esac
[ -f "$image" ] || { echo "image not found: $image (run scripts/fetch-image.sh)" >&2; exit 1; }

info="$(diskutil info "$disk")"
if ! grep -q 'Device Location: *External' <<<"$info" || ! grep -q 'Virtual: *No' <<<"$info"; then
  echo "refusing: $disk is not an external physical disk:" >&2
  grep -E 'Device / Media Name|Device Location|Virtual|Disk Size' <<<"$info" >&2
  exit 1
fi

echo "About to ERASE this disk and write $image:"
echo
grep -E 'Device / Media Name|Device Location|Disk Size|Volume Name' <<<"$info"
echo
read -r -p "Type the identifier ($disk) to confirm, anything else to abort: " confirm
[ "$confirm" = "$disk" ] || { echo "aborted"; exit 1; }

rdisk="${disk/\/dev\/disk//dev/rdisk}"
diskutil unmountDisk "$disk"
echo "writing (this takes a few minutes) ..."
case "$image" in
  *.xz) xz -dc "$image" | sudo dd of="$rdisk" bs=4m status=progress ;;
  *)    sudo dd if="$image" of="$rdisk" bs=4m status=progress ;;
esac
sync
diskutil eject "$disk"
echo "done. Insert the card into the UNPOWERED board, connect the display, then power on."
