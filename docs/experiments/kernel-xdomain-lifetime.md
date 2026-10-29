# Linux7.3 Thunderbolt XDomain lifetime fix

Our two Strix Halo hosts saved reboot-path oopses after Thunderbolt-core module
unload/reload. Vmcore inspection found old service devices with bus/type pointers
into an unloaded module; live global-device-list inspection found old XDomains
and services alongside their replacements. This is a different issue from the
RDMA RX polling/TX-window experiments and the earlier AMD profiler deadlock.

## Source path and isolated change

In the exact Linux7.3 source, `tb_switch_remove()` marks attached XDomains
unplugged and calls `tb_xdomain_remove()`. That function explicitly leaves bus
unregistration to `tb_xdomain_unregister()`. Hotplug/resume call the cleanup
helper outside the topology lock. The final `tb_domain_remove()` path stopped
the topology and drained work but omitted the helper before removing the domain.

[The isolated patch](../patches/linux-7.3-xdomain-unregister.patch) calls
`tb_domain_unregister_unplugged_xdomains(tb)` after the workqueue flush and
outside `tb->lock`. Our original domain.c matches the verified upstream7.3
archive byte-for-byte. This is a kernel Thunderbolt-core patch, not an RDMA
provider or Gufo change; it is supplied here as an experimental prerequisite,
not an upstream-accepted fix or a patch needed on every kernel release.

## Direct bounded hardware qualification

Both EVO-X2 and Corsair AI Workstation300, Linux7.3.0-strix-test:

| Gate | EVO | Corsair |
|---|---|---|
| Signed fixed core loaded |pass|pass|
| Three controlled core unload/reload cycles |pass|pass|
| Global device snapshots: no stale objects |pass|pass|
| Host/staged native8/16/32/64KiB, hot/300us idle |pass|pass|
| Tracked native error/retry counter increments |zero|zero|
| Subsequent native-loaded reboot, no new crash dump |pass|pass|

Core srcversion `BDA6ED32433F9503D84E02F`; native driver remains the pinned private
81C6 baseline, RXoff/TX32. Unsigned candidate SHA256:
`aa4aa3050c41261d20884556516a5355b530297df33e5439f67ff4830b7cb52e`.
Signed hashes differ with host signing keys. Kernel boot images, configuration,
and ROCm were unchanged. Module/initramfs backups were verified before deployment.
No kernel lists or pointers were modified manually. Each dirty host could still
fault on the initial reboot while old orphaned devices existed; a fresh fixed
boot is required before testing removal again.

This is bounded qualification, not prolonged stress or every-device coverage.
Journald exits before final device callbacks; we have pre-reboot native-loaded
state, direct boot history, no new saved dump and post-boot device inspection,
not a serial trace proving every callback executed. No performance gain is
claimed from this lifetime fix. Fresh TP1/TP2 measurements are separate.

## Reproduction and deployment cautions

Apply only against a reviewed matching source/config/ABI, build and sign using
an already trusted key, back up the original module AND initramfs, and verify
that rebuilt initramfs contains the signed candidate. Preserve management
Ethernet and a known-good kernel. A new module file cannot repair old registered
devices in the running kernel. After a clean boot, verify loaded srcversion,
record the global devices list, perform bounded idle unload/reload without force,
and check for old/duplicate XDomains/services before reboot qualification.

Do not blindly unload a Thunderbolt core with live users or assume an empty
sysfs directory proves the global registered-device list is clean. The portable
Gufo launcher does not install kernel patches or production startup services.
