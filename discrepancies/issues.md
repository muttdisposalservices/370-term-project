# Open issues (condensed 2026-10-28)

This file lists only the issues that still need addressing. Related files:
- [resolved_issues.md](resolved_issues.md): sections A, B and E, which are fixed, and the
  original text of C1-C7, which are decided.
- [plan.md](plan.md): the C1-C7 decisions, written as work items.

Legend:
- **Owner tags** follow CLAUDE.md's Ownership section, plus systemd/:
  - **[Hazel]** owns audiod, analyzerd, ui and tools, and systemd/ (taken on
    2026-10-28; not yet in CLAUDE.md, see F7).
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
- **[Hazel]** **C10. Status block consistency is unspecified.** audiod is the writer; Aaron
  reviews as the ctld reader. audiod writes the block 2000 times/s and ctld reads it at
  20 Hz from another process (:105-106, dsp:99, 132). No tearing protection is stated.
  **PROPOSED:** the src/common seqlock (one writer: audiod; readers: ctld, soakcheck).
- **[Hazel]** **C11. Startup defaults are undefined.** Nothing says how audiod behaves
  before any coefficients exist (fresh shm) or before ctld has published params (limiter
  threshold, bypass state). audiod boots first, so this window always occurs.
  **PROPOSED:** compiled-in defaults: flat EQ (unity biquads), limiter on at a
  conservative fixed threshold, bypass off.
- **[Hazel]** **C12. Corrupt shm leads to a crash loop.** audiod creates and recreates the
  segments; Aaron owns ctld's reattach. "Corrupt shm" is classed as unrecoverable, so
  audiod exits and restarts (:159-161). The restarted audiod reattaches to the same
  segment (:165) and fails again, until `StartLimitBurst` leaves it dead. Nothing says
  who recreates a bad segment. **PROPOSED:**
  - audiod creates all four segments.
  - On a bad magic/version, audiod unlinks and recreates them (it doesn't exit) and bumps
    the generation counter.
  - analyzerd and ctld only attach, and they reattach when the generation changes.

  This also decides unit ordering (F8).

## 2. audiod

- **[Hazel + Aaron]** **C13. A missing or lost device leaves audio down for good.** Aaron
  owns the udev rule (os/); Hazel owns the audiod.service dependency. Nothing orders
  audiod after the ALSA card exists. If the UMC202HD is late or unplugged, audiod reports
  "device lost" and restarts repeatedly until `StartLimitBurst` stops it. Audio then
  stays down with no recovery path. dsp:275 mentions hotplug, but no doc says what
  happens then. **PROPOSED:** a udev rule (os/) that tags the card `TAG+="systemd"` with
  a stable `SYSTEMD_ALIAS`, plus `BindsTo=`/`After=` on that device unit in audiod.service.
- **[Hazel; HW: Aaron]** **[HW CHECK] C14. Muting on crash and on stream stop is
  undefined.** §5 covers a clean exit and calls out a hang. Still undefined:
  - A SIGSEGV or a watchdog SIGKILL also skips the silence write.
  - What the UMC202HD outputs when the USB OUT stream stops: silence, or a pop.
  - What the ALSA `sw_params` (`stop_threshold`) do on underrun. With the defaults the
    stream stops on underrun, so the "hang" gap is actually short. Set these on purpose.

  **PROPOSED:** set `stop_threshold` = buffer size explicitly, so a hung audiod underruns
  and the stream stops within about 1 ms. Whether the stop pops needs a check on the
  hardware.
- **[Hazel]** **C15. Sample-rate negotiation has no failure rule.** The rate is
  "negotiated … target 96 kHz" (:27, dsp:45). If hw_params returns a different rate,
  every constant shifts: period length in ms, FFT window, ring length in seconds, and
  biquad design. **PROPOSED:** anything other than 96 kHz with 48 or 96 frames is a
  config error with a distinct exit code listed in `RestartPreventExitStatus` (see F10).
- **[Hazel; HW: Aaron]** **[HW CHECK] C16. The sample-format conversion point isn't
  specified.** UMC202HD capture is likely S24_3LE or S32_LE (confirm with
  `/proc/asound/card*/stream0` on the Pi). The ring holds float32 and the biquads use
  float64. **PROPOSED:** convert in the RT loop (int → float64 for the biquads, float32
  into the ring), with a switch on whichever format hw_params negotiated.
- **[Hazel; HW: Aaron]** **[HW CHECK] F1. Polling with no syscall (mechanism B) is
  probably not true on arm64. SOFT.**
  - ProposedStructure.md:92-100 and rework.md's B entry say audiod reads the hw pointer
    "directly in memory" with "no syscall per check" (decision C1, plan.md).
  - As far as I know, the kernel allows mmap of the PCM status/control pages only on
    x86/PPC/Alpha. On arm64, alsa-lib falls back to the `SNDRV_PCM_IOCTL_SYNC_PTR`
    ioctl.
  - If so, every poll check is a syscall that takes the PCM stream lock, which the xHCI
    IRQ thread also takes (an rt_mutex on PREEMPT_RT).
  - Polling still works, but the rationale overstates it. Check with `strace -c` on the
    Pi, or in the Pi kernel's `sound/core/pcm_native.c`, before writing the loop around
    it.
- **[Hazel]** **F2. `READY=1` timing is undefined. SOFT.** Before or after both streams
  are locked? This sets audiod's `TimeoutStartSec` and when units ordered `After=audiod`
  start. Suggest: after lock.
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
- **[Hazel]** **F5. The FFT source is undecided. SOFT.** `LDLIBS_ANALYZERD` has no FFT
  library, which suggests writing our own 32768-point FFT. Decide our own FFT vs FFTW
  explicitly, given the "intelligence in analysis/ is ours" boundary.
- **[Hazel]** **F6. The scheduling class is undecided. SOFT.** ProposedStructure.md:161
  says "SCHED_OTHER (or FIFO 10)". The unit file needs one answer.

## 4. systemd units

- **[Hazel]** **F7. systemd/ ownership isn't recorded. SOFT.** Hazel took systemd/ on
  2026-10-28, but CLAUDE.md's Ownership list doesn't include it yet. Add it there.
- **[Hazel]** **F8. Unit ordering depends on C12. HARD.**
  - If audiod creates the segments, analyzerd.service and ctld.service get
    `After=`/`Wants=audiod.service`.
  - They must not get `BindsTo=` or `Requires=`: §5 needs them to survive an audiod
    restart through the generation counter.
  - Device ordering for audiod itself is C13.
- **[Hazel]** **F9. The service user and permissions are undefined. HARD.** The unit
  files are Hazel's; creating the user on the Pi is os/ work, so Aaron's. Do the daemons
  run as root or as a dedicated user? If not root:
  - audiod needs `LimitRTPRIO=80` (or `CAP_SYS_NICE`) and `SupplementaryGroups=audio`,
    on top of `LimitMEMLOCK=infinity`.
  - All three daemons need a shared group for the `/dev/shm` segments.
  - It should be a *system* user, so logind's `RemoveIPC` never deletes the segments.
- **[Hazel + Aaron]** **F10. There's no exit-code table. SOFT.** Hazel lists the codes for
  audiod and analyzerd and puts them in the units' `RestartPreventExitStatus`. Aaron
  lists the codes for ctld. §5 asks for "a distinct nonzero code" per unrecoverable
  error, and `RestartPreventExitStatus` needs a list of codes. Neither exists.
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
    the unit, avoids changing unit state during boot.
  - **[Hazel]** Writing `systemd/rt-tune.service` itself, and whether audiod uses
    `Requires=` or `Wants=` on it.
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
- **[Hazel]** **D2. The watchdog doesn't cover exit codes.** (The text describes systemd
  supervision.) ProposedStructure.md:103 says the watchdog setup "covers … clean exit
  codes", but exit codes come from the error-handling policy in §5.
- **[Aaron]** **D3. The audio path works without the mic.**
  rework.md:30 says "nothing here works if either sensor is missing data". Without the
  mic, only the analyzer freezes (§4 gate).
- **[Hazel]** **D4. The dsp-architecture subtitle probably runs off
  the canvas.** dsp:45 is about 348 characters at 12.5 px starting at x=30, roughly
  2,100 px wide on a 1,700 px canvas. Check in a browser.

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
  - C10, C11, C12 and C15;
  - the code side of C14, C16 and F1;
  - F2, F5 and F6;
  - systemd/: F7, F8, F9, the audiod.service half of C13, the rt-tune.service unit in
    F11, and the audiod/analyzerd codes in F10;
  - D2 and D4;
  - the dsp-architecture.png re-export (plan.md).
- **Aaron:**
  - the udev rule in C13, C17, F4, the rt-tune.sh part of F11, and F12;
  - H1, H2, H3 and H4;
  - the hardware checks for C14, C16 and F1;
  - creating the service user (F9) and the ctld codes in F10;
  - D1 and D3;
  - the stale text in OsOutline.md, CtldOutline.md, ProposedStructure.md:67, rework.md
    (`nrpacks` wording and its C2/C6 references), the Makefile and os/;
  - review of C8, C10 and C12 from the ctld side.
