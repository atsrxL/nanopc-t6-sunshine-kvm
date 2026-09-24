# Independent sun-moon installation

This repository contains the client, dedicated Sunshine host, MPP worker and independent
PiKVM/kvmd USB input deployment. The old nanopc-t6-kvm/VNC repository is not required.
Upstream kvmd remains GPL-3.0-or-later; fetch its exact revision in sources.lock.json.
Do not copy a Python virtualenv or configuration from the VNC installation.

## Supported starting point

NanoPC-T6 with vendor HDMI RX/MPP kernel drivers, USB peripheral UDC and configfs,
Debian 13/Armbian with Python 3.13. A generic Linux image without these hardware
interfaces is insufficient. Kernel HDMI codec regressions may require the separately
reviewed RX codec fix; seeing a sound card alone does not establish capture support.

1. Clone this repository. Follow BUILD.md to build MPP, worker and dedicated Sunshine;
   run tools/fetch_sources.py (now includes pinned kvmd). Windows client instructions
   are in client/docs/BUILD.md. No other project checkout is needed.
2. Install input dependencies from Debian packages:

       sudo apt-get install python3-venv python3-aiohttp python3-aiofiles python3-yaml \
         python3-ruamel.yaml python3-psutil python3-setproctitle python3-passlib \
         python3-pyotp python3-serial python3-serial-asyncio python3-libgpiod \
         python3-dbus python3-dbus-next python3-systemd python3-pil python3-pygments \
         python3-xlib python3-hid python3-usb python3-pyudev python3-netifaces \
         libxkbcommon0

   OS package versions are distribution-managed, not byte-reproducibly pinned.
   The installer validates imports/configuration before installing services. Optional
   OCR is unused. The overlay removes the mandatory ustreamer import and returns
   unavailable Raspberry Pi throttling metrics on this non-Pi target; it does not
   fabricate video or health data. Only the kvmd and OTG modules are launched.
3. Run (replace user and UDC with your actual hardware/account):

       sudo python3 tools/install_input.py --source "$PWD/vendor/kvmd" --user at --udc fc000000.usb

   Installs /opt/rkmoon-input, /etc/rkmoon-input and rkmoon-{gadget,input}.service.
   Existing installations are refused. The daemon runs as rkmoon-input; only gadget
   creation runs as root. Local socket authentication admits the configured client
   user; the empty htpasswd file admits no password users. No TCP listener or VNC
   process is started. HTTP access logging is disabled to avoid recording HID request parameters. New group membership requires a fresh user session/service.
4. Ensure exclusive UDC ownership, then:

       sudo systemctl enable --now rkmoon-input.service

   Another gadget bound to the same UDC causes failure. Stop its owner explicitly
   before migration; never delete another project's gadget. The rkmoon gadget uses
   upstream keyboard and relative mouse descriptors. Actual /dev/hidg minors are
   resolved from configfs, not assumed to be 0/1. Restart input after target USB loss
   if kvmd reports offline.
5. Configure config/example.json with your built binaries, private state directory,
   explicit capture ownership and allow_cpu_pixel_copy. Set input.enabled and
   input.exclusive_hid_authorized true, kvmd_socket=/run/rkmoon-input/kvmd.sock.
   Grant the streaming account narrowly scoped video/MPP/render/dma_heap and HDMI
   ALSA permissions. Use hw:CARD=rockchiphdmiin,DEV=0 only after checking arecord -l.
   Sunshine remains unprivileged. Install the server release with tools/install_server.py (docs/SERVER-INSTALL.md).
   Include After= and Requires=rkmoon-input.service for a system service with input.
   Keep MPP libraries in this project's private prefix and scope LD_LIBRARY_PATH to
   that service. Do not use /opt/t6-kvm or /etc/t6-kvm paths.

## Ownership, rollback and removal

The old VNC service and this service cannot simultaneously own HDMI/USB hardware.
Independence means no runtime dependency, not simultaneous hardware access.
Stop the streaming service first, then rkmoon-input, then rkmoon-gadget before switching
back. Rebind a retained old gadget only through its documented owner workflow.
Never delete the VNC checkout or installation to test independence.
Back up configurations under /root/agent.backup before an upgrade. Uninstall only the
rkmoon-owned services/directories after stopping them; do not modify system kvmd.

## Validation boundary

2026-09-24 T6: independent pinned source imports and configuration passed; independent
rkmoon gadget created while old gadget retained unbound; keyboard and relative mouse
reported online. A fresh machine installation and reboot have not yet been tested.
Video/audio binaries and hardware acceptance are documented separately; input setup
success does not establish end-to-end key delivery or HDMI audio on a different host.
