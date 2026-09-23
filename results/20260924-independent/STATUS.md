# Independent input deployment — 2026-09-24

T6 live migration: pinned upstream kvmd 78ff181e in /opt/rkmoon-input/source,
new Python3.13 venv using distro packages, independent /etc/rkmoon-input configuration,
system user/group rkmoon-input and Unix socket /run/rkmoon-input/kvmd.sock.
No old VNC package or virtualenv copied. Own gadget rkmoon on fc000000.usb;
old t6-kvm gadget retained unbound, consuming hidg0/1, new minors resolved to hidg2/3.
Keyboard online=true; mouse online=true and absolute=false. Old t6-kvmd,
t6-kvmd-vnc and t6-kvm-panel inactive. No /opt/t6-kvm reference in new config/services.
New input service enabled, streaming service remains task-owned transient unit.

Private rollback configuration is retained on T6 under
/root/agent.backup/rkmoon-independence-20260924. Stop streaming and new input/gadget
before rebinding old gadget. No reboot, old VNC removal, or clean-OS install tested.
User-visible keyboard/mouse delivery after this migration still needs confirmation.

Checks: independent config tests 2 passed; existing tools tests 14 passed;
Python compile and git diff whitespace checks passed; live config validation/import,
real USB gadget creation and input API online checks passed. Kernel tests: source
fixture unavailable locally, 1 passed / 2 skipped (not new kernel acceptance).
Fixed kvmd CLI's double parser default-path issue by invoking the fixed OTG command
with init's explicitly supplied configuration. Raspberry Pi-only metrics return
unavailable; no fabricated health. HTTP access logging disabled for HID privacy.
