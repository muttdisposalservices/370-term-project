# DSP unit: proposed OS structure (walkthrough)

## Context
This is a walkthrough of how the OS and software are laid out on the Pi 5 for the
Source-vs-System correcting processor, using the listed parts. A Makefile, `src/`, `os/`
and `systemd/` now exist alongside PROBLEM.md, Components.md and CLAUDE.md; this remains a
design proposal for anything not yet implemented (CLAUDE.md: multi-file work is planned
first).

## 1. Signal path and clock domain (hardware → kernel)
*(Revised: the custom ADC/DAC board below was folded into a single off-the-shelf USB
interface, now decided as the Behringer UMC202HD -- see Components.md, images/Schematic.svg and
rework.md for how that decision was reached.)*
```
Desk ─► UMC202HD input 1 (combo XLR/TRS, line) ──┐
                                                  ├─► internal ADC (L=Source, R=System) ─USB─► Pi 5 (xHCI host)
Mic  ─► UMC202HD input 2 (combo XLR/TRS, MIDAS preamp) ┘                                         │
                                                                                                  ▼
          TRS→XLR adapter cables ◄── UMC202HD internal DAC ◄──USB isochronous OUT── audiod
```
- **One stereo ADC for both sensors, now inside the UMC202HD.** Source and System are sampled
  on the same clock edge inside the interface, so the two channels can never drift apart,
  and no delay is introduced by the *sampling* step itself. The System mic, however, hears
  the *processed output* coming back out of the room, not the raw Source signal, so the
  measured lag between the two channels is one total delay: the output-path latency
  (audiod + USB + converters, ≈6-8 ms unverified planning range -- see §4) plus the room's
  acoustic travel time (≈10-100 ms at ~343 m/s).
- **Clock topology:** the UMC202HD's internal ADC/DAC share one clock, entirely internal to
  the interface and not Pi-configurable. The Pi only sees the negotiated result over USB
  Audio Class 2.0 (isochronous IN/OUT), handled by `snd-usb-audio`, not by a kernel clock we
  own. Sample rate is negotiated up to the interface's 192 kHz ceiling; target 96 kHz
  (VERIFY against the device).
- **Kernel side:** no device-tree overlay, no I2S, no vendor driver. `snd-usb-audio` (stock
  driver) enumerates the UMC202HD as one ALSA card with 2 capture channels and 2 playback
  channels over USB.
- **Capture/playback linkage.** `snd-usb-audio` exposes capture and playback as separate
  ALSA PCMs, and the UMC202HD uses implicit feedback: by USB Audio Class 2.0's definition
  of that mode, the playback (OUT) endpoint's rate is derived by the host from the
  capture (IN) endpoint's packet pattern, so playback cannot lock onto a timing reference
  that isn't running yet. Start sequence is therefore forced, not a free choice: open and
  `snd_pcm_prepare` both PCMs, start capture, let it run long enough to establish a stable
  pattern, then start playback (VERIFY the settle time on hardware). A simultaneous
  `snd_pcm_link()`-style trigger isn't appropriate here for the same reason. **Any xrun
  in either direction stops and relocks both streams** through this same sequence --
  a capture xrun destroys playback's clock reference outright, and a playback-only xrun
  is handled the same way for one code path, even though capture itself was unaffected
  (see §5).
- **DECIDED (2026-10-29): negotiation and stream thresholds (plan.md C14-C16).**
  - *Rate and period.* hw_params sets exactly 96 kHz (no resampling) and 48 frames, else
    96, with a 2-period buffer. Capture and playback must agree on all three. Anything
    else is a config error, exit 20 (§5).
  - *Format.* S32_LE, S24_3LE or S24_LE, per PCM. Conversion is picked once per lock:
    int → float64 for the biquads, float32 for the raw ring push, and float64 → int with
    round and clamp (NaN or ±Inf → 0) for playback.
  - *sw_params, set on purpose on both PCMs:*
    - `stop_threshold` = buffer size, so a hung or dead audiod underruns and the stream
      stops within about 1 ms;
    - `start_threshold` = boundary, so the playback silence pre-fill can't autostart
      playback before capture settles;
    - `avail_min` = one period.
- **Known quirk:** a one-time "clock source N is not valid" dmesg line at USB probe, tied to
  implicit-feedback-mode support on the UMC 202/204/404 HD series. Fixed upstream around
  kernel 5.19+. **CONFIRMED closed on real hardware (2026-10-29), not just inferred from
  kernel version:** forced a fresh USB probe (unplug/replug while watching `dmesg`) on
  `linux-image-rpi-v8-rt` 6.18.50+rpt-rpi-v8-rt -- the message never appears.

## 2. Base OS configuration
Everything in this section is versioned under `os/` and `systemd/` and installed by
`make deploy`. Nothing is hand-edited on the Pi.
- Raspberry Pi OS Lite (64-bit) with the **PREEMPT_RT** kernel (mainline RT, 6.12+).
  PREEMPT_RT is a build-time option, so it has to come from a kernel package, not a setting.
  - Install the prebuilt RT kernel from apt. Forum reports give `linux-image-rpi-v8-rt`
    (labelled experimental, boots on Pi 5). Confirm the exact name with
    `apt search linux-image-rpi` on the Pi before recording it here.
  - `apt-mark hold` the RT kernel package so `apt upgrade` can't swap it out during
    development or the 48 h soak. Release the hold only on purpose, then re-verify.
  - After every deploy, confirm PREEMPT_RT via `uname -v`/`cat /proc/version` (both should
    show `PREEMPT_RT`) and `grep PREEMPT_RT /boot/config-$(uname -r)` (`CONFIG_PREEMPT_RT=y`).
    **Not** `/sys/kernel/realtime` -- confirmed absent on `linux-image-rpi-v8-rt`
    6.18.50+rpt-rpi-v8-rt despite `CONFIG_PREEMPT_RT=y` being true (that sysfs entry is a
    downstream Debian/SUSE/RHEL ksysfs.c patch, not something upstream RT carries
    automatically -- see `os/VERIFY.md`). This catches a deploy where the Pi silently boots
    a non-RT kernel.
  - Confirm the benefit by measurement: `cyclictest` worst-case latency on core 3 under
    load must sit well below the 0.5 ms period (target < 100 µs). Forum reports show
    no gain in some setups, so don't assume one.
  - Fallback if no usable package exists: build the Pi kernel with
    `bcm2712_rt_defconfig` (RT is in the source tree since 6.12, so no patches are
    needed), with the build notes in `os/kernel/`.
- Cmdline `isolcpus=3 nohz_full=3 rcu_nocbs=3` (`os/cmdline.txt`). Core 3 belongs to the
  audio thread only.
- **DECIDED (2026-10-28): no IRQ is pinned to core 3, including the xHCI one.** An
  earlier draft pinned the USB xHCI completion IRQ to core 3 on the theory that it needed
  to be near the RT thread it updates. It doesn't: audiod no longer blocks on it (it polls
  the mmap hw pointer in memory, mechanism B below), and a hardirq write is visible across
  cores via cache coherence in well under a period. Co-locating it would instead be a
  self-inflicted jitter source -- a hardirq preempts even `SCHED_FIFO`, and this one fires
  on every USB completion (2000/s, i.e. every 0.5 ms, the RT loop's own period), directly
  against "core 3 belongs to the audio thread only." It would also have required pinning
  one device's endpoints independently of the rest of the shared RP1 xHCI controller's
  IRQ, which the controller doesn't support. So the xHCI completion IRQ gets no special
  case: it moves to cores 0-1 along with every other IRQ, and `irqbalance` is disabled so
  nothing drifts back onto core 3. Whether RP1's PCIe-MSI IRQs accept an affinity change
  at all is a **VERIFY** that already applied to "every other IRQ" generically, not a new
  risk this decision introduces. Set the CPU governor to `performance` so
  frequency-scaling transitions can't add latency. Both are done by `os/rt-tune.sh`, a
  shell script (Python is not allowed outside `tools/`/`ui/`). It runs from the
  `systemd/rt-tune.service` oneshot, which is ordered before audiod. The Pi's active
  cooler (Components.md) keeps full clock from thermal throttling during the 48 h soak.
- **The RT loop paces itself by polling, not blocking (mechanism B).** Each period audiod
  checks the ALSA hw pointer instead of calling `poll()`/`snd_pcm_wait()`, so there's no
  scheduler wakeup round-trip. On arm64 that check is a real `SNDRV_PCM_IOCTL_SYNC_PTR`
  ioctl, not an mmap-only read (confirmed on hardware, issues.md F1) -- the time-gating in
  F1 below means it only runs a few times per period, not continuously. Core 3 is isolated
  for audiod alone, so there's no other work the CPU time freed by blocking could go to;
  polling spends that time on tighter, more deterministic period-boundary detection
  instead. The xHCI completion IRQ, which now runs on cores 0-1, not core 3 (above),
  still fires on every USB completion -- it just updates the hw pointer that the spin
  loop reads, rather than waking a sleeping thread. Since audiod never waits on it, it
  doesn't need to share a core with the thread it updates.
  - **DECIDED (2026-10-29): the poll is time-gated (plan.md F1).** The loop spins on
    `clock_gettime(CLOCK_MONOTONIC)` (vDSO, no syscall) until about 50 µs before the
    predicted boundary, re-anchored every period. Only then does it poll
    `snd_pcm_avail_update()`, never `snd_pcm_avail()`. On arm64 the hw-pointer check is a
    real ioctl (`SNDRV_PCM_IOCTL_SYNC_PTR`) that takes the stream lock -- confirmed on
    hardware (issues.md F1). Time-gating keeps it to a few calls per period, not
    continuous.
    - The loop gates on the capture PCM's avail.
    - Each re-anchor is backdated by the frames already past the boundary:
      anchor = now − (avail − period) frames. So a late detection (a delayed loop, or
      several periods pending) doesn't push the next window late, and a backlog is
      processed straight away.
    - USB's hw pointer moves in packet-sized steps, so the backdate can over-correct
      slightly. That only opens the window early.
    - The first period after `snd_pcm_start` polls for a few ms while the first URBs
      complete, once per lock (`src/common/pace.h`).
- systemd is the **supervisor**. Each daemon is `Type=notify` with `WatchdogSec=10` and
  `Restart=on-failure`, and sends `WATCHDOG=1` every 5 s (half the interval). The
  watchdog covers the CLAUDE.md heartbeat requirement and catches hangs. Clean exit codes
  come from the §5 restart policy and its exit-code table, not from the watchdog. audiod
  sends `READY=1` once its shm segments exist and both PCMs are locked, before unmute.
  ctld is ordered after it and the unmute needs ctld, so waiting for unmute would
  deadlock. `TimeoutStartSec=15s` (plan.md F2). For
  single-threaded daemons (analyzerd, ctld), scheduling setup goes in the unit files,
  not in code. Both stay `SCHED_OTHER` (no `CPUSchedulingPolicy=`), and analyzerd gets
  `CPUAffinity=2` (plan.md F6).
  audiod's unit sets only `LimitMEMLOCK=infinity` (for `mlockall`) -- no
  `CPUAffinity=`/`CPUSchedulingPolicy=`, see below for why. Open every fd with
  `O_CLOEXEC` so restarts don't leave orphaned fds.
- **audiod's watchdog ping comes from a non-RT thread**, because `sd_notify` can block and
  the RT loop makes no blocking syscalls (it still makes non-blocking ones, e.g. the ALSA
  mmap ioctls). That thread sends `WATCHDOG=1` only if the status-block
  heartbeat counter has advanced since its last check, so a stuck RT loop still trips the
  watchdog. If the heartbeat stalls for about 100 ms, the thread logs why and exits with
  code 12 (§5), so systemd restarts audiod at once instead of after `WatchdogSec`. It
  never calls alsa-lib on the RT thread's handles (plan.md C14).
  - The stall timer arms on the first observed heartbeat change, not on the first check.
    A slow but healthy startup (PCM open, hw_params, the capture settle) can't trip it,
    and a hang before the first heartbeat is caught because `READY=1` is never sent
    (`TimeoutStartSec`).
  - Once armed, any 100 ms gap trips it, so the RT thread bumps the heartbeat inside its
    settle and relock loops too.
  - A failed status read counts as no progress.
  - The age runs from when a change was observed, so a delayed watchdog thread detects a
    stall late but never reports a false one (`src/common/stall.h`).
  - **DECIDED (2026-10-28): audiod's RT thread pins itself in code; nothing demotes the
    non-RT thread.** A unit-file `CPUAffinity=`/`CPUSchedulingPolicy=fifo` would apply to
    the whole process at `exec`, and every subsequently `pthread_create`'d thread inherits
    that as its default -- systemd has no syntax to target one thread of a multi-threaded
    process. Setting it at the unit level would therefore leave the watchdog/logging
    thread at SCHED_FIFO 80 on core 3 unless it remembered to demote itself, which is the
    dangerous default to ship: a forgotten demotion silently puts a thread that does
    blocking journald writes on the one core `isolcpus=3 nohz_full=3 rcu_nocbs=3` is
    supposed to reserve for the RT loop alone, and the failure only shows up as rare
    jitter during the soak, not as an obvious startup error.
    Instead, audiod's RT thread calls `pthread_setschedparam(SCHED_FIFO, 80)` and
    `pthread_setaffinity_np()` (core 3) **on itself** right after it starts. The
    watchdog/logging thread gets no special treatment: it stays at the default
    `SCHED_OTHER` and default affinity, and since `isolcpus=3` already removes core 3 from
    the kernel's general scheduling domain, an unaffined thread won't land there on its
    own. If a third thread is ever added, it's safe by default without anyone having to
    remember to demote it. `mlockall()` is process-wide, so which thread calls it doesn't
    matter and stays unaffected by this split.
- **DECIDED (2026-10-29): unit files (plan.md C13, F9, F11).**
  - *User.* All three daemons run as one system user, `dspunit`, not root. A system UID
    is never subject to logind's `RemoveIPC`, and `DynamicUser=` isn't used because it
    implies `RemoveIPC=yes`. audiod adds `SupplementaryGroups=audio`, `LimitRTPRIO=80`
    and `LimitMEMLOCK=infinity`. Segments are mode 0600. rt-tune.service stays root.
  - *Device.* audiod is `BindsTo=`/`After=dev-umc202hd.device` and
    `WantedBy=dev-umc202hd.device`, so an unplug stops it and a replug starts it again.
    The device unit comes from a udev rule in os/ that tags the ALSA card.
  - *Tuning.* audiod `Wants=`/`After=rt-tune.service` for now, so a spurious rt-tune
    failure costs jitter, not all audio. It moves to `Requires=` once rt-tune.sh fails
    only on real tuning failures.
- No outbound networking (graded boundary, CLAUDE.md), **for the deployed product.**
  `os/nftables.conf` accepts inbound traffic only from the LAN subnet, and only to two
  ports: the UI port and SSH. SSH is there for `make deploy` and is key-only
  (`PasswordAuthentication no`, set in `os/sshd_config.d/`). Replies on those connections
  are allowed. Everything else, inbound or outbound, is dropped: no NTP, mDNS or DNS, and
  the Pi never starts a connection. Logs use monotonic timestamps, and the Pi is addressed
  by its LAN IP.
  - **DECIDED (2026-10-28): apt happens once during bring-up, before this firewall is
    ever loaded; `make deploy` never installs packages.** The RT kernel (above) comes from
    apt, which needs outbound HTTP(S) and DNS -- exactly what this firewall drops. The
    graded "no network calls" rule (CLAUDE.md) is about the running *product*, not the
    one-time step that puts an OS and RT kernel on the SD card before the product exists.
    Sequencing is forced by the Makefile itself: `make deploy` is copy-only
    (`Makefile:230-260`) and its printed install steps never touch apt, so by construction
    the RT kernel has to already be on the Pi before `make deploy`/`nftables` enter the
    picture at all. In order: flash Raspberry Pi OS Lite, boot it on an open network,
    `apt update && apt install` the RT kernel package and `apt-mark hold` it (both
    one-time, over that normal unfirewalled connection), *then* run `make deploy` and
    enable `os/nftables.conf` (the Makefile's own printed steps already warn to keep a
    console session open before that, since a bad rule can lock out the only SSH path --
    see `os/VERIFY.md`). If the RT kernel package is ever swapped later, repeat the same
    bootstrap -- unhold, update, re-hold -- before the firewall is (re-)enabled, then
    re-run the Verification checklist below.

## 3. Processes (the "multi-process" mechanism)

| Process | Lang / dir | Sched / core | Job |
|---|---|---|---|
| **audiod** | C17, `src/audio/` | RT thread: SCHED_FIFO 80, core 3 (self-pinned in code, §2); non-RT thread: SCHED_OTHER, cores 0-1 (default affinity, not pinned); `mlockall` (process-wide) | ALSA `hw:` mmap full-duplex loop, paced by polling the mmap hw pointer each period (mechanism B) rather than blocking in `poll()`/`snd_pcm_wait()`. Per period it reads L/R, applies the current EQ biquads (19 × 1/3-octave, float64) and dynamics (comp/gate/**limiter**) to Source, writes to the DAC, and pushes raw L/R into the analysis ring. No malloc, no syscalls that block, no logging in the loop. |
| **analyzerd** | C17, `analysis/` | SCHED_OTHER, core 2 (`CPUAffinity=2` in the unit, plan.md F6) | Reads the ring and estimates the acoustic delay between Source and System (cross-correlation / GCC-PHAT). It then computes the smoothed transfer function H(f) = System/Source and derives *bounded* EQ corrections. These are published as a new coefficient set. Analysis uses a 32768-point FFT (our own radix-2, `src/common/fft.c`, plan.md F5) with a hop of 8192 (341 ms window, 2.9 Hz bins, ≈ 11.7 analyses/s). This is the graded "intelligence" and is all our code. |
| **ctld** | C17, `src/ctl/` | SCHED_OTHER, cores 0-1 | Control plane on a Unix domain socket: presets, limits, bypass, meters. It translates UI requests into parameter writes and is the only producer of the param block. It samples the status block at 20 Hz and serves meters to the ui. Always a separate daemon from the UI: the UI is Python and never maps shm, ctld is the sole producer of the shm param block, and CLAUDE.md keeps graded mechanisms out of Python. |
| **ui** | Python, `ui/` | cores 0-1 | LAN web interface. It talks only to ctld's socket and never maps shm. It pushes meters to the browser over a WebSocket at 20 Hz. **No graded mechanism lives here.** |

### IPC (all in shared memory via `shm_open`)
audiod creates all four segments; analyzerd and ctld only attach, and attach again by name
if audiod replaces a segment (`src/common/shm.h`, §5, resolved_issues.md C12).
- **audiod → analyzerd:** a lock-free SPSC ring of interleaved (Source, System) float32
  frames, 2^17 frames long (1 MiB, ≈ 1.37 s: one FFT window plus the 100 ms delay search
  plus analyzer jitter). audiod pushes 48 frames (384 B) per period at 2000 periods/s,
  i.e. 768 KB/s (target period size -- see §3 data-flow rates, 96 frames as fallback).
  If the analyzer falls behind, the *reader* drops frames and counts them. audiod never
  waits, which keeps the no-drop guarantee intact on the audio path. audiod stores a
  `claim` counter before writing each chunk and a `commit` counter after it. The reader
  copies frames below `commit`, then re-checks `claim`: if the writer has reached its
  frames, even mid-chunk, the copy is discarded (`src/common/ring.c`, resolved_issues.md C9).
- **analyzerd → audiod:** a **triple-buffered** coefficient block (19 biquads × 5
  float64 coefficients = 760 B; ≤ 1.5 KB/s at the maximum publish rate). analyzerd
  publishes only when an estimate passes its gates (Source loud enough, coherence high
  enough, change big enough), and never more than about once every 0.5–1 s. Each side
  privately owns one slot, and a shared atomic word names the third (the "middle") plus a
  fresh flag and a generation (`src/common/tbuf.c`, issues.md C8). To publish, analyzerd
  fills its own slot and swaps it into the middle with the flag set. audiod loads the word
  once at the start of each period. Only if the flag is set does it swap its slot for the
  middle and copy the new coefficients into private state. It then ramps from the old to the
  new coefficients over that period using only private copies, so it never holds a second
  slot. analyzerd therefore always has a slot of its own to write.
- **ctld → audiod:** a second **triple-buffered** param block, `struct param_block` in
  `src/common/param.h` (bypass, limiter ceiling in dBFS), with ctld as its only
  producer. Presets stay entirely on ctld's side: it resolves a preset to concrete
  bypass/ceiling values before publishing, so audiod never sees a preset id. A triple
  buffer allows only one writer, so ctld can't share analyzerd's coefficient block.
  audiod loads both indices once per period, so reading stays wait-free. ctld publishes
  on change, capped at 50/s. Bypass and limits keep working while analyzerd is down.
  After an audiod restart, both producers get `-ESTALE`, reattach and republish their
  full current state; audiod stays muted until ctld's republish arrives and passes
  `param_valid()` -- magic/version first, so a zeroed (never-published) slot is
  rejected regardless of what its other fields contain (§5, C11, issues.md C11).
- **audiod → ctld:** a meter/status block, `struct status_block` in
  `src/common/status.h` (peaks, gain reduction, xrun count, heartbeat counter,
  `mute_state` and `mute_reason`, and separate rejected-block counts for coeff and param
  blocks). audiod writes it every period (2000/s target) and ctld samples it at 20 Hz.
  - Meters are linear and windowed, not per period: each publish covers the last two
    50 ms windows (`METER_WINDOW_MS`) and carries a window index. So a 20 Hz reader sees
    every sample, and can tell when it skipped or re-read a window.
  - A single-writer seqlock (`src/common/seqlock.h`) protects it from torn reads. Its
  other reader is audiod's own watchdog thread. soakcheck reads status through ctld's
  socket, not shm (resolved_issues.md C10).

### Data-flow rates (decided 2026-10-26, fs = 96 kHz)
| Flow | Rate |
|---|---|
| UMC202HD's internal ADC/DAC clock (shared inside the box) | fixed by the hardware, not Pi-configurable |
| Pi 5 (RP1 xHCI host) ↔ UMC202HD enumeration (USB Audio Class 2.0, `snd-usb-audio`) | at boot / hotplug |
| UMC202HD → Pi 5, capture (L = Source, R = System) | USB isochronous IN, 96 kHz (VERIFY against UMC202HD) |
| Pi 5 → UMC202HD, playback (mono on L + R) | USB isochronous OUT, same rate as capture |
| RP1 xHCI host ↔ ALSA mmap buffer ↔ audiod | rate set by ALSA period/buffer config (VERIFY round-trip via loopback) |
| RP1 xHCI completion IRQ (cores 0-1, not core 3) → updates ALSA hw pointer (audiod polls it, never blocks) | rate set by period size (VERIFY in `/proc/interrupts`) |
| audiod RT loop | 48 frames = 0.5 ms, 2000 periods/s -- confirmed accepted on hardware, both directions (issues.md H1); no 96-frame fallback needed |
| audiod → SPSC ring | 2000 pushes/s × 384 B = 768 KB/s |
| ring → analyzerd | ≈ 11.7 analyses/s (hop 8192) |
| analyzerd → coeff triple buffer | ≤ 1–2 publishes/s, 760 B each, gated |
| ctld → param triple buffer | event-driven, capped at 50/s |
| audiod → status block | 2000 writes/s target |
| status block → ctld → Unix socket → ui | 20 Hz |
| ui ↔ LAN browser | WebSocket push 20 Hz (a few KB/s); control requests at human rate |
| every daemon → systemd `WATCHDOG=1` | every 5 s (`WatchdogSec=10`) |
| audiod non-RT thread → journald | event-driven (xruns etc.), target 0 during the 48 h soak |
| audiod → UMC202HD DAC via ALSA playback buffer (silence, software mute) | on events: silence at teardown/startup, no GPIO mute pin |

## 4. Why this layout addresses "The risk" (latency)
- **Output latency is dominated by audiod, but not only by audiod.** The total is ADC
  filter delay + USB isochronous buffering (interface + host queue) + period size ×
  periods + DAC filter delay. audiod's own share, at 96 kHz with 48-frame periods × 2
  (target; 96 frames as fallback -- see §3), is about 1.0 ms. The USB + converter share is
  not audiod's to control and has not been measured on our Pi/kernel/config. Third-party
  measurements of this same interface give a planning range, not a number for our setup:
  a Linux/JACK test (44.1 kHz, 64-frame period × 2) reported output-only latency of
  6.3-7.7 ms, and a Windows test reported ~8 ms total processing latency -- different OS,
  driver, and buffer settings than ours, and shrinking the buffer made the Windows figure
  *worse* (8 ms → 12 ms), a reminder that smaller periods don't guarantee lower end-to-end
  latency once USB/driver buffering dominates. Treat **≈6-8 ms total output-path latency**
  as an unverified planning range until the DAC→ADC loopback measurement in Verification
  gives a real number for this hardware and kernel.
- The analysis path can be slow, around 100s of ms, without hurting latency. The mic is
  physically 10-100 ms "late" anyway, because sound travels about 343 m/s through the room.
  Correction is a slow closed loop over seconds, not per-sample.
- **No delay is added to the output.** The analyzer measures one total delay
  (audiod + converters + air) and delays its *own copy* of Source by that amount before
  comparing it with the mic. A deliberate speaker-alignment delay would be a user setting,
  not compensation. If a lookahead limiter is used, its lookahead counts against the
  latency budget, so keep it at ≤1 ms or leave it out.
- Slow adaptation is a requirement, not just tolerated. The mic hears the system, so the
  EQ loop is a feedback loop. Keep it stable with heavy time smoothing, frequency smoothing
  (≈1/3-octave), a cap on boost (prefer cuts), and a freeze when the Source level is too
  low or too uncorrelated to trust.

## 5. Failure behaviour (soak-test relevant)
- **analyzerd dies:** audiod keeps running on the last-good coefficients and systemd
  restarts the analyzer. Audio is unaffected.
- **audiod dies:** no GPIO mute pin exists on the UMC202HD, so audiod mutes by writing
  silence to the ALSA playback buffer before teardown, and after restart stays muted until
  the unmute gate below passes. This protects drivers from pops and DC without a hardware
  mute line. The silence write only covers a clean exit. A crash (SIGSEGV, SIGKILL) or a
  hang is covered by `stop_threshold` = buffer size (§1): the playback stream underruns
  and stops within about 1 ms. A hang also trips the watchdog thread's stall exit (§2).
  **Still open:** whether the UMC202HD pops when its OUT stream stops (issues.md C14). If
  it does, playback switches to free-running silence fill (plan.md C14).
- **xrun:** detect it via `snd_pcm_status` on either PCM, count it in the status block,
  and log from a non-RT thread. Recovery stops and relocks **both** capture and playback
  through the start sequence in §1 (`snd_pcm_prepare` both, start capture, let it settle,
  start playback), even when only one direction actually underran -- one restart path,
  no reliance on independently recovering a playback-only xrun while capture keeps
  streaming. The 48 h soak should show zero xruns.
- The limiter is in audiod, at sample accuracy, after EQ. Protection never depends on the
  analyzer being alive.
- **Startup defaults and the mute gate (C11, plan.md).** audiod boots before any
  coefficients or params exist, and `tbuf_init` zeroes every slot. An all-zero biquad
  outputs silence and an all-zero param block gives a 0 ceiling, so a fresh slot is never
  used as a default.
  - *Defaults.* On every start, restarts included, audiod's private state is compiled in,
    not read from shm:
    - EQ: flat, all 19 biquads unity (b0 = 1, b1 = b2 = a1 = a2 = 0).
    - Limiter: on, brickwall peak, -6 dBFS ceiling, no lookahead. It has instant attack
      and a fixed 50 ms exponential release (`LIMITER_RELEASE_MS`). Release can move into
      the param block once that layout exists.
    - Bypass: off.
    - Output: muted. audiod still runs the whole chain (capture, EQ, limiter, ring push)
      and writes zeros to playback, so the self-check measures the real processed output.
      analyzerd's gates reject what it captures meanwhile.
  - *Replacing a default.* A block replaces the current state only when
    `tbuf_consumer_poll` returns 1 and the block validates:
    - coefficients are all finite and every biquad is stable (|a2| < 1, |a1| < 1 + a2);
    - no band has an all-zero numerator (b0 = b1 = b2 = 0). An all-zero slot is finite
      and stable, so this is the rule that rejects it;
    - the limiter ceiling is finite and ≤ 0 dBFS.

    A block that fails is dropped, counted in the status block, and the current state is
    kept.
  - *Unmute gate* (MUTED → FADING_IN → LIVE). All three must hold:
    - (a) both PCMs are locked through the §1 start sequence;
    - (b) `SELFCHECK_MS` (250 ms; H3 resolved at ~0 ms settle, issues.md H3, so this value
      isn't blocked on it) of
      consecutive clean periods: no xrun, every capture and processed sample finite, and
      the |mean| of the processed output under a DC bound;
      - The mean is taken over the whole window, under `DC_BOUND` = 0.01 (−40 dBFS). A
        single 0.5 ms period is too short, since bass looks like DC over one period.
      - While muted and locked, the check re-runs every window until ctld's first param
        block arrives, so the unmute rests on the most recent window.
      - Accepted, fail-safe limitation: a sustained loud sub-bass tone with a non-whole
        number of cycles per window can fail every window. For example, 22 Hz at −6 dBFS
        gives |mean| ≈ 0.029, and the output stays muted while the tone lasts
        (`src/common/mutegate.h`).
    - (c) ctld has published at least one valid param block since this audiod started,
      so the user's ceiling is in force. While ctld is down the output stays muted.

    Unmuting is a linear gain ramp from 0 to 1 over `FADE_IN_MS` (proposed 20 ms), never a
    step.
  - *Re-muting.*
    - Any xrun returns to MUTED, and (b) re-runs after the relock. (c) stays satisfied for
      the life of the process.
    - A non-finite output sample mutes on that period.
    - Unrecoverable exits keep the silence-on-teardown above. Crashes and hangs rely on
      the stream thresholds and the stall exit (plan.md C14).
  - *Between processes.* The status block carries `mute_state` and a `mute_reason`
    bitmask (`WAIT_LOCK | WAIT_SELFCHECK | WAIT_PARAMS`), so ctld and ui can show why the
    output is muted. After an audiod restart, ctld and analyzerd republish their current
    state on `-ESTALE` (§3). ctld's republish lifts `WAIT_PARAMS`, and analyzerd's brings
    back its last accepted coefficients.
- **Restart policy:** errors fall into two classes.
  - *Recoverable* errors are handled in-process and only counted: xrun, a dropped ring
    frame, a rejected estimate.
  - *Unrecoverable* errors (device lost, repeated recovery failures) go through this
    sequence: log, mute, goto-cleanup, exit with a distinct nonzero code. systemd then
    restarts the daemon (`Restart=on-failure`, short `RestartSec`). `StartLimitBurst`
    stops restart storms, and `RestartPreventExitStatus` covers config errors, where
    restarting won't help. A process that hangs misses its watchdog ping and gets killed
    and restarted the same way.
  - *audiod's exit codes* (plan.md F10). 1 is internal, 10-19 restart, 20-29 are config
    errors. Signal deaths restart.

    | Code | Meaning | Restart? |
    |---|---|---|
    | 0 | clean stop (SIGTERM) | no |
    | 1 | unexpected/internal error, incl. `-EBUSY` on open | yes |
    | 10 | device lost mid-run (`-ENODEV`) | yes |
    | 11 | relock failed N times in a row | yes |
    | 12 | RT thread stalled (watchdog-thread backstop) | yes |
    | 20 | rate, period or format negotiation failed | no |
    | 21 | an shm segment can't be unlinked (`-EACCES`/`-EPERM`) | no |
    | 22 | RT setup failed: sched, affinity or `mlockall` | no |

    `RestartPreventExitStatus=20 21 22`.
  - *analyzerd's exit codes* (plan.md F10). Same scheme; a shared code means the same
    thing. Every runtime condition is handled in-process, so the table is short.

    | Code | Meaning | Restart? |
    |---|---|---|
    | 0 | clean stop (SIGTERM) | no |
    | 1 | unexpected/internal error (allocation, unexpected errno, broken invariant) | yes |
    | 21 | an shm segment can't be opened (`-EACCES`/`-EPERM` from `shm_seg_attach`) | no |

    `RestartPreventExitStatus=21`.
  - Corrupt shm is *not* unrecoverable: a restart would find the same segment, so audiod
    repairs it instead (below). The one shm config error is a segment audiod can't
    unlink (`-EACCES`/`-EPERM`).
- **Segment lifecycle (resolved_issues.md C12).** The shm segments (`/dev/shm`) outlive
  any one process and are never unlinked on exit.
  - *audiod creates and repairs.* On every start, `shm_seg_create` maps each segment at
    its exact size. A segment of the wrong size or type, or one audiod can't open
    read-write, is unlinked and recreated. A bad header in a usable segment is
    re-initialised by `ring_init`/`tbuf_init`/`seqlock_init`. audiod never exits over
    segment contents.
  - *One generation per audiod start*, shared by all four segments: the previous gen
    (read from the old status block) + 1, or a clock seed if that header was bad.
    Readers see `-ESTALE` (the ring's tells analyzerd to reset its delay estimate).
  - *analyzerd and ctld only attach.* They never exit over shm state (a permission error,
    `-EACCES`/`-EPERM`, is config, not state: analyzerd exits 21, and ctld's code is
    Aaron's, plan.md F10): they retry until
    audiod has made a segment, reattach on `-ESTALE`, and after a stall check
    `shm_seg_replaced` and attach by name again if audiod recreated the segment.
  - *Coefficients after an audiod restart* come back only through analyzerd's republish
    above; the EQ is flat until then. `tbuf_init` zeroes the slots, so nothing left in
    shm by a crashed audiod is trusted.
  - *Units:* analyzerd.service and ctld.service are `After=`/`Wants=audiod.service`,
    never `BindsTo=`/`Requires=`, so they survive an audiod restart.

## 6. Proposed repo layout
```
Makefile           # make / test / asan / memcheck / soakcheck / deploy (installs os/, systemd/)
src/common/        # shm ring, triple buffer, seqlock, logging, sd_notify wrapper
src/audio/         # audiod
src/ctl/           # ctld
analysis/          # analyzerd: delay est., transfer fn, EQ solver (graded)
ui/                # Python LAN UI (non-graded)
tools/             # Python capture/plot helpers (non-graded)
os/                # cmdline.txt, config.txt fragments, nftables.conf, sshd_config.d/, rt-tune.sh, kernel/ (fallback build notes only)
systemd/           # unit files, incl. rt-tune.service oneshot
tests/             # unit tests (biquads, ring, delay estimator) on recorded/synthetic WAVs
```

## 7. Hardware decisions (resolved 2026-10-16, revised for the UMC202HD) and follow-ups
1. **Dynamic mic, no phantom power.** A dynamic mic is not flat. Most have a presence peak
   and a low-frequency rolloff, and their output is low, so the UMC202HD's input 2 MIDAS
   preamp is run at roughly 50–60 dB of gain. analyzerd needs three things to cope:
   - an inverse mic-response curve, a small table taken from the mic's published response
   - a limited band where corrections are trusted, roughly 100 Hz–8 kHz
   - a coherence gate, so the preamp noise floor is never "corrected".
2. **Power and analog gain staging are now internal to the UMC202HD.** The custom board's
   supply-rail problems (Pi 5 V → preamp/ADC analog VCC filtering, the split-supply need
   for a mic preamp, the 3.3 V LDO dropout issue) no longer apply -- they were specific to
   the THAT1512/PCM1808/MCP1700 parts, which are removed (see Components.md). The
   UMC202HD is bus-powered and self-contained.
3. **Stereo out: ADC's two channels are already used (L=Source, R=mic), so Source stays
   mono on the way out too.** **Decided:** audiod writes the same processed mono signal
   to both output channels. One correction curve serves both speakers, which matches
   having a single mic. This decision is unchanged by the UMC202HD swap -- it's a
   software choice, not a hardware one.

## Verification (once implementation starts)
- `aplay -l`/`arecord -l` show the card; a loopback test (DAC → ADC) measures
  round-trip latency with an impulse.
- `cyclictest -a 3 -p 90` under `stress-ng` load on cores 0-2 confirms the RT
  isolation before audio work begins.
- After `make deploy`, PREEMPT_RT is confirmed via `uname -v`/`/proc/version`/
  `CONFIG_PREEMPT_RT=y` (not `/sys/kernel/realtime`, which doesn't exist on this kernel
  build -- see §2 and `os/VERIFY.md`), `apt-mark showhold` lists the RT
  kernel, and `/proc/cmdline`, `/proc/interrupts` (USB xHCI IRQ count rising only on
  CPU0/1, never CPU3), `scaling_governor` and `nft list ruleset` match the files in
  `os/`. From the LAN,
  the UI port and key-based SSH answer, password SSH is rejected, and every other port is
  refused or times out. From off the LAN subnet, SSH is refused too. An
  outbound probe from the Pi (e.g. `curl` to an external host) fails.
- `make test`, `make asan` and `make memcheck` pass. `make soakcheck` reports 0 xruns
  and a watchdog heartbeat for every daemon.
