# RKMoon minimal HDMI client (internal evaluation)

Windows-first Qt Widgets frontend over **Moonlight Qt v6.1.0** (not “Qt framework 6.1”).
Fixed source/submodule commits: `sources.lock.json`. Qt framework build versions and
actual test boundaries are recorded in [ACCEPTANCE](docs/ACCEPTANCE.md).

## Connect

1. Enter the dedicated host's IP/DNS address, then **Bind / Connect**. No mDNS or Web UI.
2. The client displays a random four-digit PIN while awaiting approval. On the host,
   its operator runs `python3 tools/admin.py --state <private-state-dir> list`, then
   `python3 tools/admin.py --state <private-state-dir> pin --id <listed-id>` and enters
   the displayed PIN at the private terminal prompt. Do not put PINs on argv/in logs.
3. Select **View HDMI**. The fixed `HDMI` app is resolved over authenticated HTTPS;
   there is no game library or application management UI. Subsequent connections
   retain the host certificate and client identity in a separate RKMoon QSettings scope.
4. If identity changes, use **Forget host binding** and approve again. This forgets
   local trust, not the server's authorization; revoke old clients on the host as needed.

During streaming: **Ctrl+Alt+Shift+Q** disconnects, **X** toggles fullscreen,
**Z** releases input capture. **Ctrl+Alt+Shift+F9/F10/F11** mute / volume down / up.
Relative mouse only; hardware decoding required (HEVC/H.264), stereo Opus playback
retained. Mute attenuates decoded samples without intentionally stopping the Opus
clock. No microphone capture/uplink, gamepads, discovery, updater, Discord or QML UI.
Windowed settings offer resolution/FPS/bitrate/codec, VSync, volume/mute/fullscreen.

The host keeps GameStream HTTP/HTTPS (47989/47984), RTSP 48010 and upstream RTP/FEC,
control/input encryption. Web port 47990 is **not** required. The client never talks
to the private host admin socket over the network.

## Build

See [BUILD](docs/BUILD.md). Source acquisition and overlay application fail closed on
pin/anchor mismatch. No system toolchain install or VM provisioning is performed by
these scripts. Windows build hosts are independently authorized, task-owned clones.

## Status / licensing

This is **not a public release or hardware-accepted client**. See
[ACCEPTANCE](docs/ACCEPTANCE.md) and [LICENSES](docs/LICENSES.md).
The first evaluation ZIP (`a2137906…`, 2026-09-23) predates security fixes and is
**offline build evidence only: do not pair or stream with that ZIP**.
