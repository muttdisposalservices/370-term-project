# Resolved issues (moved from issues.md, 2026-10-28)

History only. Nothing here needs action.
- **Sections A, B and E** were fixed in 0cc5520 ("fix issues A B E and part of C") and
  c882d91 ("fix c5-c7 issues"). This was checked by re-grepping each stale phrase across
  ProposedStructure.md, rework.md, Components.md and both SVGs; only the corrected
  wording remains.
- **C1-C7** were decided. Their original text is kept below. The decisions are restated
  as plan entries in [plan.md](plan.md), and the hardware checks they leave behind are
  tracked in [issues.md](issues.md).
- **C8** was decided and implemented on 2026-10-29. Aaron's review of it is tracked in
  issues.md.
- **C9** was decided and implemented on 2026-10-29. Only Hazel's daemons use the ring, so
  nothing is left in issues.md.
- **C10** was decided and implemented on 2026-10-29. Aaron's review of it is tracked in
  issues.md.
- **C11** was decided on 2026-10-29. The decision is restated as a plan entry in
  [plan.md](plan.md), and Aaron's ctld-side review is tracked in issues.md.
- **C12** was decided and implemented on 2026-10-29. It also decided F8, which is restated
  in plan.md. Aaron's ctld-side review is tracked in issues.md.
- **Hazel's stale-text items** (issues.md §7) were fixed on 2026-10-29. Aaron's stale-text
  items are still tracked in issues.md.
- **The audiod and systemd items** (C13-C16, F1, F2, F9-F11 and D2) were decided on
  2026-10-29. C15, F2 and D2 are fully closed. The rest are restated in plan.md, and
  Aaron's remaining checks (plus analyzerd's F10 codes) are tracked in issues.md.
- **F5** was decided and implemented on 2026-10-29. It's restated in plan.md. The `-lm`
  note it leaves for Aaron is tracked under issues.md F4.

Line references (`:NN`, `dsp:NN`) point at the files as they were when each issue was
written, so they no longer match the current files.

**Recheck (2026-10-28, after images/dsp-architecture.svg/.png were restored):**
- **No longer an issue:** the deleted-file part of B7. The restored images are identical to
  the committed versions.
- **Unchanged:** ProposedStructure.md, rework.md, Components.md and images/Schematic.svg.
  Every other issue below is still present.
- **New evidence in dsp-architecture.svg:** the SVG adds support to A1, A4, B9, B11, B13,
  C3, C5 and C6 (cited inline as `dsp:`) and has some problems of its own (section E).

**Recheck 2 (2026-10-28, after 0cc5520 and c882d91):** all of sections A, B and E are
resolved.

---

## A. Stale content from the removed I2S board (definite inconsistencies). RESOLVED (0cc5520)

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

## B. Cross-document contradictions. RESOLVED (0cc5520)

(B12 is fixed in ProposedStructure.md and Terminology.md:6 and :31, but CtldOutline.md §2
still lists the fold-in as open. That leftover is tracked in issues.md under "Stale
text".)

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

## C1-C7. Decided (2026-10-28). See plan.md

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

## C8. Decided and implemented (2026-10-29)

**Original text (issues.md §1):** The triple-buffer release protocol is unspecified.
audiod consumes the block and analyzerd produces it. Aaron reviews, since ctld's param
block uses the same protocol. :96-99 says analyzerd "fills the free slot", but not how it
knows which slot is free.
- Knowing requires audiod to *publish* which slot it holds. That is a write, and the text
  only describes audiod doing loads (also dsp:93, 314).
- During a ramp audiod holds two slots. A second publish within one period (e.g. right
  after an audiod restart) would find no free slot.

**Decision:** the standard 3-slot exchange, in `src/common/tbuf.{h,c}` (generic over the
payload size, so the coeff block and the ctld param block share it). Tested in
`tests/test_tbuf.c`.
- **The shared word.** One atomic word holds `{gen:24 | fresh:1 | idx:2}`. Each side
  privately owns one slot and the word names the third (the middle), so the producer always
  has a free slot.
- **Publishing.** The producer fills its slot and CAS-swaps it into the middle with `fresh`
  set. An unread value is replaced (latest wins).
- **audiod's poll.** audiod does one relaxed load per period. It does no store unless
  `fresh` is set, and then it swaps.
- **Ramping.** On a swap, audiod copies the new slot into private state and ramps between
  private copies, so it never holds two slots.
- **Restarts.**
  - audiod mirrors its slot index in `front_shadow`, so a restarted producer recomputes
    its own slot.
  - Reading the shadow mid-swap is detectable (it equals the middle index), and attach
    returns `-EAGAIN`.
  - An audiod re-init bumps `gen`, so a producer from before the restart gets `-ESTALE`
    and must reattach.

ProposedStructure.md §3 (IPC, analyzerd → audiod) updated to match.

## C9. Decided and implemented (2026-10-29)

**Original text (issues.md §1):** Ring overwrite detection is unspecified. (audiod writes
the ring, analyzerd reads it.) For the reader to "drop frames" (:90), it has to detect
that the writer lapped it *while* it was copying (a torn read). src/common includes a
seqlock (:173), but nothing says whether or how the ring uses it. **PROPOSED:** a
monotonic 64-bit write counter. The reader copies a chunk, re-reads the counter, and
discards the chunk if the writer lapped it. No seqlock needed. The generation counter (§5)
goes in the same header.

**Why the proposal wasn't enough.** A single counter is bumped only *after* the writer
writes a chunk. A writer that is part-way through overwriting the reader's frames hasn't
bumped it yet, so the re-read says "not lapped" and a torn chunk gets accepted.

**Decision:** two monotonic 64-bit frame counters, in `src/common/ring.{h,c}` (generic
over the frame size). Tested in `tests/test_ring.c`, including the mid-push case above.
- **claim / commit.** The writer stores `claim` before writing a chunk and `commit` after.
  The reader copies frames below `commit`, then re-reads `claim`. Frame `pos` has been
  overwritten exactly when `claim > pos + cap`, so the check is exact.
- **Writer cost.** Two stores and one release fence per period; no syscalls, no waiting.
- **Reader results.** `-EAGAIN` (not enough data yet), `-EPIPE` (lapped: lost frames are
  added to `dropped`, and the reader jumps to the newest frame) or `-ESTALE` (audiod
  restarted). On either of the last two, analyzerd resets its window.
- **No seqlock.** The claim/commit pair does that job for the ring; the seqlock stays for
  the status block (C10).
- **Generation counter.** It lives in the ring header. On restart, audiod's `ring_init`
  resumes a valid segment under a new gen, keeping the counters monotonic. It replaces a
  crashed writer's half-written chunk with silence. A segment with a bad header is
  re-initialised from scratch; recreating it is C12.
- **Known caveat.** The reader's copy can race the writer's, as in any seqlock-style
  read. The claim check discards those copies. TSan would flag it; `make` doesn't run TSan.

ProposedStructure.md §3 (IPC, audiod → analyzerd) and §5 (restart policy) updated to match.

## C10. Decided and implemented (2026-10-29)

**Original text (issues.md §1):** Status block consistency is unspecified. audiod is the
writer; Aaron reviews as the ctld reader. audiod writes the block 2000 times/s and ctld
reads it at 20 Hz from another process (:105-106, dsp:99, 132). No tearing protection is
stated. **PROPOSED:** the src/common seqlock (one writer: audiod; readers: ctld,
soakcheck).

**Decision:** a single-writer seqlock, in `src/common/seqlock.{h,c}` (generic over the
payload size; the status struct layout is separate shm-contract work). Tested in
`tests/test_seqlock.c`, including a cross-process stress test that fails if the seq
re-check is removed.
- **Why not tbuf or ring.** tbuf allows one consumer and has audiod as its consumer. The
  ring is a stream. Status is a snapshot with one writer and two readers, where only the
  latest value matters.
- **Protocol.** `seq` is even when the data is stable and odd mid-write.
  - The writer stores `seq+1`, issues a release fence, copies the data in, then stores
    `seq+2` (release).
  - The reader loads `seq`, copies the data, issues an acquire fence and re-loads `seq`.
    It keeps the copy only if the two loads are equal and even.
- **Writer cost.** Two stores, one release fence and one small memcpy per period. The
  writer keeps `seq` privately, so there is no RMW, no syscall and no waiting.
- **Readers.**
  - ctld and audiod's own watchdog thread are the readers.
  - `seqlock_read` tries 64 times, then returns `-EAGAIN`. A write takes tens of ns on the
    isolated core, so 64 tries span many writes but stay far below one period.
  - A writer that dies mid-write leaves `seq` odd, so reads return `-EAGAIN` until audiod
    restarts. The watchdog thread treats that as "heartbeat not advanced".
- **soakcheck.** It reads status through ctld's socket, not shm. Python never maps shm,
  the same rule the ui follows, and it couldn't do the acquire fence anyway.
- **Generation counter.** It lives in the seqlock header.
  - On restart, `seqlock_init` resumes a valid segment as one bracketed write: it closes
    any open write, zeroes the data and stores the new gen. `seq` (64-bit) stays monotonic.
  - Readers get `-ESTALE` once and then read the new gen's data.
  - A segment with a bad header is re-initialised from scratch; recreating it is C12.
- **Known caveat.** As with the ring, the reader's copy can race the writer's. The seq
  check discards those copies. TSan would flag it; `make` doesn't run TSan.

ProposedStructure.md §3 (IPC, audiod → ctld) and Terminology.md (seqlock) updated to match.

## C11. Decided (2026-10-29). See plan.md

**Original text (issues.md §1):** Startup defaults are undefined. Nothing says how audiod
behaves before any coefficients exist (fresh shm) or before ctld has published params
(limiter threshold, bypass state). audiod boots first, so this window always occurs.
**PROPOSED:** compiled-in defaults: flat EQ (unity biquads), limiter on at a conservative
fixed threshold, bypass off.

**Decision:** the proposal, plus a mute gate. The proposal needed one addition:
`tbuf_init` zeroes every slot, and an all-zero biquad outputs silence, not a flat
response. So a fresh slot can never be used as a default.
- **Defaults.** On every start, audiod's private state is compiled in:
  - flat EQ (b0 = 1, the rest 0);
  - limiter on: brickwall, -6 dBFS ceiling, no lookahead;
  - bypass off;
  - output muted.
- **Replacing a default.** A block replaces the state only after `tbuf_consumer_poll`
  returns 1 and the block validates (finite, stable biquads; finite ceiling ≤ 0 dBFS).
  Rejected blocks are counted.
- **Unmute gate.** The output fades in only when all of these hold:
  - both PCMs are locked;
  - `SELFCHECK_MS` of clean periods have passed;
  - ctld has published one valid param block since this audiod started.

  An xrun or a non-finite output sample re-mutes. The status block carries `mute_state`
  and `mute_reason`.
- **Restarts.** Producers republish their full state on `-ESTALE`. ctld's republish
  re-arms the gate, and analyzerd's restores the last accepted coefficients.

ProposedStructure.md §3 (IPC) and §5 updated to match. Work items are in plan.md C11.

## C12. Decided and implemented (2026-10-29)

**Original text (issues.md §1):** Corrupt shm leads to a crash loop. audiod creates and
recreates the segments; Aaron owns ctld's reattach. "Corrupt shm" is classed as
unrecoverable, so audiod exits and restarts (:159-161). The restarted audiod reattaches to
the same segment (:165) and fails again, until `StartLimitBurst` leaves it dead. Nothing
says who recreates a bad segment. **PROPOSED:**
- audiod creates all four segments.
- On a bad magic/version, audiod unlinks and recreates them (it doesn't exit) and bumps
  the generation counter.
- analyzerd and ctld only attach, and they reattach when the generation changes.

This also decides unit ordering (F8).

Related (from C11): ProposedStructure.md §5 says a restarted audiod "resumes on the
last-good coefficients" from shm, but `tbuf_init` zeroes the slots. Today that holds only
because analyzerd republishes on `-ESTALE`.

**What was already covered.** `ring_init` and `seqlock_init` re-init a segment with a bad
header in place, and `tbuf_init` always does (C8-C10). So a bad magic/version alone can't
loop once audiod has a correctly sized mapping. What was missing was the segment itself:
- no code opened, sized or named the segments;
- some states can't be fixed in place (wrong size, not a regular file, a file audiod can't
  open read-write);
- nothing said where `gen` comes from.

**Why the proposal wasn't enough.** Unlink-and-recreate makes a new object. A reader
still mapping the old one never sees a gen change, so it stalls for good.

**Decision:** the proposal, with the repair split by what's broken. Implemented in
`src/common/shm.{h,c}` and tested in `tests/test_shm.c`, including two regression tests
for the crash loop: a corrupt header and a wrong-size segment, each across repeated
restarts.
- **audiod is the only creator.** `shm_seg_create` maps each segment at exactly its
  expected size:
  - right size: kept, and a bad header is re-initialised by the primitive's init;
  - wrong size, not a regular file, or can't be opened RW: unlinked and recreated (mode
    set with `fchmod`, so umask can't narrow it). Old mappings stay valid, so no reader
    can take SIGBUS.

  audiod never exits over segment contents. The one shm exit is an unusable segment that
  audiod can't unlink (`-EACCES`/`-EPERM`, e.g. another user's file in sticky
  `/dev/shm`). That's a config error with its own exit code in `RestartPreventExitStatus`
  (F10), so systemd stops on a clear log line instead of looping.
- **Segment names:** `/dspunit-ring`, `/dspunit-coeff`, `/dspunit-param` and
  `/dspunit-status`. They are never unlinked on exit.
- **Generation.** One gen per audiod start, shared by all four inits. audiod reads the
  previous gen from the old status block with `seqlock_reader_attach`. The new gen is
  `shm_next_gen(have_prev, prev, seed)`: `prev + 1`, or a CLOCK_MONOTONIC seed if the old
  status header was bad or new. It's masked to tbuf's 24 bits. Every reader holds `prev`
  or older, so it always sees a change; the exceptions are a 2^24 wrap and a seed
  collision after a corrupt header.
- **analyzerd and ctld only attach** (`shm_seg_attach`, read-only for the ring and
  status readers):
  - they never create a segment and never exit over shm state;
  - they send `READY=1`, then retry attach every 100 ms while sending `WATCHDOG=1`.
    `-ENOENT`, `-EPROTO` and `-EAGAIN` all mean audiod hasn't (re)made the segment yet;
  - on `-ESTALE` they reattach the primitive on the same mapping and republish (C11);
  - after `SHM_STALL_MS` (proposed 1 s) without progress, they call `shm_seg_replaced`
    at most once a second. If the name now points at a new object, they close and
    attach by name again.
- **Coefficients after an audiod restart** come only from analyzerd's republish.
  `tbuf_init` keeps zeroing slots, so audiod never trusts slot contents left behind by a
  crash. The EQ stays flat until analyzerd republishes.
- **Unit ordering (F8):** analyzerd.service and ctld.service get `After=` and
  `Wants=audiod.service`, never `BindsTo=`/`Requires=`. Restated in plan.md.

ProposedStructure.md §3 (IPC) and §5 updated to match. CtldOutline.md §9 is Aaron's and
still says "corrupt shm → exit"; that is tracked in issues.md.

## audiod and systemd items (C13-C16, F1, F2, F9-F11, D2). Decided (2026-10-29). See plan.md

Each decision is restated as a plan.md entry with the same ID, and ProposedStructure.md
§1, §2 and §5 were updated to match. The original text follows.

### C13 (issues.md §2)
**Original text:** A missing or lost device leaves audio down for good. Aaron owns the
udev rule (os/); Hazel owns the audiod.service dependency. Nothing orders audiod after the
ALSA card exists. If the UMC202HD is late or unplugged, audiod reports "device lost" and
restarts repeatedly until `StartLimitBurst` stops it. Audio then stays down with no
recovery path. dsp:275 mentions hotplug, but no doc says what happens then.
**PROPOSED:** a udev rule (os/) that tags the card `TAG+="systemd"` with a stable
`SYSTEMD_ALIAS`, plus `BindsTo=`/`After=` on that device unit in audiod.service.

**Decision:** the proposal, plus `WantedBy=dev-umc202hd.device`. `BindsTo=` alone only
stops audiod on unplug, so audio would still stay down after a replug. The rule matches
the ALSA card device, not the USB device. Aaron's rule and an unplug check remain in
issues.md.

### C14 (issues.md §2)
**Original text:** Muting on crash and on stream stop is undefined. §5 covers a clean exit
and calls out a hang. Still undefined:
- A SIGSEGV or a watchdog SIGKILL also skips the silence write.
- What the UMC202HD outputs when the USB OUT stream stops: silence, or a pop.
- What the ALSA `sw_params` (`stop_threshold`) do on underrun. With the defaults the
  stream stops on underrun, so the "hang" gap is actually short. Set these on purpose.

**PROPOSED:**
- **Threshold.** Set `stop_threshold` = buffer size explicitly, so a hung audiod underruns
  and the stream stops within about 1 ms (48 frames × 2 periods at 96 kHz). This needs no
  other process.
- **Watchdog thread as a backstop.** If the heartbeat stalls for a few periods, it calls
  `snd_pcm_drop` on playback, logs why, and stops sending `WATCHDOG=1`. Before relying on
  this, check whether alsa-lib allows a second thread to call `snd_pcm_drop` on a `hw:`
  handle the RT thread is using.
- **No silence writer in systemd.** By `ExecStopPost=` time the kernel has closed the PCM
  fd and stopped the stream, and playing zeros there would start a new OUT stream.

Whether the UMC202HD pops when its OUT stream stops still needs a check on the hardware.

**Decision:** the threshold and no systemd silence writer, as proposed. Two changes:
- `start_threshold` = boundary as well. Otherwise the playback silence pre-fill would
  autostart playback before capture settles (C4).
- The watchdog thread exits with code 12 instead of calling `snd_pcm_drop`. The threshold
  already stops the stream, and exiting gets an immediate restart without touching
  alsa-lib from a second thread.

The pop check remains in issues.md. If it pops, the fallback is free-running silence fill
(plan.md C14).

### C15 (issues.md §2)
**Original text:** Sample-rate negotiation has no failure rule. The rate is "negotiated …
target 96 kHz" (:27, dsp:45). If hw_params returns a different rate, every constant
shifts: period length in ms, FFT window, ring length in seconds, and biquad design.
**PROPOSED:** anything other than 96 kHz with 48 or 96 frames is a config error with a
distinct exit code listed in `RestartPreventExitStatus` (see F10).

**Decision:** the proposal (exit 20). In addition, capture and playback must agree on
rate, period and buffer size, and `-EBUSY` on open is a normal restartable error, not a
config error. Fully closed.

### C16 (issues.md §2)
**Original text:** The sample-format conversion point isn't specified. UMC202HD capture is
likely S24_3LE or S32_LE (confirm with `/proc/asound/card*/stream0` on the Pi). The ring
holds float32 and the biquads use float64. **PROPOSED:** convert in the RT loop (int →
float64 for the biquads, float32 into the ring), with a switch on whichever format
hw_params negotiated.

**Decision:** the proposal, with these specifics:
- a whitelist of S32_LE, S24_3LE and S24_LE (else exit 20);
- the switch made once per period, outside the per-sample loop;
- pure conversion functions with unit tests in src/common/.

The format check remains in issues.md.

### F1 (issues.md §2)
**Original text:** Polling with no syscall (mechanism B) is probably not true on arm64.
ProposedStructure.md and rework.md say audiod reads the hw pointer "directly in memory"
with "no syscall per check". The kernel likely allows mmap of the PCM status/control pages
only on x86/PPC/Alpha; on arm64, alsa-lib falls back to the `SNDRV_PCM_IOCTL_SYNC_PTR`
ioctl, which takes the PCM stream lock that the xHCI IRQ thread also takes. Check with
`strace -c` on the Pi before writing the loop around it.

**Decision:** write the loop so it doesn't depend on the answer: a time-gated poll that
spins on vDSO `clock_gettime` and calls `snd_pcm_avail_update()` only near the predicted
boundary. The strace check remains in issues.md, now only to tune the margin and the
rationale wording.

### F2 (issues.md §2)
**Original text:** `READY=1` timing is undefined. Before or after both streams are locked?
This sets audiod's `TimeoutStartSec` and when units ordered `After=audiod` start. Suggest:
after lock.

**Decision:** after lock (and after the shm segments exist), before unmute. Waiting for
unmute would deadlock, since unmute needs ctld and ctld is ordered after audiod.
`TimeoutStartSec=15s`. Fully closed.

### F9 (issues.md §4)
**Original text:** The service user and permissions are undefined. The unit files are
Hazel's; creating the user on the Pi is os/ work, so Aaron's. Do the daemons run as root
or as a dedicated user? If not root: audiod needs `LimitRTPRIO=80` (or `CAP_SYS_NICE`) and
`SupplementaryGroups=audio`, on top of `LimitMEMLOCK=infinity`; all three daemons need a
shared group for the `/dev/shm` segments; it should be a *system* user, so logind's
`RemoveIPC` never deletes the segments.

**Decision:** one system user, `dspunit`, for all three daemons, with segments at mode
0600. A single user needs no shared group, and per-daemon users with group write would
add setup without adding isolation. No `DynamicUser=`. Creating the user and an EPERM
check remain in issues.md.

### F10 (issues.md §4)
**Original text:** There's no exit-code table. Hazel lists the codes for audiod and
analyzerd and puts them in the units' `RestartPreventExitStatus`. Aaron lists the codes
for ctld. audiod's list must include the shm config error from C12 (a segment it can't
unlink).

**Decision (audiod):** the table in plan.md F10 and ProposedStructure.md §5. The scheme is
1 internal, 10-19 restart, 20-29 config, with `RestartPreventExitStatus=20 21 22`.
analyzerd's and ctld's codes remain in issues.md.

### F11 (issues.md §4, Hazel's part)
**Original text:** Writing `systemd/rt-tune.service` itself, and whether audiod uses
`Requires=` or `Wants=` on it.

**Decision:** a oneshot with `RemainAfterExit=yes` and `Conflicts=irqbalance.service`.
audiod uses `Wants=` during bring-up, so a spurious rt-tune failure costs jitter, not all
audio, and moves to `Requires=` once Aaron's rt-tune.sh fix lands. The switch is
plan.md F11's Outstanding item; the script fix remains in issues.md.

### D2 (issues.md §6)
**Original text:** The watchdog doesn't cover exit codes. ProposedStructure.md:103 says
the watchdog setup "covers … clean exit codes", but exit codes come from the
error-handling policy in §5.

**Fix:** ProposedStructure.md §2 now says the watchdog covers the heartbeat and hangs, and
that exit codes come from §5 and its table. Fully closed.

## F5. Decided and implemented (2026-10-29). See plan.md

**Original text (issues.md §3):** The FFT source is undecided. SOFT. `LDLIBS_ANALYZERD`
has no FFT library, which suggests writing our own 32768-point FFT. Decide our own FFT vs
FFTW explicitly, given the "intelligence in analysis/ is ours" boundary.

**Decision:** our own FFT.
- The boundary (CLAUDE.md) bans LLMs, cloud inference and pretrained models, and an FFT
  library is none of those. The graded intelligence is the GCC-PHAT delay estimate, the
  H(f) smoothing and the EQ derivation, all built on top of the FFT.
- Still, the boundary is graded, and our own FFT is about 150 lines. It also avoids a new
  library in the missing aarch64 sysroot (F12), GPL code, and FFTW planner state under
  valgrind.
- It's in `src/common/fft.{h,c}` with tests in `tests/test_fft.c`. It isn't in
  `analysis/` because of F4.
- It leaves one Makefile item for Aaron: `LDLIBS_CTLD` needs `-lm` (issues.md F4).

## Stale text (Hazel's items). Fixed (2026-10-29)

**Original text (issues.md §7):**
- Terminology.md:31 has the same stale fold-in note (as CtldOutline.md §2; settled by
  B12).
- ProposedStructure.md:197 still names `nrpacks` as the period constraint (part of
  audiod's period config), but C3 says it was removed in Linux 3.13. The real limit is
  snd-usb-audio's minimum period (`snd_pcm_hw_params_get_period_size_min`).

**Fixes:**
- Terminology.md:31 now names only the seqlock as unclear, pointing at issues.md C10.
- ProposedStructure.md's data-flow table (audiod RT loop row, now :203) verifies against
  `snd_pcm_hw_params_get_period_size_min` (issues.md H1) instead of `nrpacks`.
- The C9 comment references in `src/common/ring.h` and `tests/test_ring.c` now point at
  resolved_issues.md, since C9 left issues.md. The `issues.md C8` references stay while
  Aaron's C8 review is open.

## E. New: issues within images/dsp-architecture.svg. RESOLVED (0cc5520)

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
