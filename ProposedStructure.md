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
- **Known quirk:** a one-time "clock source N is not valid" dmesg line at USB probe, tied to
  implicit-feedback-mode support on the UMC 202/204/404 HD series. Fixed upstream around
  kernel 5.19+ and reported harmless once that fix is present. Since §2 already requires a
  6.12+ RT kernel, this is effectively closed; it's listed for completeness, not as an open
  risk.

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
  - After every deploy, check `/sys/kernel/realtime` = 1 and `uname -v` shows
    `PREEMPT_RT`. This catches a deploy where the Pi silently boots a non-RT kernel.
  - Confirm the benefit by measurement: `cyclictest` worst-case latency on core 3 under
    load must sit well below the ~0.67 ms period (target < 100 µs). Forum reports show
    no gain in some setups, so don't assume one.
  - Fallback if no usable package exists: build the Pi kernel with
    `bcm2712_rt_defconfig` (RT is in the source tree since 6.12, so no patches are
    needed), with the build notes in `os/kernel/`.
- Cmdline `isolcpus=3 nohz_full=3 rcu_nocbs=3` (`os/cmdline.txt`). Core 3 belongs to the
  audio thread only.
- Pin the USB xHCI completion IRQ for the UMC202HD's isochronous endpoints to core 3
  (or its threaded IRQ at a priority above audiod). No device-tree overlay is needed --
  the interface enumerates under stock `snd-usb-audio`.
  Move every other IRQ to cores 0-1 and disable `irqbalance`. Set the CPU governor to
  `performance` so frequency-scaling transitions can't add latency. Both are done by
  `os/rt-tune.sh`, a shell script (Python is not allowed outside `tools/`/`ui/`). It runs
  from the `systemd/rt-tune.service` oneshot, which is ordered before audiod. The Pi's
  active cooler (Components.md) keeps full clock from thermal throttling during the 48 h soak.
- **The RT loop paces itself by polling, not blocking (mechanism B).** Each period audiod
  busy-reads the ALSA mmap control page's hw pointer directly in memory instead of calling
  `poll()`/`snd_pcm_wait()`, so there's no scheduler wakeup round-trip. Core 3 is isolated
  for audiod alone, so there's no other work the CPU time freed by blocking could go to;
  polling spends that time on tighter, more deterministic period-boundary detection
  instead. The xHCI completion IRQ (pinned to the same core, above) still runs on every
  USB completion -- it just updates the hw pointer that the spin loop reads, rather than
  waking a sleeping thread.
- systemd is the **supervisor**. Each daemon is `Type=notify` with `WatchdogSec=10` and
  `Restart=on-failure`, and sends `WATCHDOG=1` every 5 s (half the interval). This covers
  the CLAUDE.md requirement of a heartbeat within 60 s and clean exit codes. Scheduling setup goes in the unit files (`CPUAffinity=`,
  `CPUSchedulingPolicy=fifo`, `CPUSchedulingPriority=`, `LimitMEMLOCK=infinity`), not in
  code. Open every fd with `O_CLOEXEC` so restarts don't leave orphaned fds.
- **audiod's watchdog ping comes from a non-RT thread**, because `sd_notify` can block and
  the RT loop makes no blocking syscalls (it still makes non-blocking ones, e.g. the ALSA
  mmap ioctls). That thread sends `WATCHDOG=1` only if the status-block
  heartbeat counter has advanced since its last check, so a stuck RT loop still trips the
  watchdog.
- No outbound networking (graded boundary, CLAUDE.md). `os/nftables.conf` accepts inbound
  traffic only from the LAN subnet, and only to two ports: the UI port and SSH. SSH is
  there for `make deploy` and is key-only (`PasswordAuthentication no`, set in
  `os/sshd_config.d/`). Replies on those connections are allowed. Everything else, inbound
  or outbound, is dropped: no NTP, mDNS or DNS, and the Pi never starts a connection. Logs
  use monotonic timestamps, and the Pi is addressed by its LAN IP.

## 3. Processes (the "multi-process" mechanism)

| Process | Lang / dir | Sched / core | Job |
|---|---|---|---|
| **audiod** | C17, `src/audio/` | SCHED_FIFO 80, core 3, `mlockall` | ALSA `hw:` mmap full-duplex loop, paced by polling the mmap hw pointer each period (mechanism B) rather than blocking in `poll()`/`snd_pcm_wait()`. Per period it reads L/R, applies the current EQ biquads (19 × 1/3-octave, float64) and dynamics (comp/gate/**limiter**) to Source, writes to the DAC, and pushes raw L/R into the analysis ring. No malloc, no syscalls that block, no logging in the loop. |
| **analyzerd** | C17, `analysis/` | SCHED_OTHER (or FIFO 10), core 2 | Reads the ring and estimates the acoustic delay between Source and System (cross-correlation / GCC-PHAT). It then computes the smoothed transfer function H(f) = System/Source and derives *bounded* EQ corrections. These are published as a new coefficient set. Analysis uses a 32768-point FFT with a hop of 8192 (341 ms window, 2.9 Hz bins, ≈ 11.7 analyses/s). This is the graded "intelligence" and is all our code. |
| **ctld** | C17, `src/ctl/` | SCHED_OTHER, cores 0-1 | Control plane on a Unix domain socket: presets, limits, bypass, meters. It translates UI requests into parameter writes and is the only producer of the param block. It samples the status block at 20 Hz and serves meters to the ui. Always a separate daemon from the UI: the UI is Python and never maps shm, ctld is the sole producer of the shm param block, and CLAUDE.md keeps graded mechanisms out of Python. |
| **ui** | Python, `ui/` | cores 0-1 | LAN web interface. It talks only to ctld's socket and never maps shm. It pushes meters to the browser over a WebSocket at 20 Hz. **No graded mechanism lives here.** |

### IPC (all in shared memory via `shm_open`, mapped once at start)
- **audiod → analyzerd:** a lock-free SPSC ring of interleaved (Source, System) float32
  frames, 2^17 frames long (1 MiB, ≈ 1.37 s: one FFT window plus the 100 ms delay search
  plus analyzer jitter). audiod pushes 48 frames (384 B) per period at 2000 periods/s,
  i.e. 768 KB/s (target period size -- see §3 data-flow rates, 96 frames as fallback).
  If the analyzer falls behind, the *reader* drops frames and counts them. audiod never
  waits, which keeps the no-drop guarantee intact on the audio path.
- **analyzerd → audiod:** a **triple-buffered** coefficient block (19 biquads × 5
  float64 coefficients = 760 B; ≤ 1.5 KB/s at the maximum publish rate). analyzerd
  publishes only when an estimate passes its gates (Source loud enough, coherence high
  enough, change big enough), and never more than about once every 0.5–1 s. To publish, it
  fills the free slot and then atomically stores that slot's index. audiod loads the index
  once at the start of each period. If the index changed, audiod ramps from the old
  coefficients to the new ones over that period and then releases the old slot. The third
  slot means analyzerd never writes into a slot audiod is still reading or ramping from.
- **ctld → audiod:** a second **triple-buffered** param block (bypass, limits, presets,
  dynamics) with ctld as its only producer. A triple buffer allows only one writer, so
  ctld can't share analyzerd's coefficient block. audiod loads both indices once per
  period, so reading stays wait-free. ctld publishes on change, capped at 50/s. Bypass and
  limits keep working while analyzerd is down.
- **audiod → ctld:** a meter/status block (peaks, gain reduction, xrun count, heartbeat
  counter). audiod writes it every period (2000/s target) and ctld samples it at 20 Hz.

### Data-flow rates (decided 2026-10-26, fs = 96 kHz)
| Flow | Rate |
|---|---|
| UMC202HD's internal ADC/DAC clock (shared inside the box) | fixed by the hardware, not Pi-configurable |
| Pi 5 (RP1 xHCI host) ↔ UMC202HD enumeration (USB Audio Class 2.0, `snd-usb-audio`) | at boot / hotplug |
| UMC202HD → Pi 5, capture (L = Source, R = System) | USB isochronous IN, 96 kHz (VERIFY against UMC202HD) |
| Pi 5 → UMC202HD, playback (mono on L + R) | USB isochronous OUT, same rate as capture |
| RP1 xHCI host ↔ ALSA mmap buffer ↔ audiod | rate set by ALSA period/buffer config (VERIFY round-trip via loopback) |
| RP1 xHCI completion IRQ → updates ALSA hw pointer (audiod polls it, never blocks) | rate set by period size (VERIFY in `/proc/interrupts`) |
| audiod RT loop | target: 48 frames = 0.5 ms, 2000 periods/s (96 frames = 1 ms, 1000 periods/s fallback if snd-usb-audio rejects 48 -- VERIFY against its minimum period / `nrpacks`) |
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
  silence to the ALSA playback buffer before teardown, and stays muted until its first
  good period after restart. This protects drivers from pops and DC without a hardware
  mute line. **Known gap:** this only covers a clean exit path -- if audiod hangs rather
  than exiting, nothing writes silence until systemd's watchdog kills and restarts it.
- **xrun:** detect it via `snd_pcm_status` on either PCM, count it in the status block,
  and log from a non-RT thread. Recovery stops and relocks **both** capture and playback
  through the start sequence in §1 (`snd_pcm_prepare` both, start capture, let it settle,
  start playback), even when only one direction actually underran -- one restart path,
  no reliance on independently recovering a playback-only xrun while capture keeps
  streaming. The 48 h soak should show zero xruns.
- The limiter is in audiod, at sample accuracy, after EQ. Protection never depends on the
  analyzer being alive.
- **Restart policy:** errors fall into two classes.
  - *Recoverable* errors are handled in-process and only counted: xrun, a dropped ring
    frame, a rejected estimate.
  - *Unrecoverable* errors (device lost, repeated recovery failures, corrupt shm) go
    through this sequence: log, mute, goto-cleanup, exit with a distinct nonzero code.
    systemd then restarts the daemon (`Restart=on-failure`, short `RestartSec`).
    `StartLimitBurst` stops restart storms, and `RestartPreventExitStatus` covers config
    errors, where restarting won't help. A process that hangs misses its watchdog ping and
    gets killed and restarted the same way.
- The shm segments (`/dev/shm`) outlive any one process, so a restarted audiod reattaches
  and checks the segment's magic/version. It then resumes on the last-good coefficients
  instead of coming back flat. A generation counter in the ring tells analyzerd that audiod
  restarted, so it resets its delay estimate.

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
- After `make deploy`, `/sys/kernel/realtime` reads 1, `apt-mark showhold` lists the RT
  kernel, and `/proc/cmdline`, `/proc/interrupts` (USB xHCI IRQ count rising only on CPU3),
  `scaling_governor` and `nft list ruleset` match the files in `os/`. From the LAN,
  the UI port and key-based SSH answer, password SSH is rejected, and every other port is
  refused or times out. From off the LAN subnet, SSH is refused too. An
  outbound probe from the Pi (e.g. `curl` to an external host) fails.
- `make test`, `make asan` and `make memcheck` pass. `make soakcheck` reports 0 xruns
  and a watchdog heartbeat for every daemon.
