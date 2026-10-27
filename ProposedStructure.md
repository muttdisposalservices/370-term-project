# DSP unit: proposed OS structure (walkthrough)

## Context
The repo has only PROBLEM.md, Components.md and CLAUDE.md so far, with no code or Makefile yet.
You asked for a walkthrough of how the OS and software should be laid out on the Pi 5 for
the Source-vs-System correcting processor, using the listed parts. This is a design proposal.
Nothing gets implemented until you approve it (CLAUDE.md: multi-file work is planned first).

## 1. Signal path and clock domain (hardware → kernel)
```
Desk ─► THAT1246 (line rx) ─┐
                            ├─► PCM1808 ADC (L=Source, R=System) ─I2S─► Pi 5 (RP1 I2S, GPIO18-21)
Mic  ─► THAT1512 (preamp) ──┘                                               │
                                                                            ▼
               DRV134 ×2 ◄── PCM5102A DAC ◄──────────────I2S DOUT───────── audiod
   24.576 MHz osc ─► SCKI of PCM1808 and SCK of PCM5102A
```
- **One stereo ADC for both sensors.** Source and System are sampled on the same clock edge,
  so the two channels can never drift apart. This is what makes the sensors *fuse*: any
  delay between them is acoustic, not caused by the converters.
- **Clock topology:** 24.576 MHz = 256 × 96 kHz. **Decided: fs = 96 kHz** (48 kHz is not
  used). Make the PCM1808 the
  I2S **master** (it generates BCK/LRCK from SCKI). The Pi (RP1, clock-consumer mode) and the
  PCM5102A are slaves on the same BCK/LRCK. The result is one full-duplex I2S link with a
  single clock domain, so the ADC and DAC rates can't drift relative to each other.
- **Link rates:** BCK = 64 fs = 6.144 MHz, LRCK = 96 kHz, 32-bit slots carrying 24 valid
  bits. Each direction runs at 2 ch × 32 b × 96 kHz = 6.144 Mbit/s (768 KB/s).
- **Kernel side:** a device-tree overlay with `simple-audio-card` (Pi as bitclock/frame
  slave) exposes one ALSA card with 2 capture channels and 2 playback channels.

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
- `os/config.txt` enables the I2S overlay built from `dts/` (`dtoverlay=…`).
- Pin the RP1/I2S DMA IRQ to core 3 (or its threaded IRQ at a priority above audiod).
  Move every other IRQ to cores 0-1 and disable `irqbalance`. Set the CPU governor to
  `performance` so frequency-scaling transitions can't add latency. Both are done by
  `os/rt-tune.sh`, a shell script (Python is not allowed outside `tools/`/`ui/`). It runs
  from the `systemd/rt-tune.service` oneshot, which is ordered before audiod. The Pi's
  active cooler (Components.md) keeps full clock from thermal throttling during the 48 h soak.
- systemd is the **supervisor**. Each daemon is `Type=notify` with `WatchdogSec=10` and
  `Restart=on-failure`, and sends `WATCHDOG=1` every 5 s (half the interval). This covers
  the CLAUDE.md requirement of a heartbeat within 60 s and clean exit codes. Scheduling setup goes in the unit files (`CPUAffinity=`,
  `CPUSchedulingPolicy=fifo`, `CPUSchedulingPriority=`, `LimitMEMLOCK=infinity`), not in
  code. Open every fd with `O_CLOEXEC` so restarts don't leave orphaned fds.
- **audiod's watchdog ping comes from a non-RT thread**, because `sd_notify` is a syscall
  and the RT loop makes none. That thread sends `WATCHDOG=1` only if the status-block
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
| **audiod** | C17, `src/audio/` | SCHED_FIFO 80, core 3, `mlockall` | ALSA `hw:` mmap full-duplex loop. Per period it reads L/R, applies the current EQ biquads (19 × 1/3-octave, float64) and dynamics (comp/gate/**limiter**) to Source, writes to the DAC, and pushes raw L/R into the analysis ring. No malloc, no syscalls that block, no logging in the loop. |
| **analyzerd** | C17, `analysis/` | SCHED_OTHER (or FIFO 10), core 2 | Reads the ring and estimates the acoustic delay between Source and System (cross-correlation / GCC-PHAT). It then computes the smoothed transfer function H(f) = System/Source and derives *bounded* EQ corrections. These are published as a new coefficient set. Analysis uses a 32768-point FFT with a hop of 8192 (341 ms window, 2.9 Hz bins, ≈ 11.7 analyses/s). This is the graded "intelligence" and is all our code. |
| **ctld** (or folded into the UI backend) | C17, `src/ctl/` | SCHED_OTHER, cores 0-1 | Control plane on a Unix domain socket: presets, limits, bypass, meters. It translates UI requests into parameter writes and is the only producer of the param block. It samples the status block at 20 Hz and serves meters to the ui. |
| **ui** | Python, `ui/` | cores 0-1 | LAN web interface. It talks only to ctld's socket and never maps shm. It pushes meters to the browser over a WebSocket at 20 Hz. **No graded mechanism lives here.** |

### IPC (all in shared memory via `memfd`/`shm_open`, mapped once at start)
- **audiod → analyzerd:** a lock-free SPSC ring of interleaved (Source, System) float32
  frames, 2^17 frames long (1 MiB, ≈ 1.37 s: one FFT window plus the 100 ms delay search
  plus analyzer jitter). audiod pushes 64 frames (512 B) per period, i.e. 768 KB/s.
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
  counter). audiod writes it every period (1500/s) and ctld samples it at 20 Hz.

### Data-flow rates (decided 2026-10-26, fs = 96 kHz)
| Flow | Rate |
|---|---|
| 24.576 MHz osc → PCM1808 SCKI, PCM5102A SCK | 24.576 MHz (256 fs) |
| PCM1808 → BCK / LRCK to RP1 and PCM5102A | BCK 6.144 MHz (64 fs), LRCK 96 kHz |
| PCM1808 → RP1 I2S DIN; RP1 I2S DOUT → PCM5102A | 6.144 Mbit/s each way |
| RP1 DMA ↔ ALSA mmap buffer | 768 KB/s each way; 128-frame buffer (2 × 64) |
| DMA-completion IRQ → threaded IRQ (core 3) | ~1500/s per stream, ~3000/s total (verify in `/proc/interrupts`) |
| audiod RT loop | 64 frames = 0.667 ms, 1500 periods/s |
| audiod → SPSC ring | 1500 pushes/s × 512 B = 768 KB/s |
| ring → analyzerd | ≈ 11.7 analyses/s (hop 8192) |
| analyzerd → coeff triple buffer | ≤ 1–2 publishes/s, 760 B each, gated |
| ctld → param triple buffer | event-driven, capped at 50/s |
| audiod → status block | 1500 writes/s |
| status block → ctld → Unix socket → ui | 20 Hz |
| ui ↔ LAN browser | WebSocket push 20 Hz (a few KB/s); control requests at human rate |
| every daemon → systemd `WATCHDOG=1` | every 5 s (`WatchdogSec=10`) |
| audiod non-RT thread → journald, XSMT GPIO | event-driven |

## 4. Why this layout addresses "The risk" (latency)
- Output latency depends **only** on audiod. The total is ADC filter delay + period size ×
  periods + DAC filter delay. At 96 kHz with 64-frame periods × 2, the buffering adds about
  1.3 ms, plus some tens of samples of converter group delay.
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
- **audiod dies:** drive the PCM5102A **XSMT (soft-mute) pin** from a GPIO so the output
  mutes before teardown and stays muted until audiod's first good period. This protects
  drivers from pops and DC.
- **xrun:** detect it via `snd_pcm_status`, count it in the status block, recover with
  `snd_pcm_prepare`, and log from a non-RT thread. The 48 h soak should show zero xruns.
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
Makefile           # make / test / asan / memcheck / soakcheck / deploy (installs os/, systemd/, dts/)
src/common/        # shm ring, triple buffer, seqlock, logging, sd_notify wrapper
src/audio/         # audiod
src/ctl/           # ctld
analysis/          # analyzerd: delay est., transfer fn, EQ solver (graded)
ui/                # Python LAN UI (non-graded)
tools/             # Python capture/plot helpers (non-graded)
dts/               # device-tree overlay for the I2S card
os/                # cmdline.txt, config.txt fragments, nftables.conf, sshd_config.d/, rt-tune.sh, kernel/ (fallback build notes only)
systemd/           # unit files, incl. rt-tune.service oneshot
tests/             # unit tests (biquads, ring, delay estimator) on recorded/synthetic WAVs
```

## 7. Hardware decisions (resolved 2026-10-16) and follow-ups
1. **Dynamic mic, no phantom power.** A dynamic mic is not flat. Most have a presence peak
   and a low-frequency rolloff, and their output is low, so the THAT1512 needs roughly
   50–60 dB of gain. analyzerd needs three things to cope:
   - an inverse mic-response curve, a small table taken from the mic's published response
   - a limited band where corrections are trusted, roughly 100 Hz–8 kHz
   - a coherence gate, so the preamp noise floor is never "corrected".
2. **Pi 5 V rail → THAT1512 and PCM1808 analog VCC.**
   - PCM1808 VCC on 5 V is fine, but put a ferrite plus LC filter in front of it, because
     the Pi's 5 V rail carries switching and USB noise.
   - THAT1512 needs a split supply. A single 5 V rail can only give about ±2.5 V around a
     mid-rail ground, so check the datasheet minimum. Putting it on the Traco's ±12 V
     alongside the THAT1246 and DRV134s may be the better fit.
3. **MCP1700-3.3 fed from the Pi's 3.3 V will not regulate.** An LDO needs its input above
   3.3 V plus its dropout (~0.2 V). Feed it from 5 V instead: the MCP1700 accepts up to
   6 V in, and the PCM5102A's analog current is well within its 250 mA limit.
4. **Stereo out through the 2× DRV134** puts L and R on the two DAC channels. The ADC's
   two channels are already used (L=Source, R=mic), so Source is mono. **Decided:**
   audiod writes the same processed mono signal to both L and R. One correction curve
   serves both speakers, which matches having a single mic.

## Verification (once implementation starts)
- `aplay -l`/`arecord -l` show the card; a loopback test (DAC → ADC) measures
  round-trip latency with an impulse.
- `cyclictest -a 3 -p 90` under `stress-ng` load on cores 0-2 confirms the RT
  isolation before audio work begins.
- After `make deploy`, `/sys/kernel/realtime` reads 1, `apt-mark showhold` lists the RT
  kernel, and `/proc/cmdline`, `/proc/interrupts` (I2S IRQ count rising only on CPU3),
  `scaling_governor` and `nft list ruleset` match the files in `os/`. From the LAN,
  the UI port and key-based SSH answer, password SSH is rejected, and every other port is
  refused or times out. From off the LAN subnet, SSH is refused too. An
  outbound probe from the Pi (e.g. `curl` to an external host) fails.
- `make test`, `make asan` and `make memcheck` pass. `make soakcheck` reports 0 xruns
  and a watchdog heartbeat for every daemon.
