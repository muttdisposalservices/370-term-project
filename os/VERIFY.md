# os/ verification checklist (manual, real hardware only)

Nothing in `os/` is covered by `make test`/`make asan`/`make memcheck` -- it's boot/OS
config, not C logic. Every item below must be checked with pasted real output
(dmesg, `/proc/interrupts`, etc.), not assumed, per CLAUDE.md's hardware-evidence rule.

## Known gaps, not solved by the current os/ files

- **Makefile (unfinished):** `cmdline.txt`/`config.txt` still need a real merge path into
  `/boot/firmware/`. `make deploy` only stages `os/` on the Pi (`Makefile:229-244`) and never
  touches the boot partition. This is tracked as future Makefile work, not solved here.
- **Before first deploy:** fill in `LAN_SUBNET` and `UI_PORT` in `os/nftables.conf` -- neither
  value is decided anywhere else in the repo.

## Checklist

- [ ] `/sys/kernel/realtime` == 1
- [ ] `apt-mark showhold` lists the RT kernel package
- [ ] `/proc/cmdline` contains `isolcpus=3 nohz_full=3 rcu_nocbs=3`
- [ ] `/proc/interrupts` -- xHCI IRQ count rises only on CPU3 under load
- [ ] `cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor` reads `performance` on
      every core
- [ ] `nft list ruleset` matches `os/nftables.conf` (with real `LAN_SUBNET`/`UI_PORT` filled in)
- [ ] From LAN: UI port and key-based SSH respond; password SSH is rejected; every other
      port is refused or times out
- [ ] From off the LAN subnet: SSH is refused too
- [ ] An outbound probe from the Pi (e.g. `curl` to an external host) fails
- [ ] `os/rt-tune.sh` re-run twice produces no errors (idempotency)
- [ ] Real `/proc/interrupts` paste confirms the `xhci_hcd` IRQ(s) `rt-tune.sh` pins are
      actually the UMC202HD's isochronous endpoints, not a shared/unrelated IRQ
