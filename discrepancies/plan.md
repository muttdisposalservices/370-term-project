# Plan: decided design changes (2026-10-28; C11 and F8 added 2026-10-29)

These are the decisions that came out of the docs review. Each one is already written into
ProposedStructure.md (and the SVGs where noted). This file turns them into work: what to
build, which component it touches, and what's still outstanding. Entries keep their
original IDs (C1-C7, C11, F8), because other files refer to them, e.g. `os/rt-tune.sh` and
`os/VERIFY.md` cite "issues.md C6/C7". The original issue text is in
[resolved_issues.md](resolved_issues.md). Open questions and hardware checks stay in
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
    F1, a hardware check: Aaron).
  - Re-export `images/dsp-architecture.png` (see "Leftover actions").

### C2. Period size: 48 frames, falling back to 96. Component: audiod. Owner: Hazel
- **Decided:** at 96 kHz, 48 frames is 4 USB microframes, 0.5 ms and 2000 periods/s. If
  snd-usb-audio's minimum period rejects 48, use 96 frames (1 ms, 1000 periods/s).
- **Build:** hw_params tries 48 first, then 96. Anything else is a config error (see
  issues.md C15).
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
  - In `systemd/audiod.service`: `LimitMEMLOCK=infinity`. If the daemons don't run as
    root, also `LimitRTPRIO=80` (issues.md F9).
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
  restart by reattaching (§5). Device ordering for audiod itself is issues.md C13.
- **Build:** those two lines, when the unit files are written (with F9).
- **Outstanding:** none beyond F9.

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
  - segment create/repair and the start gen (C12, `src/common/shm.h`).
- **analyzerd:** GCC-PHAT delay estimation, H(f) smoothing and the EQ solver, as pure
  functions in analysis/*.c files other than main.c, tested on synthetic WAVs. This works
  until main.c lands (issues.md F4).
