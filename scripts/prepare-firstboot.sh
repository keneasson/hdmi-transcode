#!/usr/bin/env bash
# Patch the Radxa image's first-boot config so the board joins WiFi and enables SSH
# on first boot. Needed because we bring the board up with no keyboard and WITH a
# display attached: Radxa's default before.txt only enables SSH when "headless"
# (no display connector), so with the monitor plugged in SSH would stay off.
#
# Works on a COPY of the image (<name>-firstboot.img); the pinned original is untouched.
# The WiFi password ends up in plain text inside that copy, so treat the copy as a
# secret: it stays in ~/Downloads/radxa and is never committed (.gitignore covers it).
#
# Facts this relies on (verified against the b6 image on 2026-10-04):
#   - GPT partition 1 is a 16 MB FAT volume labelled "config" at byte offset 16777216
#   - it holds before.txt with the lines
#       #connect_wi-fi private_network password
#       if headless enable_service ssh
#   - rsetup provides connect_wi-fi <ssid> [password] and enable_service <unit>
#     (reference: https://github.com/radxa-pkg/rsetup/blob/main/config/before.txt)
#   - rsetup does NOT source the file as shell. rconfig.sh process_config() does
#     `read -r -a argv <<< "$REPLY"`: each line is split on whitespace, quotes are
#     literal characters, and argv is executed as-is. Therefore the SSID and the
#     password must contain no spaces or tabs, and must not be quoted.
#     (src/usr/lib/rsetup/cli/rconfig.sh and cli/wi-fi.sh, read 2026-10-04)
#   - rsetup echoes "Running connect_wi-fi with <ssid> <password> ..." to the
#     first-boot log, so the password is visible in the journal on the board.
#
# Requires mtools (brew install mtools). macOS cannot mount this FAT volume.
#
# Usage: scripts/prepare-firstboot.sh [--open] [path/to/image.img]
#   --open   the network has no password (otherwise an empty password is refused:
#            on 2026-10-05 an empty prompt produced "connect_wi-fi Easson" with no
#            password, rsetup's 10 nmcli retries could never succeed, and the first
#            boot sat there with a frozen console.)
set -euo pipefail

open_network=0
if [ "${1:-}" = "--open" ]; then open_network=1; shift; fi

die() { echo "error: $*" >&2; exit 1; }
command -v mcopy >/dev/null 2>&1 || die "mtools not found: brew install mtools"

src="${1:-$HOME/Downloads/radxa/radxa-zero3_debian_bullseye_cli_b6.img}"
if [ ! -f "$src" ]; then
  [ -f "$src.xz" ] || die "image not found: $src (run scripts/fetch-image.sh)"
  echo "decompressing $src.xz (keeps the .xz) ..."
  xz -dk "$src.xz"
fi
out="${src%.img}-firstboot.img"

CONFIG_OFFSET=16777216
vol="$out@@$CONFIG_OFFSET"

read -r -p "WiFi SSID: " WIFI_SSID
[ -n "$WIFI_SSID" ] || die "SSID is empty"
if [ "$open_network" = 1 ]; then
  WIFI_PSK=""
else
  read -r -s -p "WiFi password (typed blind, nothing will show): " WIFI_PSK; echo
  [ -n "$WIFI_PSK" ] || die "password is empty. Type it and press Return once; for a network with no password re-run with --open."
  read -r -s -p "Type the password again to confirm: " WIFI_PSK2; echo
  [ "$WIFI_PSK" = "$WIFI_PSK2" ] || die "passwords do not match; nothing written."
  echo "password accepted: ${#WIFI_PSK} characters"
fi
for v in "$WIFI_SSID" "$WIFI_PSK"; do
  [[ "$v" =~ [[:space:]] ]] && die "SSID/password contains whitespace; rsetup's connect_wi-fi splits on it. Use a network without spaces in either, or set up WiFi another way."
done
export WIFI_SSID WIFI_PSK

# Never leave a half-prepared image behind: flash-sd.sh would pick it up.
cleanup() { rm -rf "${tmp:-}"; [ "${ok:-0}" = 1 ] || rm -f "$out"; }
trap cleanup EXIT

echo "copying image to $out ..."
cp "$src" "$out"

mdir -i "$vol" ::before.txt >/dev/null 2>&1 \
  || die "no before.txt in a FAT volume at offset $CONFIG_OFFSET; is this the b6 image?"

tmp="$(mktemp -d)"
mcopy -i "$vol" ::before.txt "$tmp/before.txt"

# rsetup splits the line on whitespace with no quoting (see header), so the values
# go in bare and must contain none. Patched in python to avoid sed metacharacter traps.
python3 - "$tmp/before.txt" <<'EOF'
import os, sys
path = sys.argv[1]
ssid, psk = os.environ["WIFI_SSID"], os.environ["WIFI_PSK"]
wifi = f"connect_wi-fi {ssid} {psk}" if psk else f"connect_wi-fi {ssid}"
text = open(path).read()
a = "#connect_wi-fi private_network password"
b = "if headless enable_service ssh"
if a not in text or b not in text:
    sys.exit("before.txt does not contain the expected lines; refusing to guess")
text = text.replace(a, wifi).replace(b, "enable_service ssh")
open(path, "w").write(text)
EOF

mcopy -o -i "$vol" "$tmp/before.txt" ::before.txt

echo "patched before.txt (password masked):"
mtype -i "$vol" ::before.txt | grep -n -E '^[0-9]*:?connect_wi-fi|enable_service ssh' \
  | awk '$1 ~ /connect_wi-fi/ && NF >= 3 { $3 = "********" } { print }'
# Belt and braces: the written line must have the number of fields we expect.
nf="$(mtype -i "$vol" ::before.txt | awk '/^connect_wi-fi/ { print NF }')"
want=$(( open_network ? 2 : 3 ))
[ "$nf" = "$want" ] || die "written connect_wi-fi line has $nf fields, expected $want; not marking image ready."
ok=1
echo
echo "ready: $out"
echo "next:  scripts/flash-sd.sh /dev/diskN $out"
echo "login: user 'radxa' password 'radxa' over ssh (change it on first login)"
