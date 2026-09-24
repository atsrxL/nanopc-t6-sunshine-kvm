#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bind the HDMI-RX audio codec to the RX-only rkmoon_hdmirx_codec module at boot.

The pinned vendor kernel clears capture channels for every HDMI codec, so card
rockchiphdmiin has no capture PCM until codec.5 is rebound (kernel/hdmirx-codec/README.md).
Idempotent; verifies module SHA, kernel release and RX parent; rolls back on any failure.
"""
import hashlib, os, pathlib, subprocess, sys, time

# Release layout: <release>/tools/hdmirx_audio_bind.py and <release>/kernel/rkmoon_hdmirx_codec.ko
MODULE = pathlib.Path(os.environ.get("RKMOON_HDMIRX_CODEC_KO",
                      pathlib.Path(__file__).resolve().parents[1] / "kernel" / "rkmoon_hdmirx_codec.ko"))
MODULE_SHA256 = "3f1ea066271374f84e4fb90bfa5ab9739fe3c2ea49eaedd2666c62ae9b319cd8"
KERNEL = "6.1.115-vendor-rk35xx"
CODEC = "hdmi-audio-codec.5.auto"
DEVICE = pathlib.Path("/sys/bus/platform/devices") / CODEC
PARENT = "/sys/devices/platform/fdee0000.hdmirx-controller/"
MACHINE = "/sys/bus/platform/drivers/rk-hdmi-sound"
NEW = "/sys/bus/platform/drivers/rkmoon-hdmirx-codec"
OLD = "/sys/bus/platform/drivers/hdmi-audio-codec"


def write(path, value):
    with open(path, "w") as f:
        f.write(value)


def capture_ready():
    first = pathlib.Path("/proc/asound/pcm").read_text().splitlines()[0]
    return first.startswith("00-00: rockchip-hdmiin") and "capture 1" in first


def main():
    if os.uname().release != KERNEL:
        sys.exit("unexpected kernel release; refusing to load pinned module")
    for _ in range(50):
        if DEVICE.exists() and os.path.exists(MACHINE + "/hdmiin-sound"):
            break
        time.sleep(0.2)
    if not os.path.realpath(DEVICE).startswith(PARENT):
        sys.exit("codec.5 is not the HDMI-RX codec")
    driver = os.path.basename(os.path.realpath(DEVICE / "driver")) if (DEVICE / "driver").exists() else ""
    if driver == "rkmoon-hdmirx-codec" and capture_ready():
        print("already bound; capture PCM present")
        return
    if driver != "hdmi-audio-codec":
        sys.exit("unexpected codec driver state: " + (driver or "unbound"))
    if hashlib.sha256(MODULE.read_bytes()).hexdigest() != MODULE_SHA256:
        sys.exit("module SHA256 mismatch")
    steps = []
    try:
        write(MACHINE + "/unbind", "hdmiin-sound"); steps.append("machine")
        if not os.path.exists(NEW):
            subprocess.run(["insmod", str(MODULE)], check=True); steps.append("module")
        write(OLD + "/unbind", CODEC); steps.append("codec")
        write(DEVICE / "driver_override", "rkmoon-hdmirx-codec"); steps.append("override")
        write(NEW + "/bind", CODEC); steps.append("bind")
        write(MACHINE + "/bind", "hdmiin-sound"); steps.append("machine-bound")
        time.sleep(0.5)
        if not capture_ready():
            raise RuntimeError("capture PCM did not appear")
        print("HDMI-RX capture PCM ready")
    except BaseException as error:
        def attempt(path, value):
            try: write(path, value)
            except OSError: pass
        if "machine" in steps: attempt(MACHINE + "/unbind", "hdmiin-sound")
        if "bind" in steps: attempt(NEW + "/unbind", CODEC)
        if "override" in steps: attempt(DEVICE / "driver_override", "\n")
        if "codec" in steps: attempt(OLD + "/bind", CODEC)
        attempt(MACHINE + "/bind", "hdmiin-sound")
        if "module" in steps: subprocess.run(["rmmod", "rkmoon_hdmirx_codec"], check=False)
        sys.exit(f"rolled back after {steps}: {error}")


if __name__ == "__main__":
    main()
