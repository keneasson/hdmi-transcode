# hdmi-transcode

A small box, built on a Radxa Zero 3W and an HDMI-to-CSI bridge, that sits on the
projector leg of a cheap HDMI splitter and makes "the projector and the stream" work at
the same time.

Small venues often have an older 4:3 projector and a modern 16:9 streaming mixer. Cheap
splitters force the whole chain onto one EDID, so the two fight. This box accepts
whatever the chain locked to, rescales it (compress / letterbox / crop, chosen from a
phone over WiFi), and drives the projector with one stable signal that never changes
because something upstream did.

**Status:** requirements and planning. No code yet. See `docs/requirements.md`.

Licensed under the BSD 3-Clause License; see `LICENSE`.
