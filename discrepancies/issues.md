verify # Docs review: ProposedStructure.md, rework.md, images/Schematic.svg (2026-10-28)

The design moved from a custom I2S ADC/DAC board (PCM1808/PCM5102A/THAT/osc) to a Behringer
UMC202HD over USB. This review lists sections of the three docs (and Components.md where
they point to it) that still describe the old hardware, claims that contradict each other,
and behaviour nothing defines. Items marked **DECIDE** need a design decision before the
docs can be fixed. Items marked **VERIFY** need a check on the hardware.

**Recheck (2026-10-28, after images/dsp-architecture.svg/.png were restored):**
- **No longer an issue:** the deleted-file part of B7. The restored images are identical to
  the committed versions.
- **Unchanged:** ProposedStructure.md, rework.md, Components.md and images/Schematic.svg.
  Every other issue below is still present.
- **New evidence in dsp-architecture.svg:** the SVG adds support to A1, A4, B9, B11, B13,
  C3, C5 and C6 (cited inline as `dsp:`) and has some problems of its own (section E).

---

## A. Stale content from the removed I2S board (definite inconsistencies)

1. **The data-flow rate table still describes the I2S hardware**
   (ProposedStructure.md:111-115). It has rows for the 24.576 MHz osc → PCM1808/PCM5102A,
   BCK/LRCK, RP1 I2S DIN/DOUT, RP1 DMA, and "DMA-completion IRQ ~1500/s per stream".
   - None of these parts exist any more (§1, §7.2, Components.md, Schematic "What this
     replaces" box).
   - With USB, xHCI interrupts happen per URB completion, not per ALSA period, so the
     1500/s figure is also wrong.
   - dsp-architecture.svg's rate table (dsp:270-292) has already been rewritten for USB,
     so the two tables now disagree.
   - The same I2S-era figure survives on the diagram itself: dsp:172-173 labels the wake
     arrow "~3000/s, 1500 / stream". The SVG's own table row (dsp:291) says "rate set by
     period size; VERIFY".
2. **The XSMT GPIO is still listed** (ProposedStructure.md:125, "audiod non-RT thread →
   journald, XSMT GPIO"). This contradicts §5 (:147, "no GPIO mute pin exists"),
   Schematic.svg:67/97, and dsp:68/343.
3. **The `dts/` directory and device-tree install are still listed.**
   ProposedStructure.md:172 says the Makefile "installs … dts/", and :179 describes a
   "device-tree overlay for the I2S card". This contradicts §1 (:29, "no device-tree
   overlay, no I2S"), :54-55, dsp:190 and dsp:226. No `dts/` directory exists in the repo.
4. **The rate cascade did change** (rework.md:20). It claims "no rate-cascade numbers needed
   to change", but the cascade in ProposedStructure §3 is the I2S clock tree that no longer
   applies (see A1). dsp-architecture.svg confirms this: its table was rewritten for USB,
   and dsp:288/292 notes "hardware changed".
5. **The Context section is out of date** (ProposedStructure.md:4). It says there is "no
   code or Makefile yet", but a Makefile, src/, os/ and systemd/ now exist.
6. **The Schematic "Levels" box is stale.**
   - Schematic.svg:107-108 says "VERIFY exact headroom once the specific interface model
     is chosen", but the model is already chosen.
   - :106 still refers to the "custom-board plan".
   - :126 says "USB Audio Interface" instead of naming the model.

## B. Cross-document contradictions

7. **Mechanism tags: now only partly true.** rework.md:7 says the diagrams
   (Schematic.svg, dsp-architecture.svg) tag mechanisms B-F.
   - dsp-architecture.svg is back and does tag B, C, D, E and F (dsp:56, 72, 75, 138, 188,
     225).
   - Schematic.svg still has no mechanism tags, so the claim holds for only one of the two
     diagrams.
8. **The image paths are wrong.**
   - ProposedStructure.md:11 and rework.md:7,17 reference `Schematic.svg`. The file now
     lives at `images/Schematic.svg`, and the old root copy is deleted but not yet
     committed.
   - rework.md:7,20 and Components.md:18 reference `dsp-architecture.svg` without the
     `images/` prefix.
9. **The ring is called "no-drop", but it drops frames.** rework.md:12 and dsp:75 call
   mechanism F a "no-drop SPSC ring". ProposedStructure.md:90-91 says the *reader drops
   frames and counts them*, and dsp-architecture.svg says the same thing itself (dsp:124
   "drops + counts if late", dsp:303). The no-drop guarantee applies to the audio path,
   not the ring. As written, F is claimed for a property the ring doesn't have.
10. **The USB quirk was never added to ProposedStructure.md.** rework.md:22 says the "clock
    source N is not valid" quirk is "carried into ProposedStructure.md". Both SVGs carry
    it (Schematic.svg:99-100, dsp:227-228), but ProposedStructure.md never mentions it.
    The fix landed around kernel 5.19, and §2 requires a 6.12+ RT kernel, so this VERIFY
    can mostly be closed.
11. **memfd vs. persistent `/dev/shm`.** ProposedStructure.md:86 and dsp:194 allow `memfd`
    *or* `shm_open`. §5 (:165-167) and dsp:73 ("survives restarts") rely on segments
    outliving a process so a restarted audiod can reattach. A memfd is freed when its
    last fd closes, so only `shm_open` works. **DECIDE:** use shm_open only.
12. **The option to fold ctld "into the UI backend"** (ProposedStructure.md:83) conflicts
    with other rules.
    - The UI is Python, and :84 says the UI "never maps shm".
    - ctld is the only producer of the param block, which lives in shm.
    - CLAUDE.md allows no graded mechanism in Python and makes ctld C17 and Aaron's.

    The fold-in option breaks all three. dsp-architecture.svg (dsp:114-118) already shows
    ctld only as a separate C17 daemon.
13. **The two statements about syscalls in the RT loop disagree.** :66-67 says the RT loop
    "makes no syscalls", while :81 and dsp:63 say "no blocking syscalls". The ALSA mmap
    loop has to make syscalls (`snd_pcm_mmap_begin/commit`, status ioctls, and some kind
    of wait). :66-67 is the outlier (see C1).
14. **What the measured delay includes is stated two ways.** :22-23 says "any delay between
    them is acoustic", while :135 says it is "one total delay (audiod + converters + air)".
    The second is correct: the mic hears the *processed output*, so the measured lag
    includes the USB OUT path, the DAC and audiod buffering.
15. **"Measurement mic" vs. dynamic mic.** rework.md:30 and Schematic.svg:39 say
    "measurement mic", while §7.1, Schematic:40 and dsp:219 say dynamic with no phantom
    power. This isn't strictly a contradiction, but the UMC202HD's +48V is *shared* across
    both inputs (rework.md:18). Turning it on would also put phantom power on the desk line
    in input 1, so the docs should say "48V OFF". None of the three docs does.

## C. Undefined behaviour / underspecified mechanisms

1. **How the RT loop is paced (affects mechanism B). DECIDED (2026-10-28): polling.**
   audiod busy-polls the ALSA mmap hw pointer each period instead of blocking in
   `poll`/`snd_pcm_wait`. Core 3 is isolated for audiod alone, so blocking buys nothing (no
   other work could use the freed CPU), while polling skips the scheduler wakeup
   round-trip and needs no syscall per check. The xHCI completion IRQ (cores 0-1, not
   core 3 -- see C6) still runs on every USB completion; it just updates the hw pointer,
   rather than waking a sleeper, so it doesn't need to share a core with the thread it
   updates. Fixed in ProposedStructure.md §2/§3, rework.md:9, and dsp-architecture.svg
   (wake-arrow label + rate-table row). The .png re-export is still outstanding.
2. **A 64-frame period doesn't line up with USB microframes. DECIDED (2026-10-28): target
   48 frames, fall back to 96.** At 96 kHz a high-speed microframe carries 12 frames, and
   64/12 = 5.33, so period-elapsed lands mid-packet and wakeups jitter by up to a URB. 48
   frames (4 microframes, 0.5 ms, 2000 periods/s) lines up exactly and keeps audiod's own
   buffering contribution to latency lowest, which matters given PROBLEM.md names latency
   as the project's central risk. 96 frames (8 microframes, 1 ms, 1000 periods/s) is the
   documented fallback if `snd-usb-audio`'s minimum period / `nrpacks` rejects 48 --
   **VERIFY** on hardware either way. Fixed in ProposedStructure.md (§1, §3 IPC text,
   §3 data-flow table, §4) and rework.md:20; dsp-architecture.svg's subtitle, per-period
   label, wake-arrow rate, IPC-arrow labels and rate-table rows updated to 48/2000 with 96
   noted as fallback.
3. **The latency budget in ProposedStructure.md leaves out USB (§4). DECIDED (2026-10-28):
   fixed with a cited planning range, not a guess.**
   - "ADC + period×periods + DAC" ignored the isochronous URB queue and the interface's
     internal buffering. dsp:45 already said "output latency = audiod loop + USB
     isochronous buffering (VERIFY via loopback)", so the two docs disagreed.
   - A web search for real UMC202HD measurements (no official Behringer round-trip spec
     exists) found: a Linux/JACK test (44.1 kHz, 64-frame × 2 periods) reporting
     output-only latency of 6.3-7.7 ms ([forum.modartt.com](https://forum.modartt.com/viewtopic.php?id=8026)),
     and a Windows test reporting ~8 ms total processing latency, where a *smaller* buffer
     made it worse (8 ms → 12 ms) ([JamKazam](https://jamkazam.freshdesk.com/support/solutions/articles/66000122529)).
     `nrpacks`, the obvious kernel-side knob, was removed in Linux 3.13 and no longer
     applies, so there's no current tunable to derive a number from either.
   - ProposedStructure.md §1 and §4 now state **≈6-8 ms total output-path latency** as an
     explicitly unverified planning range sourced from other OS/driver/buffer configs, not
     ours, with the DAC→ADC loopback test in Verification as the only way to get a real
     number for this hardware/kernel. No separate ADC/USB/DAC sub-split is claimed, since
     the measurements don't decompose that way.
4. **Capture/playback linkage is unspecified. DECIDED (2026-10-28).** snd-usb-audio exposes
   capture and playback as separate PCMs, and the UMC202HD uses implicit feedback
   (playback timing is slaved to capture).
   - **Start order is forced, not chosen:** by USB Audio Class 2.0's definition of
     implicit feedback, playback's OUT rate is derived from capture's IN packet pattern,
     so capture must be prepared and started first, and must run long enough to establish
     a stable pattern before playback starts. `snd_pcm_link()`'s simultaneous-trigger
     semantics don't fit this sequencing and aren't used.
   - **xrun recovery is coupled:** any xrun in either direction -- not just a capture
     xrun, which obviously breaks playback's clock reference -- stops and relocks *both*
     streams through the same start sequence. A playback-only xrun is deliberately not
     recovered independently, trading a few extra ms of silence on a playback blip for
     one restart code path instead of relying on unverified independent-recovery
     behavior from snd-usb-audio. The settle time between starting capture and starting
     playback is still a hardware VERIFY.
   - Fixed in ProposedStructure.md §1 (new "Capture/playback linkage" bullet) and §5
     (xrun bullet rewritten for the coupled restart).
5. **Unit-level scheduling settings reach the non-RT threads. DECIDED (2026-10-28): don't
   set them at the unit level for audiod at all.** `audiod.service` previously set
   `CPUSchedulingPolicy=fifo`/`CPUAffinity=` process-wide; since systemd has no syntax to
   target one thread of a multi-threaded process, every subsequently `pthread_create`'d
   thread would inherit that as its default, so audiod's non-RT watchdog/logging thread
   would silently run as FIFO 80 on the isolated core 3 unless it remembered to demote
   itself -- the dangerous state as the default. Instead, audiod's unit now sets only
   `LimitMEMLOCK=infinity`; the RT thread calls `pthread_setschedparam(SCHED_FIFO, 80)` +
   `pthread_setaffinity_np()` (core 3) on itself right after it starts -- unconditionally,
   every time audiod starts, before it ever touches the ALSA mmap loop -- and the non-RT
   thread is left alone at `SCHED_OTHER`/default affinity. `isolcpus=3` already keeps an
   unaffined thread off core 3, so nothing needs to actively demote itself, and any future
   thread is safe by default. Fixed in ProposedStructure.md §2/§3 and
   dsp-architecture.svg's audiod box (process label, RT-thread label, non-RT-thread
   label). The .png re-export is still outstanding.
6. **An IRQ is pinned to the "audio-only" core. DECIDED (2026-10-28): pin none there.**
   :52 reserved core 3 for the audio thread, but :53 and dsp:189 pinned the xHCI
   completion IRQ there on the theory it needed to be near the RT thread it updates. It
   doesn't: audiod polls the mmap hw pointer in memory (mechanism B) rather than blocking
   on the IRQ, and a hardirq's memory write is visible across cores via cache coherence
   well within a period. Co-locating it would instead have been a jitter source in its
   own right -- a hardirq preempts even `SCHED_FIFO`, and this one fires every 0.5 ms (one
   per USB completion), directly against "core 3 belongs to the audio thread only." It
   also would have required pinning one device's endpoints independently of the rest of
   the shared RP1 xHCI controller's IRQ, which isn't possible. The xHCI completion IRQ now
   gets no special case: it moves to cores 0-1 with every other IRQ. Whether RP1's
   PCIe-MSI IRQs accept an affinity change at all stays a **VERIFY**, but it's the same
   VERIFY that already covered "every other IRQ," not a new one. Fixed in
   ProposedStructure.md §2, the §3 data-flow table, and the Verification section, and in
   dsp-architecture.svg's xHCI kernel-layer cell, rate-table row, and kernel-space banner.
   `os/VERIFY.md`'s checklist (the CPU3-only assertion and the endpoint-specific-pin item)
   was also stale from before this fix and is now updated. The .png re-export is still
   outstanding.
7. **apt vs. the firewall. DECIDED (2026-10-28): apt runs once at bring-up, before the
   firewall is ever loaded; `make deploy` never installs packages.** The graded
   "no network calls" rule (CLAUDE.md) is about the running product, not the one-time
   provisioning step that puts an OS and RT kernel on the SD card before the product
   exists, and sequencing is forced by the Makefile itself: `make deploy` is copy-only
   (`Makefile:230-260`) and its printed install steps never touch apt, so the RT kernel
   has to already be on the Pi before `nftables` enters the picture. Order: flash →
   boot on an open network → `apt update && apt install` the RT kernel + `apt-mark hold`
   (one-time) → `make deploy` → enable `os/nftables.conf`. A later kernel-package swap
   repeats that bootstrap with the firewall not yet (re-)enabled. Fixed in
   ProposedStructure.md §2 (networking bullet) and `os/VERIFY.md` (new checklist item).
8. **The triple-buffer release protocol is unspecified. DEFERRED (2026-10-28) -- revisit
   later.** :96-99 says analyzerd "fills the free slot", but not how it knows which slot is
   free.
   - Knowing requires audiod to *publish* which slot it holds. That is a write, and the
     text only describes audiod doing loads (also dsp:93, 314).
   - During a ramp audiod holds two slots. A second publish within one period (e.g.
     right after an audiod restart) would find no free slot.

   The exchange/ack mechanism needs to be specified.
9. **Ring overwrite detection is unspecified.** For the reader to "drop frames" (:90), it
   has to detect that the writer lapped it *while* it was copying (a torn read). src/common
   includes a seqlock (:173), but nothing says whether or how the ring uses it.
10. **Status block consistency is unspecified.** audiod writes the block 1500 times/s and
    ctld reads it at 20 Hz from another process (:105-106, dsp:99, 132). No tearing
    protection is stated.
11. **Startup defaults are undefined.** Nothing says how audiod behaves before any
    coefficients exist (fresh shm) or before ctld has published params (limiter
    threshold, bypass state). audiod boots first, so this window always occurs.
12. **Corrupt shm leads to a crash loop.** "Corrupt shm" is classed as unrecoverable, so
    audiod exits and restarts (:159-161). The restarted audiod reattaches to the same
    segment (:165) and fails again, until `StartLimitBurst` leaves it dead. Nothing says
    who recreates a bad segment.
13. **A missing or lost device leaves audio down for good.** Nothing orders audiod after the
    ALSA card exists. If the UMC202HD is late or unplugged, audiod reports "device lost"
    and restarts repeatedly until `StartLimitBurst` stops it. Audio then stays down with
    no recovery path. dsp:275 mentions hotplug, but no doc says what happens then.
14. **Muting on crash and on stream stop is undefined.** §5 covers a clean exit and calls
    out a hang. Still undefined:
    - A SIGSEGV or a watchdog SIGKILL also skips the silence write.
    - What the UMC202HD outputs when the USB OUT stream stops: silence, or a pop.
    - What the ALSA `sw_params` (`stop_threshold`) do on underrun. With the defaults the
      stream stops on underrun, so the "hang" gap is actually short. Set these on purpose.
15. **Sample-rate negotiation has no failure rule.** The rate is "negotiated … target
    96 kHz" (:27, dsp:45). If hw_params returns a different rate, every constant shifts:
    period length in ms, FFT window, ring length in seconds, and biquad design. Should a
    mismatch be a config error (`RestartPreventExitStatus`)? Undefined. See also E3.
16. **The sample-format conversion point isn't specified.** UMC202HD capture is likely
    S24_3LE or S32_LE, while the ring holds float32 and the biquads use float64.
17. **The preamp gain range is unconfirmed.** §7.1 and dsp:219 need 50-60 dB of gain.
    **VERIFY** against the UMC202HD spec, since the top of that range may exceed what the
    MIDAS preamp can give.

## D. Minor

- **Text overlap in the schematic.** Schematic.svg:81 places the "TRS->XLR adapter cables"
  text at y=460, which is the bottom edge of both output boxes (400+60). The text overlaps
  the box borders.
- **The watchdog doesn't cover exit codes.** ProposedStructure.md:63 says it "covers …
  clean exit codes", but exit codes come from the error-handling policy in §5.
- **The audio path works without the mic.** rework.md:30 says "nothing here works if either
  sensor is missing data". Without the mic, only the analyzer freezes (§4 gate).
- **The dsp-architecture subtitle probably runs off the canvas.** dsp:45 is about 348
  characters at 12.5 px starting at x=30, roughly 2,100 px wide on a 1,700 px canvas.
  **VERIFY** in a browser.

## E. New: issues within images/dsp-architecture.svg

1. **Mechanism D is still claimed.** dsp:72 ("shared memory (D)") and dsp:225 ("mechanisms
   we use: B, C, D, E, F") claim D for `/dev/shm`. rework.md:13 says that shm is tmpfs,
   has no power-cut story, and that D should be treated as unclaimed. The diagram and the
   rework notes disagree about the mechanism count.
2. **The legend says agreed values are missing from ProposedStructure.md, but they are
   there.**
   - dsp:249 says teal = "value agreed in the rate review (not yet in
     ProposedStructure.md)". ProposedStructure.md already contains every teal value:
     - 19 × ⅓-oct float64 biquads (:81, :92);
     - FFT 32768 / hop 8192 (:82);
     - 2¹⁷-frame ring (:88);
     - 20 Hz meters (:83-84);
     - WatchdogSec=10 / 5 s (:61-62);
     - the ctld param triple buffer (:100-104).
   - dsp:312 says the param buffer "fixes the two-writer gap in §3", but §3 already
     includes that fix.
3. **The capture rate is given as "48/96 kHz"** (dsp:279). 48 kHz appears nowhere else.
   Every other doc and the rest of the SVG (dsp:45, 59, 252) target 96 kHz.
4. **There is no §7.4.** dsp:284 cites "§7.4" for the mono-on-L+R decision, but
   ProposedStructure.md §7 has only items 1-3. The decision is §7.3.

---

## Suggested fixes (doc-only, not yet applied)

- **ProposedStructure.md:**
  - replace the §3 rate-table rows 111-115 and row 125 with the USB rows already in
    dsp:270-292;
  - remove `dts/` from §6;
  - fix the §1 image path and update the Context section;
  - reword :22-23 and :66;
  - bring §4's latency formula in line with dsp:45;
  - add the USB quirk note;
  - settle memfd → shm_open and drop the "folded into UI" option (after DECIDE).
- **rework.md:** fix both image paths, reword the "no-drop" claim for F, correct the :20
  rate-cascade claim, and either tag Schematic.svg or narrow :7 to dsp-architecture.svg.
- **Components.md:18:** add the `images/` prefix.
- **images/Schematic.svg:** update the Levels box and the :126 label, add "48V OFF", and
  fix the :81 text overlap.
- **images/dsp-architecture.svg:**
  - fix the :172-173 wake rate, the :194 memfd label, the :249 legend, :279 "48/96",
    :284 "§7.4" and the "no-drop" wording at :75;
  - resolve the D tag at :72/:225 together with rework.md:13;
  - re-export the .png after any edit.
- **C-items:** those marked DECIDE/VERIFY stay open questions until they are resolved.
