# os/ verification checklist (manual, real hardware only)

Nothing in `os/` is covered by `make test`/`make asan`/`make memcheck` -- it's boot/OS
config, not C logic. Every item below must be checked with pasted real output
(dmesg, `/proc/interrupts`, etc.), not assumed, per CLAUDE.md's hardware-evidence rule.

## Known gaps, not solved by the current os/ files

- **Makefile boot-partition merge: RESOLVED (2026-10-29).** `os/merge-boot-config.sh` now
  idempotently applies `cmdline.txt`'s isolation params and `config.txt`'s `kernel=` line,
  rsynced by `make deploy` along with the rest of `os/` and printed as the manual step to
  run after deploy (`deploy` stays copy-only by design -- it still never edits the Pi's
  live boot config itself). Confirmed on real hardware: running it against an
  already-configured Pi correctly detected both entries present and left them alone
  (no duplication).
- **Before first deploy:** `LAN_SUBNET` is set to all RFC1918 private ranges (`10.0.0.0/8`,
  `172.16.0.0/12`, `192.168.0.0/16`) deliberately, since the unit is still moving between
  development networks with different subnets each time -- **narrow this to the real
  deployment subnet before the final demo** (confirm via `ip -4 addr show` at that
  location; today's test network was `10.0.0.0/24`). `UI_PORT` is still a placeholder
  (`0`) -- `ui/` (Hazel's) hasn't picked a listen port yet, so this can't be set solo;
  needs a sync before `nftables` can actually be enabled.
- **`rt-tune.sh` can't affinity-move every IRQ -- by design limitation, not a bug, left
  as-is on purpose (confirmed 2026-10-29):** it attempts every IRQ in `/proc/interrupts`
  (ProposedStructure.md §2: "moves to cores 0-1 along with every other IRQ"), and a fixed
  subset always fails with "could not set affinity" -- irqs 9, 11-13 (vgic/kvm
  timers/arch_timer), 33-36 (arm-pmu), 162-164 (i2c), and 169-181 (HDMI/VC4 display, power
  button). The timer/pmu ones are genuinely per-CPU interrupts with no single owner to
  redirect (a kernel property, not fixable here). The i2c/display ones show **0 count on
  every core** in the real `/proc/interrupts` paste -- dormant hardware on this headless,
  no-display board, so the failure is moot in practice. Deliberately not special-cased or
  skipped in the script: a hardcoded exclusion list (by IRQ number or name) would be
  fragile against kernel updates or hardware changes reassigning IRQ numbers, for no
  measurable runtime benefit (one-time boot script, failures cost microseconds). No
  fallback needed unless one of these starts firing later (e.g. a display gets attached),
  at which point re-check its count on core 3 specifically.

## Checklist

- [x] `/sys/kernel/realtime` -- **file doesn't exist on this kernel build**
      (6.18.50+rpt-rpi-v8-rt). That file comes from a downstream (Debian/SUSE/RHEL)
      ksysfs.c patch, not something upstream RT carries automatically, and
      linux-image-rpi-v8-rt was built straight from mainline RT sources without it --
      consistent with `CONFIG_PREEMPT_RT=y` being true while the file is absent, not a
      contradiction. Confirmed PREEMPT_RT instead via: `uname -v`/`cat /proc/version`
      both show `PREEMPT_RT`, and `grep PREEMPT_RT /boot/config-$(uname -r)` shows
      `CONFIG_PREEMPT_RT=y`. Use those three on any future re-verification instead of
      this file.
- [x] `apt-mark showhold` lists the RT kernel package -- confirmed, `linux-image-rpi-v8-rt`.
- [x] `/proc/cmdline` contains `isolcpus=3 nohz_full=3 rcu_nocbs=3` -- confirmed present and
      actually parsed by the running kernel (seen in `/proc/cmdline` post-boot, not just the
      file on disk).
- [x] `/proc/interrupts` -- xHCI IRQ count rises only on CPU0/CPU1, never CPU3, under load --
      confirmed. Real board labels these `xhci-hcd:usb1` (irq 130) and `xhci-hcd:usb3`
      (irq 135) -- **hyphen, not underscore** (`os/rt-tune.sh`'s original `xhci_hcd` awk
      pattern never matched on this kernel; fixed to `xhci[-_]hcd`). Post-fix,
      `/proc/irq/130/smp_affinity_list` and `/proc/irq/135/smp_affinity_list` both read `0-1`.
- [x] `cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor` reads `performance` on
      every core -- confirmed, all 4 cores.
- [ ] `nft list ruleset` matches `os/nftables.conf` (with real `LAN_SUBNET`/`UI_PORT` filled in)
- [ ] From LAN: UI port and key-based SSH respond; password SSH is rejected; every other
      port is refused or times out
- [ ] From off the LAN subnet: SSH is refused too
- [ ] An outbound probe from the Pi (e.g. `curl` to an external host) fails
- [x] `os/rt-tune.sh` re-run twice produces no errors (idempotency) -- confirmed, two
      consecutive runs produced identical output.
- [x] Real `/proc/interrupts` paste confirms the xHCI IRQ(s) land on CPU0/CPU1 and that
      RP1's affinity is settable for them (plan.md C6) -- confirmed for both xHCI IRQs
      (130, 135). See the "Known gaps" section above for the other IRQs that can't be
      moved -- expected, not a bug.
- [x] UMC202HD's "clock source N is not valid" USB quirk is actually closed on this
      hardware, not just inferred from kernel version -- confirmed by forcing a fresh USB
      probe (`sudo dmesg -C`, unplug/replug the interface, `dmesg`) on
      `linux-image-rpi-v8-rt` 6.18.50+rpt-rpi-v8-rt: the message never appears, even on a
      genuinely new enumeration (new USB device number assigned). See Components.md/
      ProposedStructure.md §1.
- [x] RT kernel package was installed via apt and held *before* `nftables` was ever
      enabled (plan.md C7) -- confirmed via real shell history
      (`history | grep -nE "apt (install|mark)|nft"`): line 3 is
      `sudo apt install linux-image-rpi-v8-rt`, and **no `nft` command has ever been run
      on this Pi** -- `os/nftables.conf` hasn't been enabled yet at all, so the required
      ordering holds trivially. Re-check this item again once `nftables` actually gets
      enabled, since this evidence only covers "not yet," not the final state.
