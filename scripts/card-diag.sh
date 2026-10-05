#!/usr/bin/env bash
# Pull boot diagnostics off the board's microSD card on the Mac, with no keyboard
# and no serial console: the config partition (did first boot consume before.txt?)
# and selected files from the ext4 root filesystem via debugfs (journal, logs,
# NetworkManager state). Read-only: nothing on the card is modified.
#
# Requires: mtools (config partition), e2fsprogs (debugfs) — both via Homebrew.
# Must run as root to read the raw device: sudo scripts/card-diag.sh /dev/diskN
#
# Output: ~/Downloads/radxa/diag-<timestamp>/ (owned by the invoking user)
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "run with sudo (raw disk access)" >&2; exit 1; }
[ $# -ge 1 ] || { echo "usage: sudo $0 /dev/diskN" >&2; diskutil list external physical; exit 1; }
disk="$1"
case "$disk" in /dev/disk[0-9]|/dev/disk[0-9][0-9]) ;; *) echo "need a whole-disk id like /dev/disk5" >&2; exit 1;; esac

DEBUGFS=/opt/homebrew/opt/e2fsprogs/sbin/debugfs
command -v mcopy >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "debugfs missing: brew install e2fsprogs" >&2; exit 1; }

user="${SUDO_USER:-$(whoami)}"
home="$(dscl . -read /Users/"$user" NFSHomeDirectory | awk '{print $2}')"
out="$home/Downloads/radxa/diag-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out"
rdisk="${disk/\/dev\/disk//dev/rdisk}"

diskutil unmountDisk "$disk" >/dev/null 2>&1 || true

echo "== partitions"
diskutil list "$disk" | tee "$out/partitions.txt"

echo "== config partition (s1)"
dd if="${rdisk}s1" of="$out/config.fat" bs=1m 2>/dev/null
mdir -i "$out/config.fat" :: | tee "$out/config-listing.txt"
if mtype -i "$out/config.fat" ::before.txt > "$out/before.txt" 2>/dev/null; then
  echo "before.txt STILL PRESENT -> first-boot config did not complete"
  grep -n -E '^connect_wi-fi|enable_service ssh' "$out/before.txt" \
    | awk '$1 ~ /connect_wi-fi/ && NF>=3 {$3="********"} {print}'
else
  echo "before.txt consumed -> rsetup first-boot ran to completion"
fi

echo "== root filesystem (s3) via debugfs"
root="${rdisk}s3"
dump() {  # dump <path-on-card> <local-name>
  if "$DEBUGFS" -R "dump $1 $out/$2" "$root" >/dev/null 2>&1 && [ -s "$out/$2" ]; then
    echo "  got $1 ($(wc -c < "$out/$2" | tr -d ' ') bytes)"
  else
    rm -f "$out/$2"; echo "  (absent) $1"
  fi
}
"$DEBUGFS" -R "ls -l /" "$root" > "$out/root-ls.txt" 2>&1 || true
dump /etc/hostname hostname
dump /etc/os-release os-release
dump /var/log/syslog syslog
dump /var/log/messages messages
dump /var/log/kern.log kern.log
dump /var/log/dmesg dmesg
dump /var/log/boot.log boot.log
"$DEBUGFS" -R "ls -l /etc/NetworkManager/system-connections" "$root" > "$out/nm-connections-ls.txt" 2>&1 || true
"$DEBUGFS" -R "ls -l /var/lib/NetworkManager" "$root" > "$out/nm-state-ls.txt" 2>&1 || true
"$DEBUGFS" -R "ls -l /var/log" "$root" > "$out/var-log-ls.txt" 2>&1 || true
"$DEBUGFS" -R "ls -l /var/log/journal" "$root" > "$out/journal-ls.txt" 2>&1 || true
# journal files are binary; pull every *.journal we can find so strings/journalctl can read them
for d in $(awk '/^ *[0-9]+ +[0-9]+ .* [0-9a-f]{32}$/ {print $NF}' "$out/journal-ls.txt" 2>/dev/null); do
  "$DEBUGFS" -R "ls -l /var/log/journal/$d" "$root" > "$out/journal-$d-ls.txt" 2>&1 || true
  for j in $(grep -o '[A-Za-z0-9@_.~-]*\.journal\b' "$out/journal-$d-ls.txt" | sort -u); do
    dump "/var/log/journal/$d/$j" "journal-$j"
  done
done
# NetworkManager connection profiles hold the WiFi password: copy with the psk masked
for f in $(grep -o '[^ ]*\.nmconnection' "$out/nm-connections-ls.txt" | sort -u); do
  "$DEBUGFS" -R "cat /etc/NetworkManager/system-connections/$f" "$root" 2>/dev/null \
    | sed -E 's/^(psk=).*/\1********/' > "$out/nm-$f.txt" && echo "  got NM profile $f (psk masked)"
done

chown -R "$user" "$out"
echo
echo "diagnostics in: $out"
