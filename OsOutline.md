# os/: design outline

## 1. Role
`os/` holds the "OS fragments needed for boot and runtime config" (`CLAUDE.md`'s ownership
split) that `make deploy` ships alongside the daemon binaries and `systemd/` unit files
(`Makefile:234-237`). It is not a program — there is no `main.c` here — it is the boot-time
and firewall configuration the Pi 5 needs so that `audiod`'s RT thread on core 3 gets
uncontended CPU/IRQ time and the box never originates or accepts network traffic outside the
LAN UI + deploy SSH (`ProposedStructure.md §2`, `CLAUDE.md` hard constraints). Owner: Aaron.

## 2. Open questions (not resolved here)
- **`cmdline.txt`/`config.txt` merge path. RESOLVED (2026-10-29).** `os/merge-boot-config.sh`
  idempotently applies both into `/boot/firmware/` (cmdline's isolation params, config.txt's
  RT-kernel `kernel=` selection), rsynced by `make deploy` and printed as the manual step to
  run after (`deploy` stays copy-only by design -- see `Makefile`'s `deploy` target and
  `setupGuide.md` §7). The script reads its values from `os/cmdline.txt`/`os/config.txt`
  directly (its sibling files, as rsynced) rather than hardcoding its own copies, so the
  fragment files are the actual single source of truth. Verified idempotent on real
  hardware, both the original and the fragment-reading version.
- **`LAN_SUBNET` / `UI_PORT` in `nftables.conf`. Partially resolved.** `LAN_SUBNET` is now
  set (deliberately broad: all RFC1918 ranges, since the unit still moves between
  development networks) with a flagged reminder to narrow it to the real subnet before the
  final demo. `UI_PORT` is still undecided -- depends on `ui/`'s listen port (Hazel's,
  outside this directory's scope).
- **`rt-tune.sh`'s IRQ matching. RESOLVED (2026-10-29), real evidence attached.** Real
  `/proc/interrupts` on the actual Pi 5 + UMC202HD shows the IRQs as `xhci-hcd:usb1`/
  `xhci-hcd:usb3` -- hyphen, not underscore. The original `/xhci_hcd/` pattern never
  matched; fixed to `/xhci[-_]hcd/`, confirmed on hardware it now isolates exactly those
  two IRQs (not an unrelated/shared one) and sets their affinity correctly.
- **`config.txt` content. RESOLVED (2026-10-29).** Real hardware evidence now justifies one
  line -- `kernel=kernel8_rt.img` under `[pi5]`, needed because the Pi 5 firmware defaults
  to `kernel_2712.img` and won't pick up the RT kernel package on its own. That line now
  lives in `os/config.txt` itself (not hardcoded elsewhere), and `os/merge-boot-config.sh`
  reads it from there. No other `config.txt` content (no `dtparam=audio=off`, no USB
  bus-power setting) is justified yet -- `Components.md:37` still flags USB bus-power as
  unverified.

## 3. Responsibilities
- Carry the kernel cmdline fragment that isolates core 3 for `audiod`
  (`isolcpus=3 nohz_full=3 rcu_nocbs=3`) once a merge path lands it in
  `/boot/firmware/cmdline.txt`.
- Carry any genuinely-justified Pi firmware `config.txt` fragment (the `[pi5] kernel=`
  line, since 2026-10-29 — see §2).
- Define the only firewall policy the unit runs under: LAN-only inbound to {UI port, SSH},
  zero outbound ever, matching the graded "no network calls except serving the LAN UI"
  constraint (`CLAUDE.md`).
- Harden SSH to key-only, since SSH exists solely to support `make deploy`.
- Provide the boot-time script that pins IRQs and sets the CPU governor so core 3 is
  actually real-time-usable, run once per boot by `systemd/rt-tune.service` (a unit file
  owned alongside the rest of `systemd/`, not by this directory) before `audiod` starts.
- Document the PREEMPT_RT kernel fallback-build procedure, used only if the prebuilt
  `linux-image-rpi-v8-rt` apt package doesn't work on the Pi 5.
- Give whoever deploys a concrete, pasteable checklist to confirm all of the above actually
  took effect on real hardware.

## 4. File breakdown (`os/`)
- **`cmdline.txt`** — bare kernel cmdline fragment, one line, no comments (the real
  `/boot/firmware/cmdline.txt` must stay a single line, so this has to be directly
  concatenable). Merged by `os/merge-boot-config.sh`, which reads this file as its source
  of truth rather than hardcoding a copy of its contents.
- **`config.txt`** — Pi firmware `config.txt` fragment; holds the `[pi5] kernel=` line
  (real hardware evidence, 2026-10-29) plus comments explaining why nothing else is set.
  Merged the same way, by `os/merge-boot-config.sh` reading this file directly.
- **`nftables.conf`** — the complete firewall ruleset (`inet filter` table, default-drop
  input/forward/output, LAN-only inbound exception for SSH + UI port, no outbound exception
  at all).
- **`sshd_config.d/10-dspunit-key-only.conf`** — drop-in picked up automatically by
  Raspberry Pi OS's stock `sshd_config`'s `Include`; disables password and
  challenge-response auth.
- **`rt-tune.sh`** — POSIX `sh`, idempotent. Moves the UMC202HD's xHCI IRQ(s), and every
  other IRQ, to cores 0-1 -- **never core 3**, which belongs to audiod's RT thread alone
  (core 2 stays analyzerd's; ProposedStructure.md §2 C6: a hardirq preempts even
  `SCHED_FIFO`, so co-locating it there would be a self-inflicted jitter source, not a
  benefit). Also disables `irqbalance` and forces the CPU governor to `performance`.
- **`merge-boot-config.sh`** — POSIX `sh`, idempotent, applies `cmdline.txt`/`config.txt`
  (above) into the live `/boot/firmware/` files on the Pi; the manual step that closes the
  gap described in §6, since `make deploy` stays copy-only by design and never edits the
  Pi's live boot config itself.
- **`kernel/RT-KERNEL-FALLBACK.md`** — "build notes only" (`ProposedStructure.md:50`) for
  building a PREEMPT_RT kernel from `bcm2712_rt_defconfig` if the apt package fails; pure
  documentation, no companion script, by design.
- **`VERIFY.md`** — the manual post-deploy checklist, plus the two known gaps from §2.

## 5. Shared contracts (coordinate with the rest of the team)
These are cross-directory dependencies, not `os/`'s to decide alone:
- `systemd/rt-tune.service` must exist, be `Type=oneshot`, and be ordered
  (`Before=audiod.service` / `Wants=`) ahead of `audiod` — the unit file itself lives in
  `systemd/`, not here.
- No scheduling-priority contract exists between `rt-tune.sh` and `audiod.service` --
  an earlier draft assumed one (an `IRQ_THREAD_PRIO=85` meant to stay above audiod's
  `SCHED_FIFO 80`), but that's not how IRQ/thread priority interact: a hardirq preempts
  even `SCHED_FIFO` regardless of any priority number, which is also why no IRQ is ever
  pinned to core 3 in the first place (ProposedStructure.md §2 C6). Core 3's protection
  comes entirely from `isolcpus=3` (`os/cmdline.txt`) plus `rt-tune.sh` never assigning
  any IRQ's affinity to include core 3 -- confirmed on hardware, `os/VERIFY.md`.
- `nftables.conf`'s `UI_PORT` placeholder depends on a port number chosen in `ui/` (Hazel's
  directory) — `os/` just needs the final number once it's picked.
- The Makefile's `deploy` target (`Makefile:229-244`) already rsyncs `os/` wholesale, and now
  prints `os/merge-boot-config.sh` as the manual step for `cmdline.txt`/`config.txt`. Installing
  `nftables.conf`/`sshd_config.d/` live on the Pi is still unscripted, printed as separate
  manual `cp`/`systemctl` steps by `deploy` — future Makefile work if that's ever worth
  automating, coordinated with whoever touches that file next (Aaron, per `CLAUDE.md`
  ownership).

## 6. Known gap (partially resolved)
Same shape as the `rework.md §1` / `CtldOutline.md §6` persistence gap, but on the deploy
side rather than the storage side: the project's design docs describe `os/` as fully
"installed by `make deploy`," but the actual Makefile only stages files. **`cmdline.txt`/
`config.txt` are now applied by `os/merge-boot-config.sh`** (manual step, printed by
`deploy`; verified idempotent on real hardware, 2026-10-29). `nftables.conf`/
`sshd_config.d/` are still not applied automatically — still manual `cp`/`systemctl` steps,
same as before. See `os/VERIFY.md`'s "Known gaps" section.

## 7. Testability split (per the Makefile's existing philosophy)
None of `os/` is covered by `make test`/`make asan`/`make memcheck` — it's boot/firewall/IRQ
config, not C logic, and holds no code the Makefile's `TESTABLE_SRCS` could ever reference.
The Makefile's own split (`src/common/`+`analysis/`+ctld's pure logic in `src/ctl/`
unit-testable, daemon glue not) doesn't extend here at all; the entire directory's correctness is verified manually, on real
hardware, after `make deploy`, via `os/VERIFY.md`'s checklist — never asserted from a verbal
description, per `CLAUDE.md`.

## 8. Failure / supervision behavior
- `rt-tune.sh` runs once per boot, before `audiod`. A failure that `die()`s (no xHCI IRQ
  found, no cpufreq sysfs, `irqbalance` present but un-disable-able) gives `rt-tune.service`
  a non-zero exit, which — depending on how that unit is ordered in `systemd/` — can block
  `audiod` from starting at all. That is the intended failure mode for a genuinely broken RT
  setup: better to not start the audio loop than to start it on an uncontended core 3 that
  isn't actually uncontended.
- Per-IRQ affinity-write failures and governor-write failures only warn and continue, since
  some IRQs/sysfs entries legitimately refuse reassignment and shouldn't block boot over it.
- `rt-tune.sh` is safe to re-run (idempotent) — rerunning it after a config change or a
  partial failure doesn't require a fresh boot.
- `nftables.conf`/`sshd_config.d/` have no daemon-style restart behavior; they take effect
  once loaded (`nft -f`, `sshd` re-exec/reload) and stay in effect until changed or the Pi
  reboots without them being reapplied — which is exactly the still-open Makefile-merge gap
  in §6.
