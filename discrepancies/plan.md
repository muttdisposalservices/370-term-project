# Plan: decided design changes (2026-10-28; C11, C13-C16, F1, F2 and F8-F11 added 2026-10-29)

These are the decisions that came out of the docs review. Each one is already written into
ProposedStructure.md (and the SVGs where noted). This file turns them into work: what to
build, which component it touches, and what's still outstanding. Entries keep their
original IDs (C1-C7, C11, C13-C16, F1, F2, F8-F11), because other files refer to them,
e.g. `os/rt-tune.sh` and `os/VERIFY.md` cite "issues.md C6/C7". The original issue text
is in [resolved_issues.md](resolved_issues.md). Open questions and hardware checks stay in
[issues.md](issues.md).

Owners follow CLAUDE.md's Ownership section. Hazel owns audiod, analyzerd, ui, tools and
systemd/ (taken on 2026-10-28, see issues.md F7). Aaron owns the hardware, the Makefile,
ctld and os/. Hardware checks on the unit are Aaron's.

## Decisions

### C1. The RT loop is paced by polling (mechanism B). Component: audiod. Owner: Hazel
- **Decided:** each period, audiod busy-polls the ALSA mmap hw pointer instead of
  blocking in `poll()`/`snd_pcm_wait()`. Core 3 is isolated for audiod alone, so the CPU
  that blocking would free has no other use.
- **Build:** the RT loop spins on the hw pointer and never waits on the xHCI IRQ.
- **Outstanding:**
  - The "no syscall per check" part of the rationale may not hold on arm64 (issues.md
    F1, a hardware check: Aaron). The loop is time-gated either way (F1 below).
  - Re-export `images/dsp-architecture.png` (see "Leftover actions").

### C2. Period size: 48 frames, falling back to 96. Component: audiod. Owner: Hazel
- **Decided:** at 96 kHz, 48 frames is 4 USB microframes, 0.5 ms and 2000 periods/s. If
  snd-usb-audio's minimum period rejects 48, use 96 frames (1 ms, 1000 periods/s).
- **Build:** hw_params tries 48 first, then 96. Anything else is a config error (C15
  below).
- **Outstanding:** whether the device accepts 48 frames (issues.md H1: Aaron).

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
- **Outstanding:** the settle time between starting capture and starting playback
  (issues.md H3: Aaron).

### C5. audiod's RT thread pins itself. Components: audiod, systemd. Owner: Hazel
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
- **Outstanding:** re-export `images/dsp-architecture.png` (see "Leftover actions").

### C6. No IRQ is pinned to core 3. Component: os. Owner: Aaron
- **Decided:** every IRQ goes to cores 0-1, the xHCI completion IRQ included, and
  `irqbalance` is disabled. This is already done in `os/rt-tune.sh` and `os/VERIFY.md`.
- **Build:** done.
- **Outstanding:**
  - Whether RP1's PCIe-MSI IRQs accept an affinity change (issues.md H4: Aaron).
  - Re-export `images/dsp-architecture.png` (see "Leftover actions").
  - OsOutline.md still describes the old core-3 pin (issues.md "Stale text").

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
  - Unmute (MUTED → FADING_IN → LIVE, linear ramp over `FADE_IN_MS`) needs all of:
    - both PCMs locked (C4);
    - `SELFCHECK_MS` of clean periods (no xrun, finite samples, DC under bound);
    - one valid ctld param block since this audiod started.
  - An xrun or a non-finite output sample re-mutes.
- **Build:**
  - In audiod:
    - the default state;
    - block validation, with a rejected-block counter;
    - the mute state machine and fade-in;
    - `mute_state`/`mute_reason` in the status block.
  - In analyzerd: on `-ESTALE` from `tbuf_publish`, reattach and republish the last
    accepted coefficient set.
  - Tests (shared): flat biquad is unity; zeroed or unstable blocks are rejected; the gate
    stays muted until all three conditions hold; an xrun re-mutes.
- **Outstanding:**
  - `SELFCHECK_MS` (proposed 250 ms) against the settle time from H3 (Aaron).
  - Aaron's ctld review (issues.md C11): ctld republishes its full param state on every
    `-ESTALE` reattach, and the output stays muted while ctld is down.

### F8. Unit ordering (decided by C12). Component: systemd. Owner: Hazel
- **Decided** (resolved_issues.md C12): audiod is the only shm creator, so
  `analyzerd.service` and `ctld.service` get `After=audiod.service` and
  `Wants=audiod.service`. Never `BindsTo=` or `Requires=`: both must survive an audiod
  restart by reattaching (§5). Device ordering for audiod itself is C13 below.
- **Build:** those two lines, when the unit files are written (with F9).
- **Outstanding:** none.

### C13. audiod binds to the UMC202HD's device unit. Component: systemd. Owner: Hazel (udev rule: Aaron)
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
  - the udev rule and the card's USB IDs (issues.md C13: Aaron);
  - on the Pi, unplug while running. audiod may exit 10 (F10) before systemd sees the
    device go. Confirm the restart is dropped because the bound device is inactive, and
    that it doesn't count toward `StartLimitBurst`.

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
  - A SIGSEGV or SIGKILL needs nothing extra: the process dies, the kernel closes the fd
    and the stream stops at the threshold.
  - Nothing in systemd writes silence. By `ExecStopPost=` time the stream has already
    stopped, and playing zeros there would start a new OUT stream.
- **Build:**
  - the sw_params, in the shared start/relock function (C4);
  - the stall check in the watchdog thread.
- **Tests (shared):** the stall decision (heartbeat delta and elapsed time → stalled or
  not) as a pure function in src/common/.
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
  - Playback: float64 → int with rounding and clamping. A NaN becomes 0, and that period
    also trips the C11 non-finite re-mute.
- **Build:** the conversions as pure functions in src/common/.
- **Tests (shared):**
  - 24-bit sign extension;
  - S24_3LE byte packing;
  - full-scale scaling (2^-23 and 2^-31);
  - round and clamp at both rails;
  - NaN → 0.
- **Outstanding:** which format the UMC202HD actually negotiates
  (`/proc/asound/card*/stream0`, issues.md C16: Aaron). That only decides which path runs
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
- **Build:** the RT loop's wait, with the margin as a named constant.
- **Outstanding:** `strace -c` on the Pi (issues.md F1: Aaron). It settles the syscall
  question and tunes the 50 µs margin, but doesn't change the structure. Reword C1's "no
  syscall per check" rationale once it lands.

### F2. READY=1 after lock, before unmute. Components: audiod, systemd. Owner: Hazel
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
- **Outstanding:** none. Raise `TimeoutStartSec` only if H3's settle time needs it.

### F9. One system user for all daemons. Components: systemd, os. Owner: Hazel (creating the user: Aaron)
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

### F10 (audiod). Exit codes. Components: audiod, systemd. Owner: Hazel (ctld codes: Aaron)
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
- **Outstanding:** analyzerd's codes (Hazel) and ctld's (Aaron), in the same scheme
  (issues.md F10).

### F11 (unit). rt-tune.service, wanted by audiod for now. Component: systemd. Owner: Hazel (rt-tune.sh: Aaron)
- **Decided:**
  - `systemd/rt-tune.service`: `Type=oneshot`, `RemainAfterExit=yes`,
    `ExecStart=/usr/local/sbin/rt-tune.sh`, `Before=audiod.service`,
    `Conflicts=irqbalance.service`. It runs as root.
  - For now, audiod.service gets `Wants=rt-tune.service` and `After=rt-tune.service`.
    - During bring-up, a spurious rt-tune failure must not block audio. Today's example
      is the `die` when `/xhci_hcd/` matches nothing.
    - Running untuned costs jitter, which shows up as counted xruns and dropouts.
    - It costs no safety: the C11 gate and the limiter protect the drivers either way.
- **Build:** both unit files.
- **Outstanding:**
  - **Hazel:** switch audiod to `Requires=rt-tune.service` once rt-tune.sh fails only on
    real tuning failures (IRQ affinity, governor). From then on, audiod never runs
    untuned.
  - That waits on Aaron's rt-tune.sh fix and the irqbalance masking (issues.md F11).

## Leftover actions from these decisions
- **Hazel:** re-export `images/dsp-architecture.png` from the SVG after the C1, C5 and C6
  edits. The diagram is Hazel's, though it has been edited jointly.

## What can start now
These parts depend only on decided items. All of them are Hazel's. Tests are shared
(CLAUDE.md "Both").
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
- **systemd/:** `audiod.service` and `rt-tune.service` (C5, C13, F2, F9, F10, F11).
- **analyzerd:** GCC-PHAT delay estimation, H(f) smoothing and the EQ solver, as pure
  functions in analysis/*.c files other than main.c, tested on synthetic WAVs. This works
  until main.c lands (issues.md F4).
