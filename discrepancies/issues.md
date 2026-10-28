# Docs review: ProposedStructure.md, rework.md, images/Schematic.svg (2026-10-28)

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

1. **How the RT loop is paced (affects mechanism B).** The docs don't say how audiod waits
   for the next period: blocking in `poll`/`snd_pcm_wait` (interrupt-driven) or spinning
   on the hw pointer (polling).
   - rework.md:9 and dsp:290 claim B as "xHCI IRQ → wakes audiod", but audiod never sees
     that IRQ directly. It only gets snd-usb-audio's period-elapsed wakeup.
   - **DECIDE** the wakeup model, which settles what B actually compares.
2. **A 64-frame period doesn't line up with USB microframes.** At 96 kHz a high-speed
   microframe carries 12 frames, and 64/12 = 5.33. Period-elapsed therefore lands
   mid-packet, and wakeups jitter by up to a URB. A period of 48 or 96 frames would line
   up. **VERIFY** against snd-usb-audio's minimum period and `nrpacks`. dsp:59 and dsp:295
   already mark 64 frames as a VERIFY target.
3. **The latency budget in ProposedStructure.md leaves out USB** (§4, :128-130).
   - "ADC + period×periods + DAC" ignores the isochronous URB queue and the interface's
     internal buffering, which are likely several ms. The ~1.3 ms figure is the I2S-era
     number.
   - dsp:45 already says "output latency = audiod loop + USB isochronous buffering
     (VERIFY via loopback)", so the two docs now disagree.
   - Measure the real value with the loopback test in Verification.
4. **Capture/playback linkage is unspecified.** snd-usb-audio exposes capture and playback
   as separate PCMs, and the UMC202HD uses implicit feedback (playback timing is slaved
   to capture). The docs don't say:
   - how the two streams are started and kept aligned (`snd_pcm_link`, start order);
   - how both are recovered when only one of them has an xrun.
5. **Unit-level scheduling settings reach the non-RT threads.** §2 sets
   `CPUSchedulingPolicy=fifo` and `CPUAffinity=` in the unit (:63-64, dsp:144), and
   threads inherit both.
   - audiod's "non-RT" watchdog/logging thread (:66) would therefore run as FIFO 80 on
     the isolated core 3, which breaks "core 3 belongs to the audio thread only" (:52).
   - dsp:65 puts that thread on "cores 0-1", but nothing says what code moves it there or
     drops its priority.
6. **An IRQ is pinned to the "audio-only" core.** :52 reserves core 3 for the audio thread,
   but :53 and dsp:189 pin the xHCI IRQ / "isochronous IN/OUT endpoints" there. Two
   further problems:
   - One device's endpoints can't be pinned on their own. The IRQ belongs to the whole
     RP1 xHCI controller and is shared with every USB device on it.
   - RP1 IRQs go through PCIe MSI, whose affinity may not be settable.

   **VERIFY** on hardware.
7. **apt vs. the firewall.** §2:38-41 installs the RT kernel "from apt", but :73-74 and
   dsp:204 drop all outbound traffic, including DNS, and say "the Pi never starts a
   connection". How do packages get installed: a bootstrap before the firewall goes up,
   or a `.deb` pushed by `make deploy`? **DECIDE.** This also touches the network
   boundary that CLAUDE.md says is graded.
8. **The triple-buffer release protocol is unspecified.** :96-99 says analyzerd "fills the
   free slot", but not how it knows which slot is free.
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
