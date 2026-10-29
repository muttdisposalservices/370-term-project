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
  C13): audiod.service is `BindsTo=`, `After=` and `WantedBy=dev-umc202hd.device`. Still
  needed from os/:
  - A udev rule that matches the ALSA **card** device (`SUBSYSTEM=="sound"`,
    `KERNEL=="card*"`) on the UMC202HD's USB IDs (from `lsusb` on the Pi). It sets
    `TAG+="systemd"` and a `SYSTEMD_ALIAS` of `/dev/umc202hd`.
    - Match the card, not the USB device, so the device unit goes active only once the
      PCMs exist.
    - No `SYSTEMD_WANTS` is needed, because of the `WantedBy=`.
  - **[HW CHECK]** Once audiod runs, unplug the UMC202HD and paste the journal. It should
    show whether a restart after exit 10 is dropped (the bound device is inactive), and
    that the restart doesn't count toward `StartLimitBurst`.
- **[Aaron]** **[HW CHECK] C14. Does the UMC202HD pop when its OUT stream stops?** The
  code side is decided (plan.md C14):
  - `stop_threshold` = buffer size, set explicitly;
  - `start_threshold` = boundary;
  - a watchdog-thread stall exit.

  Record the analog output (scope, or a recording on another device) while an
  `aplay -D hw:<card>` of silence is stopped, once with Ctrl-C and once with `kill -9`.
  If it pops, Hazel switches playback to free-running silence fill (plan.md C14).
- **[Aaron]** **[HW CHECK] C16. Which sample format does the UMC202HD negotiate?** The
  code side is decided (plan.md C16): S32_LE, S24_3LE or S24_LE, with the conversion
  picked once per lock. Paste `/proc/asound/card*/stream0` from the Pi, for both capture
  and playback.
- **[Aaron]** **[HW CHECK] F1. Is each hw-pointer check a syscall on arm64?** The code
  side is decided (plan.md F1): the poll is time-gated, so the loop is correct either way.
  - As far as I know, the kernel allows mmap of the PCM status/control pages only on
    x86/PPC/Alpha. On arm64, alsa-lib falls back to the `SNDRV_PCM_IOCTL_SYNC_PTR`
    ioctl, which takes the PCM stream lock that the xHCI IRQ thread also takes.
  - Check with `strace -c -f arecord -M -D hw:<card> ...` on the Pi (`-M` is mmap mode),
    or in the Pi kernel's `sound/core/pcm_native.c`, and paste the result.
  - The result tunes the 50 µs margin and the wording of C1's "no syscall per check"
    rationale (ProposedStructure.md §2, rework.md's B entry).
- **[Aaron]** **[HW CHECK] H1. Is a 48-frame period accepted?** Left over from decision C2
  (plan.md; Hazel owns the code). Check that snd-usb-audio accepts 48 frames at 96 kHz
  (`snd_pcm_hw_params_get_period_size_min`). If it doesn't, the 96-frame fallback
  applies.
- **[Aaron]** **[HW CHECK] H2. What is the real output-path latency?** Left over from
  decision C3 (plan.md). Do the DAC→ADC loopback impulse measurement to replace the
  ≈6-8 ms planning range.
- **[Aaron]** **[HW CHECK] H3. How long should capture run before playback starts?** Left
  over from decision C4 (plan.md; Hazel owns the code). Measure the settle time from
  capture start to playback start that implicit feedback needs.
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
- **[Aaron; loop side: Hazel]** **[HW CHECK] F14. RT throttling vs. a busy-polling RT
  thread. SOFT.** (New 2026-10-29.)
  - audiod's RT thread spins at SCHED_FIFO 80 on core 3 and never sleeps (C1, F1).
  - RT throttling (`kernel.sched_rt_runtime_us`, default 950000 of every 1000000 µs) caps
    RT tasks at 95% of each second. The thread could then be held off for up to 50 ms,
    about 100 periods, which is a guaranteed xrun.
  - On 6.12+ kernels, the fair-server deadline server may instead run starved CFS tasks
    (per-CPU kworkers) on core 3. That's shorter, but it still preempts the loop.
  - `cyclictest` sleeps between samples, so the planned isolation check can't see this.
  - Check on the Pi and paste:
    - `uname -r` and `cat /proc/sys/kernel/sched_rt_runtime_us`;
    - a minimal busy loop under `chrt -f 80 taskset -c 3`, logging any gap over 100 µs
      for a few minutes.
  - Then decide between two fixes:
    - os/ sets `kernel.sched_rt_runtime_us=-1` (Aaron). That removes the safety net for
      a runaway RT thread, but only on isolated core 3.
    - The loop yields briefly each period (Hazel).

## 3. analyzerd

- **[Aaron]** **F4. The Makefile's test link breaks as soon as analyzerd has a `main.c`.
  HARD.** (Makefile; it blocks Hazel's analyzerd tests.)
  - `TESTABLE_SRCS` (Makefile:57) pulls in all of `analysis/*.c`, `main.c` included. This
    contradicts the Makefile's own header comment ("never a daemon's own main.c").
  - Test binaries link only `-lm` (Makefile:140, 165). A second `main()`, or any
    `sd_notify` call, breaks `make test` and `make asan`.
  - ProposedStructure.md:269 puts the "sd_notify wrapper" in `src/common/`, which every
    test binary links, so it hits the same problem.
  - Fix: `filter-out` the main.c, and keep sd_notify out of src/common (or give tests
    `-lsystemd`).
  - Related (new 2026-10-29, from F5): `src/common/fft.c` calls `cos`/`sin`, and every
    src/common object is linked into every daemon. `LDLIBS_CTLD` (Makefile:37) has no
    `-lm`, so ctld's link fails once `src/ctl/` has sources. Fix: add `-lm` to
    `LDLIBS_CTLD`, or link src/common as a static archive so each daemon pulls in only
    the objects it uses.

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
- **[Aaron]** **[HW CHECK] F11. rt-tune.service can block audiod over a log line. SOFT.**
  - `os/rt-tune.sh` `die`s if `/xhci_hcd/` matches nothing in /proc/interrupts. Since
    C6, that match only feeds a log message.
  - If the RP1 controller's IRQ has a different name on the Pi 5 (possibly
    `xhci-hcd:usbN`), rt-tune.service fails. Per OsOutline.md §8 that is meant to stop
    audiod from starting.
  - Needs a real /proc/interrupts paste before anyone changes it (CLAUDE.md evidence
    rule).
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
- **[Aaron]** **[HW CHECK] H4. Do RP1's PCIe-MSI IRQs accept an affinity change?** Left
  over from decision C6 (plan.md). Paste `/proc/interrupts` under load showing the xHCI
  count rising only on CPU0/1. If affinity can't be set, rt-tune.sh needs a fallback
  (os/VERIFY.md).

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

- **[Aaron]** OsOutline.md §4 says rt-tune.sh "pins the UMC202HD's xHCI IRQ(s) to core
  3". §5 states an `IRQ_THREAD_PRIO=85` contract against audiod's unit priority. c882d91
  removed both.
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

- **Hazel:**
  - the loop side of F14, once Aaron's check is in.
- **Aaron:**
  - the udev rule and unplug check in C13, C17, F4, the rt-tune.sh part of F11, F12 and
    F13;
  - H1, H2, H3 and H4;
  - the hardware checks for C14, C16, F1 and F14 (and the os/ sysctl, if chosen);
  - creating the service user and the EPERM check (F9), and the ctld codes in F10;
  - D1 and D3;
  - the stale text in OsOutline.md, CtldOutline.md, ProposedStructure.md:67, rework.md
    (`nrpacks` wording and its C2/C6 references), the Makefile and os/;
  - review of C8, C10, C11 and C12 from the ctld side.
