# Plan: decided design changes (2026-10-28; C11, C13-C16, F1, F2, F5, F6 and F8-F11 added 2026-10-29)

These are the decisions that came out of the docs review. Each one is already written into
ProposedStructure.md (and the SVGs where noted). This file turns them into work: what to
build, which component it touches, and what's still outstanding. Entries keep their
original IDs (C1-C7, C11, C13-C16, F1, F2, F5, F6, F8-F11), because other files refer to them,
e.g. `os/rt-tune.sh` and `os/VERIFY.md` cite "issues.md C6/C7". The original issue text
is in [resolved_issues.md](resolved_issues.md). Open questions and hardware checks stay in
[issues.md](issues.md).

Owners follow CLAUDE.md's Ownership section. Hazel owns audiod, analyzerd, ui and tools.
Aaron owns the Makefile, ctld, os/ and systemd/ (moved from Hazel on 2026-10-30; see
resolved_issues.md F15). Hardware checks on the unit are Aaron's.

## Decisions

### C1. The RT loop is paced by polling (mechanism B). Component: audiod. Owner: Hazel
- **Decided:** each period, audiod busy-polls the ALSA mmap hw pointer instead of
  blocking in `poll()`/`snd_pcm_wait()`. Core 3 is isolated for audiod alone, so the CPU
  that blocking would free has no other use.
- **Build:** the RT loop spins on the hw pointer and never waits on the xHCI IRQ.
- **Outstanding:** none. **RESOLVED (2026-10-30):** the "no syscall per check" part of
  the rationale does not hold on arm64 -- confirmed real `SNDRV_PCM_IOCTL_SYNC_PTR` ioctl
  calls (issues.md F1). The loop is time-gated either way (F1 below); wording corrected
  in ProposedStructure.md §2 and rework.md's B entry.

### C2. Period size: 48 frames, falling back to 96. Component: audiod. Owner: Hazel
- **Decided:** at 96 kHz, 48 frames is 4 USB microframes, 0.5 ms and 2000 periods/s. If
  snd-usb-audio's minimum period rejects 48, use 96 frames (1 ms, 1000 periods/s).
- **Build:** hw_params tries 48 first, then 96. Anything else is a config error (C15
  below).
- **Outstanding:** none. **RESOLVED (2026-10-30):** confirmed accepted -- live
  `period_size: 48` on both capture and playback at 96 kHz (issues.md H1).

### C3. Latency budget includes USB. Components: analyzerd, verification. Owner: Hazel (analyzerd); Aaron (measurement)
- **Decided:** plan around **≈6-8 ms total output-path latency**. This is an unverified
  range from third-party measurements. audiod's own share is about 1.0 ms (48 frames ×
  2 periods at 96 kHz).
- **Build:** nothing in code. The delay estimate in analyzerd measures the real total
  anyway.
- **Outstanding:** the DAC→ADC loopback measurement on our hardware and kernel
  (issues.md H2: Aaron).

### C4. Capture/playback start order and coupled xrun recovery. Component: audiod. Owner: Hazel
- **Decided:**
  - Prepare both PCMs, start capture, let it settle, then start playback. Implicit
    feedback forces this order; `snd_pcm_link()` is not used.
  - Any xrun, in either direction, stops and relocks both streams through that same
    sequence.
- **Build:** one start/relock function, shared by startup and every xrun.
- **Outstanding:** none. **RESOLVED (2026-10-30):** ~0 ms -- zero underruns swept across
  0-1 s of delay, 6 runs (issues.md H3). No deliberate wait needed; capture-then-playback
  ordering alone suffices.

### C5. audiod's RT thread pins itself. Components: audiod, systemd. Owner: Hazel (unit: Aaron)
- **Decided:**
  - `audiod.service` sets only `LimitMEMLOCK=infinity`, with no
    `CPUAffinity=`/`CPUSchedulingPolicy=`.
  - When the RT thread starts, it calls `pthread_setschedparam(SCHED_FIFO, 80)` and
    `pthread_setaffinity_np()` (core 3) on itself, before it enters the ALSA loop.
  - The non-RT watchdog/log thread stays at `SCHED_OTHER` with default affinity.
- **Build:**
  - In audiod: the self-pinning at RT-thread start. Check and log every return value,
    and exit with a distinct code if it fails.
  - In `systemd/audiod.service`: `LimitMEMLOCK=infinity` and `LimitRTPRIO=80`, since the
    daemons don't run as root (F9 below).
- **Outstanding:** none.

### C6. No IRQ is pinned to core 3. Component: os. Owner: Aaron
- **Decided:** every IRQ goes to cores 0-1, the xHCI completion IRQ included, and
  `irqbalance` is disabled. This is already done in `os/rt-tune.sh` and `os/VERIFY.md`.
- **Build:** done.
- **Outstanding:** none -- both items below resolved 2026-10-29, real evidence in
  `os/VERIFY.md`/issues.md:
  - ~~Whether RP1's PCIe-MSI IRQs accept an affinity change (issues.md H4: Aaron).~~
    Resolved: yes, for the xHCI IRQs -- confirmed via `/proc/irq/<n>/smp_affinity_list`
    reading `0-1` on real hardware.
  - ~~OsOutline.md still describes the old core-3 pin (issues.md "Stale text").~~
    Resolved: §4/§5 corrected to match the real script's behavior.

### C7. apt runs once at bring-up, before the firewall. Component: os. Owner: Aaron
- **Decided:** the bring-up order is:
  1. flash the SD card;
  2. boot on an open network;
  3. `apt install` and `apt-mark hold` the RT kernel;
  4. `make deploy`;
  5. enable `os/nftables.conf`.
  
  `make deploy` never installs packages. A later kernel swap repeats this bootstrap before
  the firewall is re-enabled.
- **Build:** done (ProposedStructure.md §2, `os/VERIFY.md` checklist, the Makefile's
  printed deploy steps).
- **Outstanding:** none beyond the `os/VERIFY.md` checklist item.

### C11. Startup defaults and mute gate. Components: audiod, analyzerd, ctld. Owner: Hazel (ctld side: Aaron)
- **Decided** (ProposedStructure.md §5):
  - On every start, audiod's private state is compiled in:
    - flat EQ (unity biquads);
    - limiter on: brickwall, -6 dBFS ceiling, no lookahead;
    - bypass off;
    - output muted.
  - A fresh shm slot is never used as a default, because `tbuf_init` zeroes slots.
  - A block replaces the defaults only after `tbuf_consumer_poll` returns 1 and the block
    validates (finite, stable biquads; finite ceiling ≤ 0 dBFS).
    - Added 2026-10-29: a coeff block is also rejected if any band has b0 = b1 = b2 = 0.
      An all-zero slot is finite and stable, so without this rule it would pass. A band
      that outputs silence is never a real EQ band.
  - The limiter has instant attack (no lookahead) and a fixed 50 ms exponential release
    (`LIMITER_RELEASE_MS`, 2026-10-29). Release can move into the param block later, once
    that layout is defined.
  - Unmute (MUTED → FADING_IN → LIVE, linear ramp over `FADE_IN_MS`) needs all of:
    - both PCMs locked (C4);
    - `SELFCHECK_MS` of clean periods (no xrun, finite samples, DC under bound);
      - The DC test is |mean| over the whole `SELFCHECK_MS` window, under
        `DC_BOUND` = 0.01 (−40 dBFS) (2026-10-29). A single 0.5 ms period is too short,
        since bass content looks like DC over one period.
      - While muted and locked, the check re-runs every window until ctld's first param
        block arrives, so the unmute rests on the most recent window (2026-10-29).
      - Known, accepted limitation (fail-safe): a sustained loud sub-bass tone with a
        non-whole number of cycles per window can fail every window. For example, 22 Hz
        at −6 dBFS gives |mean| ≈ 0.029. The output stays muted while the tone lasts.
    - one valid ctld param block since this audiod started.
  - An xrun or a non-finite output sample re-mutes. Re-muting is a step to 0, and a muted
    period is written as exact zeros.
- **Build:**
  - Pure parts: done.
    - biquads, flat default and coeff validation: `src/common/biquad.h`, `biquad.c`;
    - limiter and ceiling rule: `src/common/limiter.h`, `limiter.c`;
    - the mute state machine, self-check, fade-in and `mute_reason` bits:
      `src/common/mutegate.h`, `mutegate.c`.
  - Still to do in audiod:
    - wiring the default state and those calls into the RT loop;
    - a rejected-block counter;
    - `mute_state`/`mute_reason` in the status block.
    - The layout for both is defined (2026-10-29): `struct status_block` in
      `src/common/status.h` has `rejected_coeff`, `rejected_param`, `mute_state` and
      `mute_reason`. Filling them in is the RT-loop work above.
  - The param block's own validation waits on its layout (Aaron, ctld). The ceiling
    predicate `limiter_ceiling_valid` is ready for it.
  - In analyzerd: on `-ESTALE` from `tbuf_publish`, reattach and republish the last
    accepted coefficient set.
  - Tests (shared): done for the pure parts (`tests/test_biquad.c`,
    `tests/test_limiter.c`, `tests/test_mutegate.c`). They cover:
    - flat biquad is unity;
    - zeroed or unstable blocks are rejected;
    - the gate stays muted until all three conditions hold;
    - an xrun re-mutes.
- **Outstanding:**
  - `SELFCHECK_MS`: H3 resolved at ~0 ms settle (issues.md H3), so the proposed 250 ms
    value isn't blocked on it anymore -- fine to finalize.
  - Aaron's ctld review (issues.md C11): ctld republishes its full param state on every
    `-ESTALE` reattach, and the output stays muted while ctld is down.

### F8. Unit ordering (decided by C12). Component: systemd. Owner: Aaron
- **Decided** (resolved_issues.md C12): audiod is the only shm creator, so
  `analyzerd.service` and `ctld.service` get `After=audiod.service` and
  `Wants=audiod.service`. Never `BindsTo=` or `Requires=`: both must survive an audiod
  restart by reattaching (§5). Device ordering for audiod itself is C13 below.
- **Build:** those two lines, when the unit files are written (with F9).
- **Outstanding:** none.

### C13. audiod binds to the UMC202HD's device unit. Component: systemd. Owner: Aaron
- **Decided:**
  - `audiod.service` gets `BindsTo=dev-umc202hd.device` and `After=dev-umc202hd.device`.
    audiod never starts without the card, and systemd stops it when the card goes.
  - `[Install] WantedBy=dev-umc202hd.device multi-user.target`. A replug then starts
    audiod again. `BindsTo=` alone only stops it, so audio would stay down after a replug.
  - Aaron's udev rule (os/) tags the ALSA **card** device (`SUBSYSTEM=="sound"`,
    `KERNEL=="card*"`, matched on the UMC202HD's USB IDs) with `TAG+="systemd"` and a
    `SYSTEMD_ALIAS` of `/dev/umc202hd`.
    - Matching the card, not the USB device, means the device unit goes active only once
      the PCMs exist.
    - The `WantedBy=` above means the rule needs no `SYSTEMD_WANTS`.
- **Build:** those lines in `systemd/audiod.service`.
- **Outstanding:**
  - udev rule: done (issues.md C13, resolved 2026-10-30);
  - on the Pi, unplug while running. Blocked on `systemd/audiod.service` existing. audiod
    may exit 10 (F10) before systemd sees the device go. Confirm the restart is dropped
    because the bound device is inactive, and that it doesn't count toward
    `StartLimitBurst`.

### C14. Stream thresholds and the stall backstop. Component: audiod. Owner: Hazel (hardware check: Aaron)
- **Decided:**
  - Both PCMs get explicit sw_params:
    - `stop_threshold` = buffer size. This is the ALSA default, but it's set on purpose: a
      hung or dead audiod underruns, and the stream stops within about 1 ms.
    - `start_threshold` = the boundary, so nothing autostarts. Playback is pre-filled with
      silence before `snd_pcm_start`. With the default threshold of 1, that pre-fill would
      start playback before capture had settled (C4).
    - `avail_min` = one period.
  - The non-RT watchdog thread is the backstop for a hang. If the status heartbeat stalls
    for about 100 ms, it logs why and calls `_exit(12)` (F10).
    - The kernel closes the PCM fds, and systemd restarts audiod at once instead of after
      `WatchdogSec=10`.
    - The thread never calls alsa-lib on the RT thread's handles, so it doesn't matter
      whether `snd_pcm_drop` is safe from a second thread.
    - The stall timer arms on the first observed heartbeat change, not on the first check
      (2026-10-29). A slow but healthy startup (PCM open, hw_params, the C4 settle) then
      can't trip it. A hang before the first heartbeat is covered by `READY=1` never being
      sent (`TimeoutStartSec=15s`, F2).
    - Once armed, any 100 ms gap trips it, so the RT thread must bump the heartbeat
      inside its settle and relock loops too.
    - A failed status read (`-EAGAIN`) counts as no progress.
    - The age runs from when a change was observed, so a delayed watchdog thread detects a
      stall late but never reports a false one.
    - `WATCHDOG=1` can be sent unconditionally every 5 s, since a heartbeat stall would
      already have exited.
  - A SIGSEGV or SIGKILL needs nothing extra: the process dies, the kernel closes the fd
    and the stream stops at the threshold.
  - Nothing in systemd writes silence. By `ExecStopPost=` time the stream has already
    stopped, and playing zeros there would start a new OUT stream.
- **Build:**
  - the sw_params, in the shared start/relock function (C4);
  - the stall decision: done (`src/common/stall.h`, `src/common/stall.c`). Calling it from
    the watchdog thread is still to do.
- **Tests (shared):** done (`tests/test_stall.c`). They cover:
  - unarmed until the first change;
  - the exact 100 ms threshold;
  - progress resets the timer;
  - wraparound;
  - no false stall from a delayed watchdog thread;
  - detection within 100 ms plus one check interval.
- **Outstanding:** whether the UMC202HD pops when its OUT stream stops (issues.md C14:
  Aaron). If it does, playback switches to free-running:
  - `stop_threshold` and `silence_size` = the boundary;
  - ALSA then fills played-out areas with zeros, so a hang plays silence and the stream
    never stops;
  - audiod has to detect late writes itself, which reworks C4's "any xrun relocks both".

### C15. Only 96 kHz at 48 or 96 frames. Component: audiod. Owner: Hazel
- **Decided:**
  - The rate is set exactly: `snd_pcm_hw_params_set_rate_resample(0)`, then
    `snd_pcm_hw_params_set_rate` with dir 0. Anything but 96000 Hz is a config error.
  - The period is 48 frames, else 96 (C2). The buffer is 2 periods.
  - Capture and playback negotiate separately. Both must end up with the same rate,
    period and buffer size, and a mismatch is the same config error.
  - A config error exits 20 (F10), which is in `RestartPreventExitStatus`.
  - `-EBUSY` on open (something else holds `hw:`) isn't a config error. It exits 1 and
    restarts normally.
- **Build:** the negotiation, in the same function as C2.
- **Outstanding:** none.

### C16. Format conversion in the RT loop. Components: audiod, src/common. Owner: Hazel (hardware check: Aaron)
- **Decided:**
  - hw_params accepts S32_LE, S24_3LE or S24_LE, negotiated separately for capture and
    playback. Anything else exits 20.
  - The conversion is picked once per lock. The RT loop switches on the format once per
    period, with the per-sample loop inside each case. It never switches per sample.
  - Capture: int → float64 for the biquads. The raw L/R is cast to float32 for the ring.
  - Playback: float64 → int with rounding (to nearest, ties to even) and clamping. Any
    non-finite sample becomes 0, ±Inf as well as NaN (2026-10-29), so a rail value never
    reaches the drivers. That period also trips the C11 non-finite re-mute.
- **Build:** done (`src/common/pcmfmt.h`, `src/common/pcmfmt.c`).
- **Tests (shared):** done (`tests/test_pcmfmt.c`). They cover:
  - 24-bit sign extension;
  - S24_3LE byte packing;
  - full-scale scaling (2^-23 and 2^-31);
  - round and clamp at both rails;
  - NaN and ±Inf → 0.
- **Outstanding:** none. **RESOLVED (2026-10-30):** `S32_LE` (24 bits), both directions;
  `S24_3LE` isn't offered by this hardware at all (issues.md C16). The S32_LE path runs
  on the unit.

### F1. The poll is time-gated. Component: audiod. Owner: Hazel (hardware check: Aaron)
- **Decided:**
  - The RT loop spins on `clock_gettime(CLOCK_MONOTONIC)` (vDSO on arm64, no syscall)
    until about 50 µs before the predicted period boundary. Only then does it poll
    `snd_pcm_avail_update()` until the period is available.
  - The prediction is re-anchored at every observed boundary, so drift between the
    monotonic clock and the audio clock can't build up.
  - It never uses `snd_pcm_avail()`, which always forces a hwsync ioctl.
  - This works whether or not each hw-pointer check is an ioctl on arm64. If it is, the
    ioctl, and the stream lock it shares with the xHCI IRQ thread, runs a few times per
    period instead of continuously.
  - Added 2026-10-29: the loop gates on the capture PCM's avail.
    - The re-anchor backdates by the frames already past the boundary:
      anchor = now − (avail − period) frames.
    - A late detection (a delayed loop, or several periods pending) therefore doesn't
      push the next window late, and a backlog is processed straight away.
    - With USB's chunked hw pointer the backdate can over-correct slightly, which only
      opens the window early.
    - The first period after `snd_pcm_start` polls for a few ms while the first URBs
      complete. That's a one-off cost per lock.
- **Build:**
  - The prediction: done (`src/common/pace.h`, `src/common/pace.c`, with
    `PACE_MARGIN_NS`).
  - The RT loop's spin and poll around it: still to do.
- **Tests (shared):** done (`tests/test_pace.c`). They run on a simulated hw pointer:
  - drift at 0 and ±200 ppm;
  - 12-frame steps with up to 30 µs of IRQ jitter;
  - a hold-up that makes one period late;
  - a hold-up long enough to leave two periods pending.

  In every case the window never opens late, it opens at most margin + jitter + 2 µs
  early, and a period is seen within one poll of becoming visible.
- **Outstanding:** none. **RESOLVED (2026-10-30):** confirmed real
  `SNDRV_PCM_IOCTL_SYNC_PTR` ioctl calls on arm64 (356 over a 5 s capture, issues.md F1).
  Structure unchanged. C1's "no syscall per check" rationale corrected in
  ProposedStructure.md §2 and rework.md's B entry.

### F2. READY=1 after lock, before unmute. Components: audiod, systemd. Owner: Hazel (unit: Aaron)
- **Decided:**
  - audiod sends `READY=1` once all four shm segments exist and both PCMs are locked
    through the C4 start sequence. That is before the C11 unmute.
  - It can't wait for unmute. ctld is `After=audiod.service` (F8), so under `Type=notify`
    ctld doesn't start until audiod is ready, and unmute needs ctld's param block.
    Waiting would deadlock.
  - systemd starts the watchdog only once start-up completes. `TimeoutStartSec=15s` keeps
    the first heartbeat well inside CLAUDE.md's 60 s, since locking is sub-second plus the
    H3 settle time.
  - A relock after an xrun never re-sends READY. The non-RT thread may send `STATUS=`
    lines.
- **Build:** the sd_notify call in audiod's main.c; `TimeoutStartSec=15s` in the unit.
- **Outstanding:** none. H3 resolved at ~0 ms settle (issues.md H3), well inside
  `TimeoutStartSec=15s` -- no raise needed.

### F9. One system user for all daemons. Components: systemd, os. Owner: Aaron
- **Decided:**
  - audiod, analyzerd and ctld run as one dedicated system user, `dspunit`, not as root.
    - A system UID means logind's `RemoveIPC` never deletes the segments.
    - No `DynamicUser=`, which implies `RemoveIPC=yes`.
  - audiod.service adds `User=dspunit`, `SupplementaryGroups=audio`, `LimitRTPRIO=80` and
    `LimitMEMLOCK=infinity` (C5).
  - audiod creates the segments with mode 0600. Per-daemon users would need 0660 and a
    shared group, and group write would still let any daemon write any segment. They'd
    add setup without adding isolation.
  - rt-tune.service stays root, since it writes /proc/irq and cpufreq.
- **Build:** the unit lines; the mode argument to `shm_seg_create`.
- **Outstanding:**
  - creating the `dspunit` system user in os/ (issues.md F9: Aaron);
  - on the Pi, if `pthread_setschedparam` returns `EPERM` despite `LimitRTPRIO=80`, the
    kernel has `CONFIG_RT_GROUP_SCHED` and audiod's cgroup has no RT budget. Exit 22
    (F10) makes that visible.

### F10 (audiod). Exit codes. Components: audiod, systemd. Owner: Hazel (unit line: Aaron; ctld codes: Aaron)
- **Decided:** these codes, mirrored in a `src/audio/` header. They avoid 126-127, 128+n
  (signals) and systemd's own 200-243. The scheme: 1 is internal, 10-19 restart, 20-29
  are config errors.

  | Code | Meaning | Restart? |
  |---|---|---|
  | 0 | clean stop (SIGTERM) | no |
  | 1 | unexpected/internal error, incl. `-EBUSY` on open (C15) | yes |
  | 10 | device lost mid-run (`-ENODEV`); absence at start is handled by `BindsTo=` (C13) | yes |
  | 11 | relock failed N times in a row (C4) | yes |
  | 12 | RT thread stalled (watchdog-thread backstop, C14) | yes |
  | 20 | rate, period or format negotiation failed (C15, C16) | **no** |
  | 21 | an shm segment can't be unlinked (`-EACCES`/`-EPERM`, C12) | **no** |
  | 22 | RT setup failed: sched, affinity or `mlockall` (C5) | **no** |

  - `RestartPreventExitStatus=20 21 22`.
  - Code 22 counts as config: `EPERM` there means the unit is missing `LimitRTPRIO` or
    `LimitMEMLOCK`, and a restart can't fix that.
  - A signal death (SIGSEGV, the watchdog's SIGKILL) restarts under `Restart=on-failure`.
- **Build:** the header; every exit path uses it; the unit line.
- **Outstanding:** ctld's codes (Aaron), in the same scheme (issues.md F10). analyzerd's
  are F10 (analyzerd) below.

### F10 (analyzerd). Exit codes. Components: analyzerd, systemd. Owner: Hazel (unit line: Aaron)
- **Decided:** these codes, in audiod's scheme. A code shared with audiod means the same
  thing in both.

  | Code | Meaning | Restart? |
  |---|---|---|
  | 0 | clean stop (SIGTERM) | no |
  | 1 | unexpected/internal error: a failed allocation (`fft_init` -ENOMEM, analysis buffers), an unexpected errno from a syscall or `shm_seg_attach` (e.g. -EMFILE, -ENOMEM), a broken invariant | yes |
  | 21 | an shm segment can't be opened (`-EACCES`/`-EPERM` from `shm_seg_attach`) | **no** |

  - `RestartPreventExitStatus=21`. Signal deaths restart, including the SIGABRT from
    `WatchdogSec=10`.
  - Every runtime condition is handled in-process, so none needs an exit:
    - shm absent, replaced or stale (C12): retry, reattach, republish (C11);
    - ring `-EPIPE`/`-ESTALE`: reset the window;
    - rejected estimates: counted.
  - No 10-19 codes. audiod's cover device loss, relock failure and an RT stall, and none
    of those exist here. analyzerd is single-threaded, so a hang goes to systemd's
    watchdog. The range stays reserved.
  - No 20 or 22. analyzerd negotiates no device and does no RT setup (F6). systemd
    applies `CPUAffinity=2` before exec, and a failure there is systemd's own 215.
  - 21 narrows C12's "never exit over shm state": a permission error is config, not
    state.
    - It isn't transient. `shm_seg_create` creates as dspunit with owner rw even before
      its `fchmod`, and a half-made segment has size 0, which is `-EPROTO` (retry).
    - So EACCES means analyzerd isn't running as `dspunit` (F9), or a foreign segment is
      in `/dev/shm`, in which case audiod exits 21 as well.
    - Retrying forever would leave a READY, heartbeating daemon that never analyses,
      with nothing in the journal saying why.
  - Non-finite analysis state isn't an exit either. A NaN in a recursive smoother would
    poison every later estimate, so analyzerd resets that state the way it resets the
    window, and counts the reset.
- **Build:** an exit-code header next to analyzerd's main.c (once F4 lands); every exit
  path uses it; `RestartPreventExitStatus=21` in `analyzerd.service`.
- **Outstanding:** none.

### F11 (unit). rt-tune.service, wanted by audiod for now. Component: systemd. Owner: Aaron
- **Decided:**
  - `systemd/rt-tune.service`: `Type=oneshot`, `RemainAfterExit=yes`,
    `ExecStart=/usr/local/sbin/rt-tune.sh`, `Before=audiod.service`,
    `Conflicts=irqbalance.service`. It runs as root.
  - For now, audiod.service gets `Wants=rt-tune.service` and `After=rt-tune.service`.
    - During bring-up, a spurious rt-tune failure must not block audio. The original
      example was the `die` when `/xhci_hcd/` matched nothing -- **fixed and verified on
      hardware 2026-10-29** (issues.md F11), so this is no longer a live spurious-failure
      risk.
    - Running untuned costs jitter, which shows up as counted xruns and dropouts.
    - It costs no safety: the C11 gate and the limiter protect the drivers either way.
- **Build:** both unit files.
- **Outstanding:**
  - **Hazel:** switch audiod to `Requires=rt-tune.service` now that rt-tune.sh fails only
    on real tuning failures (IRQ affinity, governor) -- Aaron's fix landed and is
    confirmed on hardware (issues.md F11, os/VERIFY.md). From then on, audiod never runs
    untuned. **This was the blocker; it's cleared.**

### F5. Our own FFT, not FFTW. Components: analyzerd, src/common. Owner: Hazel
- **Decided:**
  - analyzerd uses our own FFT, not FFTW or any other library. CLAUDE.md's boundary bans
    LLMs, cloud inference and pretrained models, so a math library would arguably be
    allowed. But the boundary is graded, and writing our own removes the question.
  - Writing our own also means:
    - no new `-l` flag, while there's still no aarch64 sysroot (issues.md F12);
    - no GPL code;
    - no planner state for valgrind to report, or to slow startup.
  - Algorithm: an n-point real transform runs as an n/2-point iterative radix-2 complex
    FFT plus the even/odd split. Twiddles are computed directly, not by recurrence. All
    tables and the work buffer are allocated in `fft_init`, so the transforms never
    allocate.
  - Load: 32768-point transforms, about 3 per analysis at ≈ 11.7 analyses/s, come to
    roughly 90 MFLOP/s, well under 10% of one A76 core.
  - The code lives in `src/common/`, not `analysis/`. The first `.c` in `analysis/` makes
    the Makefile link analyzerd, which fails without a main.c (issues.md F4). The FFT is
    a primitive, not the graded analysis.
- **Build:** done (`src/common/fft.h`, `src/common/fft.c`).
- **Tests (shared):** done (`tests/test_fft.c`). They cover:
  - rejected sizes;
  - agreement with a naive DFT at n = 4-1024;
  - impulse, DC and single-bin cosines at n = 32768;
  - Parseval's theorem;
  - the forward/inverse round trip to within 1e-12.
- **Outstanding:** `LDLIBS_CTLD` needs `-lm` before ctld gets sources, because every
  src/common object is linked into every daemon (issues.md F4: Aaron).

### F6. analyzerd runs SCHED_OTHER on core 2. Components: analyzerd, systemd. Owner: Hazel (unit line: Aaron)
- **Decided:** SCHED_OTHER, not FIFO 10.
  - analyzerd has no hard deadline. A late analysis only makes the correction a little
    stale, since audiod keeps applying the last published coefficients. If it falls far
    behind, the ring returns `-EPIPE` and analyzerd resets its window, which is already
    designed in.
  - It needs well under 10% of a core (F5). Core 2 isn't isolated (`isolcpus=3` only),
    but a fair CFS share is far more than that, even under the planned `stress-ng` load
    on cores 0-2.
  - FIFO would cost more than it gives:
    - A spinning bug could starve the per-CPU kworkers on core 2. That gets worse if
      F14 ends with `sched_rt_runtime_us=-1`, which removes RT throttling everywhere.
    - analyzerd would need `LimitRTPRIO`, and could hit F9's `CONFIG_RT_GROUP_SCHED`
      `EPERM`.
    - Tests and the Pi would no longer schedule it the same way.
- **Build:** `systemd/analyzerd.service` sets `CPUAffinity=2`, and sets no
  `CPUSchedulingPolicy=` or `LimitRTPRIO=`. analyzerd is single-threaded, so the
  affinity covers the whole process.
- **Outstanding:** none. If the soak shows analyses lagging because of other work on
  core 2, try `Nice=-5` (or move that work off core 2) before any RT class.

## What can start now
These parts depend only on decided items. The audiod and analyzerd items are Hazel's; the
systemd unit files are Aaron's. Tests are shared (CLAUDE.md "Both").
- **audiod:**
  - ALSA open and hw_params negotiation (C2);
  - the capture→playback start sequence and coupled xrun relock (C4);
  - RT-thread self-pinning plus `mlockall` (C5);
  - the watchdog/log thread;
  - a passthrough loop that writes silence on teardown;
  - biquads and the limiter, in src/common with unit tests;
  - the startup defaults, block validation and mute gate (C11);
  - segment create/repair and the start gen (C12, `src/common/shm.h`);
  - hw_params negotiation and its config-error rule (C15);
  - sw_params and the watchdog-thread stall exit (C14);
  - the format conversions, in src/common with unit tests (C16);
  - the time-gated poll (F1);
  - `READY=1` after lock (F2);
  - the exit-code header (F10).
- **systemd/:** `audiod.service` and `rt-tune.service` (C5, C13, F2, F9, F10, F11), and
  `analyzerd.service` (F6, F8, F9, F10).
- **analyzerd:** GCC-PHAT delay estimation, H(f) smoothing and the EQ solver, as pure
  functions tested on synthetic WAVs. They go in src/common/ for now, as the FFT did
  (F5). The first `.c` in analysis/ makes `make` link analyzerd, and that fails without a
  main.c. Once a main.c exists, the test link fails instead (it gets two `main()`s).
  Move them to analysis/ once Aaron's F4 fix lands.
