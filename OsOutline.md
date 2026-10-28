# os/: design outline

## 1. Role
`os/` holds the "OS fragments needed for boot and runtime config" (`CLAUDE.md`'s ownership
split) that `make deploy` ships alongside the daemon binaries and `systemd/` unit files
(`Makefile:234-237`). It is not a program — there is no `main.c` here — it is the boot-time
and firewall configuration the Pi 5 needs so that `audiod`'s RT thread on core 3 gets
uncontended CPU/IRQ time and the box never originates or accepts network traffic outside the
LAN UI + deploy SSH (`ProposedStructure.md §2`, `CLAUDE.md` hard constraints). Owner: Aaron.

## 2. Open questions (not resolved here)
- **`cmdline.txt`/`config.txt` merge path.** `ProposedStructure.md:34-35` says everything
  under `os/` is "installed by `make deploy`," but `Makefile:229-244` only stages `os/` into
  `~/dspunit-deploy/os/` on the Pi — it never touches `/boot/firmware/`. Per the user's
  decision (see `os/VERIFY.md`), the actual merge into the live boot files is **future
  Makefile work**, explicitly left unfinished by this pass. `os/cmdline.txt` and
  `os/config.txt` are fragments only; nothing in `os/` applies them yet.
- **`LAN_SUBNET` / `UI_PORT` in `nftables.conf`.** Neither value is decided anywhere in the
  repo (confirmed by grep across all docs). `os/nftables.conf` ships with loud
  `CHANGEME`-style placeholders; whoever deploys fills them in once `ui/`'s listen port is
  chosen (`ui/` is Hazel's, outside this directory's scope).
- **`rt-tune.sh`'s IRQ matching.** Whether grepping `/proc/interrupts` for `xhci_hcd` isolates
  exactly the UMC202HD's isochronous-endpoint IRQ(s), or also catches an unrelated/shared USB
  IRQ, is unverified without a real `/proc/interrupts` dump from the actual Pi 5 + interface
  (`CLAUDE.md`: "Hardware bugs: paste real evidence... No fixes proposed from a verbal
  description").
- **`config.txt` content.** Deliberately comments-only for now (no `dtparam=audio=off`, no
  USB bus-power setting) per the same evidence rule — `Components.md:37` already flags USB
  bus-power as unverified, and nothing should be added until real hardware testing justifies
  it.

## 3. Responsibilities
- Carry the kernel cmdline fragment that isolates core 3 for `audiod`
  (`isolcpus=3 nohz_full=3 rcu_nocbs=3`) once a merge path lands it in
  `/boot/firmware/cmdline.txt`.
- Carry any genuinely-justified Pi firmware `config.txt` fragment (currently none — see §2).
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
  concatenable once a merge step exists).
- **`config.txt`** — Pi firmware `config.txt` fragment; currently comments-only, explaining
  why nothing is set yet rather than guessing.
- **`nftables.conf`** — the complete firewall ruleset (`inet filter` table, default-drop
  input/forward/output, LAN-only inbound exception for SSH + UI port, no outbound exception
  at all).
- **`sshd_config.d/10-dspunit-key-only.conf`** — drop-in picked up automatically by
  Raspberry Pi OS's stock `sshd_config`'s `Include`; disables password and
  challenge-response auth.
- **`rt-tune.sh`** — the one real piece of logic in this directory: POSIX `sh`, idempotent,
  pins the UMC202HD's xHCI IRQ(s) to core 3, sweeps every other IRQ to cores 0-1 (core 2
  stays analyzerd's), disables `irqbalance`, forces the CPU governor to `performance`.
- **`kernel/RT-KERNEL-FALLBACK.md`** — "build notes only" (`ProposedStructure.md:50`) for
  building a PREEMPT_RT kernel from `bcm2712_rt_defconfig` if the apt package fails; pure
  documentation, no companion script, by design.
- **`VERIFY.md`** — the manual post-deploy checklist, plus the two known gaps from §2.

## 5. Shared contracts (coordinate with the rest of the team)
These are cross-directory dependencies, not `os/`'s to decide alone:
- `systemd/rt-tune.service` must exist, be `Type=oneshot`, and be ordered
  (`Before=audiod.service` / `Wants=`) ahead of `audiod` — the unit file itself lives in
  `systemd/`, not here.
- `rt-tune.sh`'s `IRQ_THREAD_PRIO=85` must stay above whatever `CPUSchedulingPriority=` is
  set to in `systemd/audiod.service` (currently assumed 80 per `ProposedStructure.md`'s
  process table). If that priority ever changes, `rt-tune.sh` needs updating to match.
- `nftables.conf`'s `UI_PORT` placeholder depends on a port number chosen in `ui/` (Hazel's
  directory) — `os/` just needs the final number once it's picked.
- The Makefile's `deploy` target (`Makefile:229-244`) already rsyncs `os/` wholesale; any
  future extension to actually merge `cmdline.txt`/`config.txt` into
  `/boot/firmware/` or install `nftables.conf`/`sshd_config.d/` live on the Pi is Makefile
  work, coordinated with whoever touches that file next (Aaron, per `CLAUDE.md` ownership).

## 6. Known gap
Same shape as the `rework.md §1` / `CtldOutline.md §6` persistence gap, but on the deploy
side rather than the storage side: the project's design docs describe `os/` as fully
"installed by `make deploy`," but the actual Makefile only stages files — nothing currently
applies `cmdline.txt`/`config.txt`/`nftables.conf`/`sshd_config.d/` on the Pi itself. Treated
as unclaimed/incomplete until the Makefile (or a documented manual step) actually closes that
loop; see `os/VERIFY.md`'s "Known gaps" section.

## 7. Testability split (per the Makefile's existing philosophy)
None of `os/` is covered by `make test`/`make asan`/`make memcheck` — it's boot/firewall/IRQ
config, not C logic, and holds no code the Makefile's `TESTABLE_SRCS` could ever reference.
The Makefile's own split (`src/common/`+`analysis/` unit-testable, daemon glue not) doesn't
extend here at all; the entire directory's correctness is verified manually, on real
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
