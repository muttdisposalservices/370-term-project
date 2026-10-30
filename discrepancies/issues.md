# Open issues (condensed 2026-10-28)

This file lists only the issues that still need addressing. Related files:
- [resolved_issues.md](resolved_issues.md): sections A, B and E, which are fixed, and the
  original text of C1-C7, which are decided.
- [plan.md](plan.md): the C1-C7 decisions, written as work items.

Legend:
- **Owner tags** follow CLAUDE.md's Ownership section:
  - **[Hazel]** owns audiod, analyzerd, ui, tools and systemd/.
  - **[Aaron]** owns the hardware, the Makefile, ctld and os/.
  - **[Hazel; HW: Aaron]**: Hazel owns the code or decision; Aaron does the check on the
    unit.
  - Doc owners (set 2026-10-28):
    - rework.md is Aaron's.
    - Terminology.md and images/dsp-architecture.svg/.png are Hazel's, though both have
      been edited jointly.
    - images/Schematic.svg is the hardware wiring diagram, so Aaron's.
    - Items in ProposedStructure.md are assigned by the component they describe.
- **[HW CHECK]:** the item is neither decided nor addressed, and settling it depends on a
  check on the real Pi 5 + UMC202HD. Paste the evidence (dmesg, /proc output, a capture)
  under the item, per CLAUDE.md.
- **HARD:** code can't be written correctly, or can't pass `make test`/`make asan`, until
  this is decided.
- **SOFT:** work can start, but this must be decided before the component is done.
- **PROPOSED:** a recommendation only. It isn't decided.

F8 was decided by C12 and is now in plan.md. C15, F2, F5, F6, F7 and D2 were settled
on 2026-10-29 and have left this file. Hazel's sides of C13, C14, C16, F1, F9, F10 (audiod, analyzerd) and F11 were
decided the same day and are in plan.md; only the remaining parts are listed below.

IDs are kept from the earlier version (C8-C17, D1-D4, F1-F12; F3 became H1-H4) so
references elsewhere still work. `:NN` references are to ProposedStructure.md unless
another file is named. They were written against earlier versions of the files, so they
may have drifted.

---

## 1. The shared shm contract (HARD, blocks audiod, analyzerd and ctld)

Nothing defines the byte layout, magic/version or creator of any of the four segments
(ring, coeff block, param block, status block). CtldOutline.md §5 says this has to be
settled jointly. Each item below is owned by the side that writes or creates the segment;
the other side reviews.

- **[Aaron]** **C8. Review the triple buffer for the ctld param block.** It's decided and
  implemented (see [resolved_issues.md](resolved_issues.md) C8). From the ctld side:
  - Review `src/common/tbuf.h` and the shared `tests/test_tbuf.c`.
  - Confirm ctld can live with the producer contract:
    - retry `tbuf_producer_attach` on `-EAGAIN`;
    - reattach on `-ESTALE` from `tbuf_publish`;
    - give the param block a fixed `slot_size`.
- **[Aaron]** **C10. Review the seqlock for the status block.** It's decided and
  implemented (see [resolved_issues.md](resolved_issues.md) C10). From the ctld side:
  - Review `src/common/seqlock.h` and the shared `tests/test_seqlock.c`.
  - Confirm ctld can live with the reader contract:
    - on `-EAGAIN` from `seqlock_read`, skip that 20 Hz tick;
    - on `-ESTALE` (audiod restarted), reset any deltas and read again;
    - treat a persistent `-EAGAIN` or a stalled heartbeat as audiod down.
  - **New 2026-10-29: review the payload,** `struct status_block` in
    `src/common/status.h` and the shared `tests/test_status.c`. Confirm ctld can live
    with:
    - the field meanings, including that a zeroed block (heartbeat 0, `meter_window` 0)
      means nothing has been written yet;
    - linear units: peaks in full scale 1, `gain_min` in [0, 1]. ctld does the dB
      conversion;
    - meters that cover the last two 50 ms windows, with `meter_window` there to detect
      a skipped or repeated window;
    - separate `rejected_coeff` and `rejected_param` counts.
  - soakcheck reads status through ctld's socket, not shm, so ctld's "query current
    meters/status" request has to serve it. CtldOutline.md and the Makefile's soakcheck
    comment ("a status-block reader") should say so.
- **[Aaron]** **C11. Review the startup mute gate from the ctld side.** It's decided (see
  [resolved_issues.md](resolved_issues.md) C11 and plan.md C11). audiod stays muted until
  ctld has published one valid param block since audiod started. Confirm ctld can live
  with:
  - republishing its full param state after every `-ESTALE` reattach, not only on change,
    since that is what unmutes a restarted audiod;
  - the output staying muted while ctld is down;
  - serving `mute_state`/`mute_reason` from the status block in its meters/status reply.

  CtldOutline.md should say all three.
  - **Param-block layout: DECIDED and implemented (2026-10-29).** C11's shared tests
    require that a zeroed block is rejected. A zeroed coeff block fails audiod's
    validation because of its all-zero numerators (`src/common/biquad.c`); a zeroed
    **param** block had nothing like that (0 dBFS is a valid, full-scale ceiling, and
    bypass 0 is a valid state), so the layout needed a field that a zeroed slot can't
    satisfy.
    - `struct param_block` (`src/common/param.{h,c}`, tested in `tests/test_param.c`):
      `magic`, `version`, `bypass` (0/1), an explicit `_reserved` pad (keeps
      `ceiling_dbfs` 8-byte aligned with no compiler-inserted padding), `ceiling_dbfs`.
      24 B, size-asserted, no implicit padding -- same style as `struct status_block`.
    - `param_valid()` checks `magic == PARAM_MAGIC && version == PARAM_VERSION`
      *before* the field-level checks (bypass in {0,1}, `limiter_ceiling_valid`), so a
      zeroed `tbuf` slot (`magic == 0`) is rejected regardless of what the other fields
      happen to contain.
    - Only carries `bypass` + `ceiling_dbfs`, not a preset id: presets are ctld's own
      concept and resolve to these concrete values before publishing, so audiod never
      needs to know which preset was active.
    - `param_default()` gives audiod's compiled-in default (bypass off, -6 dBFS), same
      role as `eq_flat()` for the coeff block.
    - Calling `param_valid()` on receipt is audiod's job (plan.md C11); ctld's own
      publisher just needs to fill every field, including magic/version, on every
      publish -- starting from `param_default()` and overwriting `bypass`/
      `ceiling_dbfs` gives that for free.
- **[Aaron]** **C12. Review the segment lifecycle from the ctld side.** It's decided and
  implemented (see [resolved_issues.md](resolved_issues.md) C12). audiod is the only
  creator; ctld and analyzerd only attach. Review `src/common/shm.h` and the shared
  `tests/test_shm.c`, and confirm ctld can live with:
  - attaching with `shm_seg_attach` (read-only for status, RW for the param block), and
    never creating a segment;
  - never exiting over shm state: send `READY=1`, then retry attach every 100 ms while
    sending `WATCHDOG=1` (`-ENOENT`, `-EPROTO` and `-EAGAIN` mean audiod hasn't made the
    segment yet);
  - after `SHM_STALL_MS` (proposed 1 s) of a stalled heartbeat or persistent `-EAGAIN`,
    calling `shm_seg_replaced` at most once a second, and attaching by name again if it
    returns 1.

  CtldOutline.md §9 still lists "corrupt shm" as an unrecoverable exit and should say the
  above instead.

## 2. audiod

- **[Aaron]** **C13. The udev rule for the UMC202HD.** Hazel's half is decided (plan.md
  C13): audiod.service is `BindsTo=`, `After=` and `WantedBy=dev-umc202hd.device`.
  - **Udev rule: RESOLVED (2026-10-30).** `lsusb`: `1397:0507 BEHRINGER International
    GmbH UMC202HD 192k`. Rule at `os/udev/rules.d/99-umc202hd.rules`:
    `SUBSYSTEM=="sound", KERNEL=="card*", ATTRS{idVendor}=="1397", ATTRS{idProduct}=="0507",
    TAG+="systemd", ENV{SYSTEMD_ALIAS}="/dev/umc202hd"`. `Makefile` `deploy` installs it
    and reloads/triggers udev.
  - **[HW CHECK] Still outstanding, blocked on `systemd/audiod.service` (doesn't exist
    yet).** Once audiod runs, unplug the UMC202HD and paste the journal. It should show
    whether a restart after exit 10 is dropped (the bound device is inactive), and that
    the restart doesn't count toward `StartLimitBurst`.
- **[Aaron]** **[HW CHECK] C14. Does the UMC202HD pop when its OUT stream stops?** The
  code side is decided (plan.md C14):
  - `stop_threshold` = buffer size, set explicitly;
  - `start_threshold` = boundary;
  - a watchdog-thread stall exit.

  Record the analog output (scope, or a recording on another device) while an
  `aplay -D hw:<card>` of silence is stopped, once with Ctrl-C and once with `kill -9`.
  If it pops, Hazel switches playback to free-running silence fill (plan.md C14).
- **[Aaron]** **C16. Which sample format does the UMC202HD negotiate? RESOLVED
  (2026-10-30), real evidence attached.** `S32_LE` (24 significant bits), both
  directions. `cat /proc/asound/card2/stream0`: playback Altset 1 = `S32_LE`/24 bits
  (implicit feedback, synced off the capture endpoint), Altset 2 = `S16_LE`/16 bits
  (fallback, explicit feedback, not needed); capture Altset 1 = `S32_LE`/24 bits only, no
  fallback. `S24_3LE` isn't offered by this hardware at all. Confirmed live via
  `arecord`/`aplay -f S32_LE -r 96000 --dump-hw-params`: both open and run successfully
  at `Signed 32 bit Little Endian, Rate 96000 Hz, Stereo`.
- **[Aaron]** **F1. Is each hw-pointer check a syscall on arm64? RESOLVED (2026-10-30),
  real evidence attached.** Yes, confirmed. `strace -c -f arecord -M -D hw:2,0 -r 96000
  -c 2 -f S32_LE -d 5 /dev/null`: 356 `ioctl` calls over 5s, 0 errors. Named trace
  (`strace -f -e trace=ioctl ...`) confirms it's repeated `SNDRV_PCM_IOCTL_SYNC_PTR`
  calls (16+ in the first 1s alone) after setup (`HW_REFINE`/`HW_PARAMS`/`SW_PARAMS`/
  `PREPARE`/`START`). arm64 does take the real ioctl fallback, not pure mmap status-page
  reads. C1's "no syscall per check" wording (ProposedStructure.md §2, rework.md's B
  entry) needs correcting to reflect a real but poll-gated ioctl cost.
- **[Aaron]** **H1. Is a 48-frame period accepted? RESOLVED (2026-10-30), real evidence
  attached.** Yes, both directions. Forced `--period-size=48` at 96 kHz/S32_LE on
  `hw:2,0`, then read the live committed params: `/proc/asound/card2/pcm0c/sub0/hw_params`
  and `pcm0p/sub0/hw_params` both show `rate: 96000 (96000/1)`, `period_size: 48`. No
  96-frame fallback needed.
- **[Aaron]** **[HW CHECK] H2. What is the real output-path latency?** Left over from
  decision C3 (plan.md). Do the DAC→ADC loopback impulse measurement to replace the
  ≈6-8 ms planning range.
- **[Aaron]** **H3. How long should capture run before playback starts? RESOLVED
  (2026-10-30), real evidence attached.** ~0 ms. Swept delay (0, 0.01, 0.05, 0.1, 0.2,
  0.5, 1s) between `arecord` start and `aplay` start on `hw:2,0` at 96 kHz/S32_LE, 6 runs
  total (including delay=0 every time). Zero `underrun!!!` at any delay, all 6 runs. No
  deliberate settle wait needed in C4's coupled start, just capture-then-playback
  ordering.
- **[Aaron]** **F13. Every build is unoptimised (`-O0`). SOFT.** (Makefile; it affects
  audiod's RT loop, which is Hazel's.)
  - `CFLAGS` (Makefile:31) has `-g` but no `-O` level, so native, cross and asan builds
    all compile at GCC's default, `-O0`.
  - audiod's RT loop runs every 0.5 ms on core 3: 19 float64 biquads, the limiter, a ring
    push and the status seqlock write. At `-O0` nothing is inlined or vectorised, and
    locals are spilled to the stack. That spends period budget the design doesn't account
    for, and it can cause soak xruns that come from the build, not the design.
  - Some `-Werror` warnings (e.g. `-Wmaybe-uninitialized`) only fire with optimisation
    on. Code that passes now may stop building once `-O` is added, so it's cheaper to
    switch early.
  - **PROPOSED:** add `-O2` to `CFLAGS` (keep `-g`), so tests run the same code generation
    the Pi does. No `-ffast-math`, because the biquads and limiter depend on IEEE
    behaviour. After the change, Hazel re-runs `make test`, `make asan` and
    `make memcheck`.
- **[Aaron]** **F14. RT throttling vs. a busy-polling RT thread. RESOLVED (2026-10-29),
  real evidence attached.**
  - audiod's RT thread spins at SCHED_FIFO 80 on core 3 and never sleeps (C1, F1).
    `kernel.sched_rt_runtime_us` caps RT tasks at 95% of each second by default
    (950000/1000000 µs); the worry was the thread getting held off up to ~50 ms once a
    second -- a guaranteed xrun -- and that `cyclictest` sleeps between samples, so it
    can't see this.
  - Checked on the real Pi 5: `uname -r` is `6.18.50+rpt-rpi-v8-rt`,
    `sched_rt_runtime_us` is still the unmodified default `950000`.
  - `os/rt-busycheck.c` (new, standalone, same pattern as rt-tune.sh -- not built by the
    Makefile) busy-spins on `CLOCK_MONOTONIC` with no syscalls in the hot path (unlike
    cyclictest), and logs any gap between consecutive reads over 100 µs; it also
    installs a SIGINT handler so an early stop still prints the summary instead of
    losing it.
  - Run as `sudo chrt -f 80 taskset -c 3 ./rt-busycheck 180`. Confirmed mid-run via
    `ps -o pid,psr,rtprio,cls,comm` that it actually held `PSR=3 RTPRIO=80 CLS=FF`, so
    this wasn't silently running under weaker conditions than claimed.
  - Result, two runs: **0 gaps over 100 µs** with the core otherwise idle (180 s,
    ~4.69 billion samples), and again with cores 0-2 saturated by independent busy loops
    for the same 180 s (~4.50 billion samples) to simulate real contention elsewhere on
    the box. Because `CLOCK_MONOTONIC` keeps advancing regardless of which task is
    scheduled, any forced de-scheduling of this thread -- RT throttle, fair-server,
    anything -- would show up directly as a gap on the next sample once it resumes. None
    did, under either condition.
  - Conclusion: on this kernel, with core 3 isolated for audiod's RT thread alone
    (`isolcpus=3`, and rt-tune.sh moves every other IRQ off it), RT throttling does not
    stall the loop in practice at the default `sched_rt_runtime_us`. No os/ sysctl change
    and no loop-side yield are needed.
  - Caveat: this is 180 s, not the 48-hour soak (CLAUDE.md's test of last resort). If the
    soak ever shows xruns correlated with throttling, re-open this with that evidence.

## 3. analyzerd

- **[Aaron]** **F4. RESOLVED (2026-10-29).** `TESTABLE_SRCS` now filters out
  `analysis/main.c`; `LDLIBS_CTLD` now includes `-lm`. Verified: `make test`/`make asan`
  pass clean, and a scratch `analysis/main.c`+`analysis/dummy.c` pair confirmed `main.c`
  is excluded from every test binary while other analysis files still link and test.
    - Since then, two more src/common objects need libm (2026-10-29, checked with
      `nm -u`): `pcmfmt.o` (`lrint`) and `limiter.o` (`pow`, `expm1`).
    - A static archive wouldn't help ctld if it calls `limiter_ceiling_valid` for param
      validation, since that pulls in all of `limiter.o`. Adding `-lm` is the simpler
      fix.

## 4. systemd units

- **[Aaron]** **F9. Create the `dspunit` service user. SOFT.** Decided (plan.md F9): all
  three daemons run as one system user, `dspunit`, not root. The units add the groups and
  limits.
  - Create it in os/ as a *system* user (UID below 1000, no login shell), so logind's
    `RemoveIPC` never deletes the segments. A `sysusers.d` fragment would keep it
    versioned.
  - **[HW CHECK]** Once audiod runs: if it exits 22 because `pthread_setschedparam` fails
    with `EPERM` despite `LimitRTPRIO=80`, paste the kernel config's
    `CONFIG_RT_GROUP_SCHED` line.
- **[Aaron]** **F10. Exit codes for ctld. SOFT.** audiod's and analyzerd's tables are
  decided (plan.md F10, ProposedStructure.md §5). The scheme is 1 internal, 10-19
  restart, 20-29 config errors listed in `RestartPreventExitStatus`, and a code shared
  between daemons means the same thing. Still needed, in the same scheme: ctld's codes.
  - analyzerd exits 21 on `-EACCES`/`-EPERM` from `shm_seg_attach`: a permission error
    is config, not shm state. ctld probably wants the same, which narrows the "never
    exiting over shm state" bullet in C12 above.
- **[Aaron]** **[HW CHECK] F11. rt-tune.service can block audiod over a log line. RESOLVED
  (2026-10-29), real evidence attached.**
  - Confirmed exactly as predicted: the RP1 controller's IRQ is named `xhci-hcd:usb1`/
    `xhci-hcd:usb3` on the real Pi 5 (hyphen, not underscore) -- real `/proc/interrupts`
    paste on hardware. The original `/xhci_hcd/` awk pattern in `os/rt-tune.sh` never
    matched, so it `die`d every run.
  - Fixed: pattern changed to `/xhci[-_]hcd/` (matches both spellings, robust to either
    naming on future kernels). Re-run on hardware, confirmed it now finds both IRQs and
    sets their affinity to `0-1` (`cat /proc/irq/130/smp_affinity_list` and `/135/` both
    read `0-1`). Also confirmed idempotent (two consecutive runs, identical output).
  - Full evidence and the "other IRQs can't be affinitized" follow-on gap (per-CPU
    timers/dormant display hardware, not a bug) are in `os/VERIFY.md`.
  - Unblocks plan.md F11's outstanding item: Hazel can now switch `audiod.service` from
    `Wants=`/`After=rt-tune.service` to `Requires=`, since rt-tune.sh no longer fails on
    this.
  - Separately, the script runs `systemctl disable --now irqbalance` from inside a boot
    oneshot. Masking irqbalance once at deploy, plus `Conflicts=irqbalance.service` in
    the unit (now decided, plan.md F11), avoids changing unit state during boot.
  - The unit side is decided (plan.md F11). For now audiod `Wants=` rt-tune.service, and
    it moves to `Requires=` once this script fails only on real tuning failures (IRQ
    affinity, governor). So this fix is what lets audiod require tuning.
- **[Aaron]** **F12. `make deploy` can't ship the daemons yet. SOFT.**
  - There's no aarch64 sysroot for `-lasound`/`-lsystemd` (Makefile:27).
  - Makefile:247 enables `ctld` and `rt-tune.service`, neither of which exists yet.
  - Until the sysroot exists, build natively on the Pi.
- **[Aaron]** **[HW CHECK] H4. Do RP1's PCIe-MSI IRQs accept an affinity change? RESOLVED
  (2026-10-29), real evidence attached.** Yes, confirmed for the xHCI IRQs specifically:
  real `/proc/interrupts` paste showed irq 130/135 (`xhci-hcd:usb1`/`usb3`) hitting CPU0
  only before tuning; after `rt-tune.sh`, `cat /proc/irq/130/smp_affinity_list` and
  `/135/` both read `0-1`. No fallback needed for these. A fixed subset of *other*
  IRQs (per-CPU timers/pmu, plus dormant i2c/display hardware) can't be affinitized, but
  that's expected (kernel property / zero-count dormant hardware), tracked separately in
  `os/VERIFY.md`'s "Known gaps," not a RP1-PCIe-MSI-wide limitation.

## 5. Hardware

- **[Aaron]** **[HW CHECK] C17. The preamp gain range is unconfirmed.** §7.1 and dsp:219
  need 50-60 dB of gain. Check against the UMC202HD spec and on the unit, since the top
  of that range may exceed what the MIDAS preamp can give.

## 6. Minor (doc fixes, re-checked 2026-10-28: all still present)

- **[Aaron]** **D1. Text overlap in the schematic.** (Schematic.svg is the hardware wiring
  diagram.) Schematic.svg:81 places the "TRS->XLR adapter cables" text at y=460, which is
  the bottom edge of both output boxes (400+60). The text overlaps the box borders.
- **[Aaron]** **D3. The audio path works without the mic.**
  rework.md:30 says "nothing here works if either sensor is missing data". Without the
  mic, only the analyzer freezes (§4 gate).

## 7. Stale text that will mislead whoever implements this

- **[Aaron]** OsOutline.md §4/§5 stale rt-tune.sh claims. **RESOLVED (2026-10-29).** §4
  said rt-tune.sh "pins the UMC202HD's xHCI IRQ(s) to core 3" (backwards -- it moves them
  to cores 0-1, never core 3) and §5 stated a fictional `IRQ_THREAD_PRIO=85` contract
  against audiod's unit priority (the real script sets no IRQ thread priority at all;
  hardirqs preempt `SCHED_FIFO` regardless of any priority number, per C6). Both corrected
  to match the real script and `os/VERIFY.md`'s confirmed hardware behavior.
- **[Hazel]** `images/dsp-architecture.png` needs re-exporting from `images/dsp-architecture.svg`
  -- three lines of its "clock source N is not valid" quirk note were updated 2026-10-29
  (now CONFIRMED on real hardware instead of inferred from kernel version; see
  Components.md/ProposedStructure.md §1/os/VERIFY.md). No SVG-rendering tool was available
  to do the re-export in that session.
- **[Aaron]** CtldOutline.md §2 still lists "fold ctld into the UI" as open. That was
  settled by B12 (resolved_issues.md).
- **[Aaron]** ProposedStructure.md:67 says "~0.67 ms period", which should now be 0.5 ms.
  It's in the §2 RT-kernel/cyclictest bullet, which is os/ work.
- **[Aaron]** rework.md §1 (:20) still names `nrpacks` as the period constraint, but C3
  says it was removed in Linux 3.13. The real limit is snd-usb-audio's minimum period
  (`snd_pcm_hw_params_get_period_size_min`).
- Several files cite "issues.md C2/C6/C7". Those entries now live in plan.md:
  - **[Aaron]** Makefile:257 (C7);
  - **[Aaron]** os/VERIFY.md:30, 33 (C6, C7);
  - **[Aaron]** os/rt-tune.sh:8, 31 (C6);
  - **[Aaron]** rework.md:9, 20 (C6, C2).

## 8. By owner

- **Aaron:**
  - the unplug check in C13 (udev rule resolved 2026-10-30), C17, F4, the rt-tune.sh part
    of F11, F12 and F13;
  - H2 (H1, H3 resolved 2026-10-30, H4 resolved 2026-10-29, see §4/§5);
  - the hardware check for C14 (C16, F1 resolved 2026-10-30, F14 resolved 2026-10-29,
    see §2);
  - creating the service user and the EPERM check (F9), and the ctld codes in F10;
  - D1 and D3;
  - the stale text in OsOutline.md, CtldOutline.md, ProposedStructure.md:67, rework.md
    (`nrpacks` wording and its C2/C6 references), the Makefile and os/;
  - review of C8, C10, C11 and C12 from the ctld side.
