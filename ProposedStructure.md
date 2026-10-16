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
- **Clock topology:** 24.576 MHz = 256 × 96 kHz (or 512 × 48 kHz). Make the PCM1808 the
  I2S **master** (it generates BCK/LRCK from SCKI). The Pi (RP1, clock-consumer mode) and the
  PCM5102A are slaves on the same BCK/LRCK. The result is one full-duplex I2S link with a
  single clock domain, so the ADC and DAC rates can't drift relative to each other.
- **Kernel side:** a device-tree overlay with `simple-audio-card` (Pi as bitclock/frame
  slave) exposes one ALSA card with 2 capture channels and 2 playback channels.

## 2. Base OS configuration
- Raspberry Pi OS Lite (64-bit) with the **PREEMPT_RT** kernel (mainline RT, 6.12+).
- Cmdline `isolcpus=3 nohz_full=3 rcu_nocbs=3`. Core 3 belongs to the audio thread only.
- Pin the RP1/I2S DMA IRQ to core 3 (or its threaded IRQ at a priority above audiod).
  Move every other IRQ to cores 0-1.
- Set the CPU governor to `performance` so frequency-scaling transitions can't add latency.
- systemd is the **supervisor**. Each daemon is `Type=notify` with `WatchdogSec=` and
  `Restart=on-failure`, which covers the CLAUDE.md requirement of a heartbeat within 60 s
  and clean exit codes. Open every fd with `O_CLOEXEC` so restarts don't leave orphaned fds.
- No outbound networking (graded boundary). The firewall allows only inbound LAN to the UI port.

## 3. Processes (the "multi-process" mechanism)

| Process | Lang / dir | Sched / core | Job |
|---|---|---|---|
| **audiod** | C17, `src/audio/` | SCHED_FIFO 80, core 3, `mlockall` | ALSA `hw:` mmap full-duplex loop. Per period it reads L/R, applies the current EQ biquads and dynamics (comp/gate/**limiter**) to Source, writes to the DAC, and pushes raw L/R into the analysis ring. No malloc, no syscalls that block, no logging in the loop. |
| **analyzerd** | C17, `analysis/` | SCHED_OTHER (or FIFO 10), core 2 | Reads the ring and estimates the acoustic delay between Source and System (cross-correlation / GCC-PHAT). It then computes the smoothed transfer function H(f) = System/Source and derives *bounded* EQ corrections. These are published as a new coefficient set. This is the graded "intelligence" and is all our code. |
| **ctld** (or folded into the UI backend) | C17, `src/ctl/` | SCHED_OTHER, cores 0-1 | Control plane on a Unix domain socket: presets, limits, bypass, meters. It translates UI requests into parameter writes. |
| **ui** | Python, `ui/` | cores 0-1 | LAN web interface. It talks only to ctld's socket and reads meters read-only. **No graded mechanism lives here.** |

### IPC (all in shared memory via `memfd`/`shm_open`, mapped once at start)
- **audiod → analyzerd:** a lock-free SPSC ring of interleaved (Source, System) frames.
  If the analyzer falls behind, the *reader* drops frames and counts them. audiod never
  waits, which keeps the no-drop guarantee intact on the audio path.
- **analyzerd/ctld → audiod:** a **triple-buffered** coefficient block. analyzerd
  publishes only when an estimate passes its gates (Source loud enough, coherence high
  enough, change big enough), and never more than about once every 0.5–1 s. To publish, it
  fills the free slot and then atomically stores that slot's index. audiod loads the index
  once at the start of each period. If the index changed, audiod ramps from the old
  coefficients to the new ones over that period and then releases the old slot. The third
  slot means analyzerd never writes into a slot audiod is still reading or ramping from.
- **audiod → everyone:** a meter/status block (peaks, gain reduction, xrun count, heartbeat counter).

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
Makefile           # make / test / asan / memcheck / soakcheck / deploy
src/common/        # shm ring, seqlock, logging, sd_notify wrapper
src/audio/         # audiod
src/ctl/           # ctld
analysis/          # analyzerd: delay est., transfer fn, EQ solver (graded)
ui/                # Python LAN UI (non-graded)
tools/             # Python capture/plot helpers (non-graded)
dts/               # device-tree overlay for the I2S card
systemd/           # unit files
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
- `make test`, `make asan` and `make memcheck` pass. `make soakcheck` reports 0 xruns
  and a watchdog heartbeat for every daemon.
