# RKMoon minimal HDMI client (internal evaluation)

Windows-first Qt Widgets frontend over **Moonlight Qt v6.1.0** (not “Qt framework 6.1”).
Fixed source/submodule commits: `sources.lock.json`. Qt framework build versions and
actual test boundaries are recorded in [ACCEPTANCE](docs/ACCEPTANCE.md).

## Connect

There is **no PIN pairing and no TLS on the control channel**. The host authorizes every
request against a password over plaintext HTTP on one base port (47989 by default). The
user name is fixed at `kvm`; the initial server default password is also `kvm`.

> **Plaintext warning.** The password and all control traffic are readable and modifiable
> by anyone on the path. This layout was chosen deliberately for a trusted local network.
> Do not expose the host's base port to an untrusted network or the internet. The
> GameStream RTSP/RTP control and input encryption on the streaming path is unchanged.

1. Enter the host's IP/DNS address and port, plus its password, then **Bind / Connect**.
   No mDNS, no Web UI, no discovery step. The password field is masked and starts at the
   server default.
2. The client requests `serverinfo` on that base port with
   `Authorization: Basic base64("kvm:" + password)`. An unauthorized request is answered
   with a GameStream document whose root `status_code` is 401. An authorized one reports
   `RKMoonAuth=password-http-v1`, `HttpsPort=0` and `PairStatus=1`; the client requires
   the scheme marker and the authorized state before going further.
3. Select **View HDMI**. The fixed `HDMI` app is resolved over an authorized `applist`
   request; there is no game library or application management UI. Every later request,
   including the streaming session's `launch`/`resume`/`cancel`, carries the same header
   to the same base port.
4. Use **Forget host binding** to drop the stored host identity and resolved app.

The password is held in memory for the session, is attached only to the one bound
`http://host:port`, and is never written to settings, a URL or a log. It has to be
retyped after restarting the client. The host applies a shared failure budget (10 burst,
1 recovered per second), so repeated wrong passwords can briefly block a correct one too.

During streaming: **Ctrl+Alt+Shift+Q** disconnects, **X** toggles fullscreen,
**Z** releases input capture. **Ctrl+Alt+Shift+F9/F10/F11** mute / volume down / up.
Relative mouse only; hardware decoding required (HEVC/H.264), stereo Opus playback
retained. Mute attenuates decoded samples without intentionally stopping the Opus
clock. No microphone capture/uplink, gamepads, discovery, updater, Discord or QML UI.
Windowed settings offer resolution/FPS/bitrate/codec, VSync, volume/mute/fullscreen.
The video-mode picker includes an explicit 2560×1440 at 90 fps experimental preset.
It requests exactly 2560×1440/90 from the streaming session; 1920×1080/60 remains
the default. T6 capture and 60-second HEVC/H.264 MPP encoding with independent
decoding have passed; full RTP streaming and client playback at 90 fps remain
unverified, so the preset stays experimental.

The host serves all six endpoints on HTTP 47989 and keeps RTSP 48010 with upstream
RTP/FEC and control/input encryption. There is no 47984 listener and no server
certificate. Web port 47990 is **not** required. `/pair` does not exist (404) and this
client never requests it: `NvPairingManager` is not compiled in. Upstream's separate
"HTTPS" base URL is redirected onto the same plaintext base port by the overlay, so no
request can land on a TLS port. Redirects are never followed, so a 3xx answer can never
move the credential elsewhere.

## Build

See [BUILD](docs/BUILD.md). Source acquisition and overlay application fail closed on
pin/anchor mismatch. No system toolchain install or VM provisioning is performed by
these scripts. Windows build hosts are independently authorized, task-owned clones.

## Status / licensing

The HTTP/password Windows x64 package is available at
smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-reviewed-b4367616e169.zip.
Its SHA256 is c187fad05223b47fc0735bc162672aa00c6ebfb455a7918cf6727db7c1fd960b;
Windows offline tests and SMB readback passed. Real HDMI/audio/input remain untested.

This is **not a public release or hardware-accepted client**. See
[ACCEPTANCE](docs/ACCEPTANCE.md) and [LICENSES](docs/LICENSES.md).
The first evaluation ZIP (`a2137906…`, 2026-09-23) predates security fixes and is
**offline build evidence only: do not pair or stream with that ZIP**.

The current client automatically starts and follows the authenticated host's display
information (server display contract v1 required). Resolution and refresh-rate controls
have been removed; only bitrate (up to 35 Mbps) and codec remain in Settings. When the
source loses signal, the client waits and resumes when a supported signal returns.
Explicit stream exit or Cancel stops automatic reconnect. Ctrl+Alt+Shift+C shows/hides
the local cursor without changing the relative USB mouse protocol; while shown, motion
is subject to local pointer boundaries. Hide it again to restore raw relative capture.


Mouse settings now offer Absolute (default desktop/direct touch) and Relative
(touchpad gestures). Mode changes apply to the next session. Absolute requires the
server to advertise absolute capability and accept rkmoonMouseMode=absolute at launch
and resume. If unsupported, select Relative or upgrade the server; there is no silent
fallback. Ctrl+Alt+Shift+C only toggles the local cursor; it never changes mouse mode.
Direct touch is emulated as absolute mouse input, not native pen/multitouch forwarding.
