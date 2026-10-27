# Rework notes: Behringer UMC202HD vs. assignment requirements

## 1. Does the project have at least two of mechanisms A-F, given the UMC202HD?

**Yes, comfortably.**

**Mechanism count:** the diagrams (Schematic.svg, dsp-architecture.svg) tag B, C, D, E, F (5 of 6, only A dropped). None of those five depend on which specific USB interface is plugged in — they're all Pi-side software:

- **B** (interrupt vs. polling) — xHCI completion IRQ → audiod. Solid.
- **C** (SCHED_FIFO RT path) — audiod on core 3. Solid.
- **E** (multi-process + supervisor) — audiod/analyzerd/ctld/systemd. Solid.
- **F** (no-drop SPSC ring, sequence-counted). Solid.
- **D** (storage layer with crash-consistency) — weaker than it looks: the "ring buffer" claimed for D is the *same* `/dev/shm` SPSC ring claimed for F, and D's rubric specifically wants "recovery after a mid-write power cut." `/dev/shm` is tmpfs — it's wiped on power loss, so there's no actual crash-consistency story here, just a restart-survival story (which is really just part of E). Treat D as unclaimed until something genuinely persists across a power cycle (even a tiny append-only log of session params/presets would do it).

So even throwing out D as currently written, the project is still at B+C+E+F = 4, well past the "at least two" bar.

**The UMC202HD matches Components.md/Schematic.svg cleanly.** Those docs assume two combo XLR/TRS inputs and balanced outputs to the PA, and the UMC202HD delivers both:
- **Two** combo XLR/TRS inputs, each with a MIDAS preamp, gain, line/instrument selector, pad, and shared +48V phantom — matches "input 1 = line (desk monitor), input 2 = mic preamp" exactly.
- **Balanced 1/4" TRS outputs** (not XLR, but TRS carries the same balanced hot/cold/ground signal XLR does — a TRS→XLR adapter cable is a connector swap, not a noise-rejection compromise).
- **Sample rate up to 192 kHz**, comfortably covering the 96 kHz target already in dsp-architecture.svg — no rate-cascade numbers needed to change.

The one real risk carried into Components.md/the SVGs/ProposedStructure.md: a known Linux `snd-usb-audio` quirk on the UMC-series (a one-time "clock source N is not valid" dmesg line at probe, tied to implicit-feedback-mode support). It's fixed upstream for kernels around 5.19+ and reported harmless once that fix is present — flagged as a VERIFY-the-kernel-version item, not a blocker.

Sources: [B&H UMC202HD](https://www.bhphotovideo.com/c/product/1113600-REG/behringer_umc202hd_audiophile_2x2_24_bit_192_khz.html), [Crutchfield UMC202HD](https://www.crutchfield.com/S-NgTKKWGI3wW/p_937UMC202/Behringer-U-Phoria-UMC202HD.html), [ALSA UMC 204/404 HD workaround commit](https://android-kvm.googlesource.com/linux/+/ae8b1631561a3634cc09d0c62bbdd938eade05ec), [kernel bugzilla 215934](https://bugzilla.kernel.org/show_bug.cgi?id=215934)

## 2. Does the project meet the "two sensors that cooperate" requirement?

**Yes.**

The two sensors are physically distinct (mixing-desk line-out vs. a measurement microphone — different transducers, different physical cables, different signal chains), and they don't just coexist: analyzerd's single output (the EQ coefficient set) is a function of *both* signals together — GCC-PHAT cross-correlation between them to estimate acoustic delay, then `H(f) = System/Source` to derive the correction. That's the "feeding a single shared pipeline whose behavior depends on both" branch of the rubric, explicitly. Two independent demos stapled together would be e.g. "mic meter on one screen, desk level on another" with no shared computation — that's not what this is; nothing here works if either sensor is missing data, because the whole output is a ratio/comparison of the two.

**One honest caveat:** the assignment's "systems content hides" sentence is about sensors wanting different sample rates/interfaces/latency budgets that have to be reconciled. In this design, Source and System are literally two channels of *one* stereo ADC inside the UMC202HD, so they share sample rate, interface, and clock domain by construction — there's no clock-domain reconciliation problem to solve, which is the specific flavor of difficulty that sentence is pointing at.

That's not fatal — the requirement is "cooperate," not "cooperate across mismatched clocks" — and there is still real reconciliation work: the mic needs ~50-60 dB more gain than the desk line-out (handled by the UMC202HD's MIDAS preamp on input 2), the two signals differ by 10-100 ms of acoustic propagation that analyzerd has to estimate and compensate, and they have different noise/coherence characteristics that drive the trust-gating logic. So the systems content is there, it just shows up as *temporal and level* reconciliation rather than *clock* reconciliation. If a grader applies the paragraph literally this is fine; if preempting the "didn't you just avoid the hard part by picking one 2-channel box" question is wanted, that's the one-sentence answer to have ready.
