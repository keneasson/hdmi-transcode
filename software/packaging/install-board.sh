#!/usr/bin/env bash
# Install hdmi-transcode on the board as the boot-time display owner (N4, N7).
# Run ON THE BOARD from software/ after building:  sudo packaging/install-board.sh
#
#   - installs build/hdmi-transcode to /usr/local/bin
#   - installs the systemd unit and /etc/default/hdmi-transcode (kept if present)
#   - disables the desktop and the tty1 login so nothing else draws on HDMI
#   - kernel command line: drops `splash` (Plymouth would hold the display at boot) and
#     `console=tty1` (kernel messages on HDMI); adds vt.global_cursor_default=0.
#     The previous /etc/kernel/cmdline is kept as cmdline.pre-hdmi-transcode.
#
# Idempotent. Takes effect fully after a reboot.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
here="$(cd "$(dirname "$0")" && pwd)"
build="$here/../build"
[ -x "$build/hdmi-transcode" ] || { echo "build first: cmake --build build" >&2; exit 1; }

install -m 0755 "$build/hdmi-transcode" /usr/local/bin/hdmi-transcode
install -m 0644 "$here/hdmi-transcode.service" /etc/systemd/system/hdmi-transcode.service
[ -f /etc/default/hdmi-transcode ] || install -m 0644 "$here/hdmi-transcode.default" /etc/default/hdmi-transcode

systemctl disable --now gdm 2>/dev/null || true
systemctl set-default multi-user.target
systemctl mask getty@tty1.service

cmdline=/etc/kernel/cmdline
[ -f "$cmdline.pre-hdmi-transcode" ] || cp -a "$cmdline" "$cmdline.pre-hdmi-transcode"
new="$(sed -E -e 's/(^| )splash( |$)/ /' -e 's/(^| )console=tty1( |$)/ /' "$cmdline" | tr -s ' ' | sed 's/ $//')"
case " $new " in *" vt.global_cursor_default=0 "*) ;; *) new="$new vt.global_cursor_default=0" ;; esac
if [ "$new" != "$(cat "$cmdline")" ]; then
  printf '%s\n' "$new" > "$cmdline"
  u-boot-update >/dev/null
  echo "kernel command line updated: $new"
fi

systemctl daemon-reload
systemctl enable hdmi-transcode.service
echo "installed; start now with: systemctl restart hdmi-transcode (or reboot)"
