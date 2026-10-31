# What Hazel can do before Aaron's checks

## Context
Hazel asked what work can get done before Aaron's hardware checks and reviews come back.
The tree as of 54c43b9:
- **Done:** every pure src/common piece and its tests: biquad, limiter, mutegate, pace,
  pcmfmt, stall, fft, ring, tbuf, seqlock and shm.
- **Empty:** `src/audio/`, `systemd/` and `analysis/`.
- **On this lab box:** the ALSA and libsystemd headers are installed (`pkg-config` finds
  `-lasound -lsystemd`), and there's an HDA Intel card at hw:0. So audiod can build
  natively here, and its open/negotiation error paths can be smoke-tested.

**The main finding:** almost none of Aaron's HW checks block code. H1, H3, C16, F1 and
SELFCHECK_MS only tune constants or decide which already-written path runs. Each of those
constants already exists as a named placeholder (`PACE_MARGIN_NS`, `SELFCHECK_MS`) or can
be added as one. **Update (2026-10-30, Aaron):** H1, H3, C16 and F1 all landed with real
evidence -- see issues.md. H1: 48-frame period accepted, both directions. H3: ~0 ms
settle, no deliberate wait needed. C16: `S32_LE`, `S24_3LE` not offered by the hardware
at all. F1: confirmed real `SNDRV_PCM_IOCTL_SYNC_PTR` ioctl on arm64, not mmap-only.
`SELFCHECK_MS` is now unblocked too (plan.md C11). Four things are actually blocking:
1. The **param-block layout** (Aaron, issues.md C11/C8). Without it, audiod can't size or
   validate the param segment, so the unmute path can't be finished. **Update
   (2026-10-29, Aaron):** landed -- `struct param_block` in `src/common/param.h`
   (magic/version + bypass + ceiling_dbfs, `param_valid()`/`param_default()`), tested
   in `tests/test_param.c`, `make test`/`make asan` both pass. See issues.md C11 for
   the full writeup. Should unblock sizing/validating the param segment and finishing
   the unmute path -- flag it here if anything about the layout doesn't work for you.
2. **F4 (Makefile).** analyzerd can't have a main.c yet. **Update (2026-10-29, Aaron):**
   fixed, see issues.md F4. analyzerd can have a main.c now.
3. **ctld's socket protocol.** ui and `tools/soakcheck.py` wait on it.
4. **F12 (sysroot).** There's no cross build or deploy yet.

## Recommended order (each chunk gets its own plan/approval per CLAUDE.md)

### 1. Quick wins, fully decided (small, independent)
- **Note (2026-10-30):** `systemd/` ownership moved to Aaron (resolved_issues.md F15).
  Writing these three unit files is now his; the `Wants=`/`Requires=` call on
  `audiod.service` (F11) still follows Hazel's read of audiod's reliability.
- `systemd/audiod.service`: C5, C13, F2, F8, F9, F10, F11 (`Wants=` rt-tune for now).
- `systemd/rt-tune.service`: F11.
- `systemd/analyzerd.service`: F6, F8, F9, F10 (`RestartPreventExitStatus=21`).
- The audiod exit-code header in `src/audio/` (F10 table).
- The analyzerd exit-code header can go in `analysis/` now. Only a `.c` there triggers the
  analyzerd link (the Makefile's `ANALYZERD_OWN` is `analysis/*.c`), and plan.md F10 only
  says "once F4 lands" because it put the header next to main.c.
- Verify with `systemd-analyze verify` locally. Expect warnings for the missing
  binary and user, and read past them.

### 2. Define the status-block payload (Hazel owns it; it unblocks Aaron)
- Add a `src/common/status.h` struct with the fields listed in ProposedStructure.md §3:
  - peaks, GR, xruns and heartbeat;
  - `mute_state`/`mute_reason`;
  - the rejected-block count.
- Add a coeff-block typedef (19 × 5 float64).
- Add both to issues.md for Aaron's ctld review.
- ctld's meters reply needs this, so landing it early helps Aaron more than anything
  else on this list.
- Optionally, draft a param-block proposal for Aaron, with a nonzero magic/version word,
  bypass and a ceiling field. It would be a proposal only, since the layout is his.

### 3. analyzerd's graded core, as pure functions in src/common/ (the biggest, highest-value chunk)
- **GCC-PHAT delay estimation** over the 100 ms search, using `fft.c`.
- **H(f) = System/Source with time and frequency smoothing** (≈1/3-octave), with
  NaN reset per F10.
- **The bounded EQ solver.** It maps the smoothed H(f) to 19 biquads, caps boost,
  prefers cuts, applies the freeze gate, and its output must pass `biquad`'s validation.
- Tests use synthetic signals generated in the C tests: a known delay plus noise, a
  known filter, and edge cases.
- This needs no hardware, no Aaron and no Makefile change, and it's the graded
  "intelligence". It's algorithmic, so it gets its own plan first.

### 4. audiod's main.c, which builds here but runs fully only on the Pi
Split it into reviewable pieces:
1. PCM open plus hw_params/sw_params negotiation (C2, C15, C16, C14) and the exit codes.
2. The start/relock function (C4). `CAPTURE_SETTLE_MS` -- H3 resolved at ~0 ms settle
   (issues.md H3), no deliberate wait needed.
3. RT-thread self-pin and `mlockall` (C5), with exit 22.
4. The RT loop: pace spin/poll (F1) → pcmfmt → biquads → limiter → mutegate → ring
   push → status seqlock write → coeff `tbuf_consumer_poll`.
5. The watchdog thread: `stall.c`, `_exit(12)`, `WATCHDOG=1`, and `READY=1` after lock
   (F2).
6. The segment create/repair (C12) for ring, coeff and status.
   - **Param segment:** leave it as a stub until Aaron's layout lands. The output then
     stays muted, which is the fail-safe C11 already requires.
   - **Smoke tests here:**
     - `make` builds audiod.
     - Pointing it at hw:0 (HDA Intel) should hit the negotiation path. It will most
       likely refuse 96 kHz/48 frames and exit 20.
     - Running with no card should give the expected code.
   - Real loop behaviour waits for the Pi.

## Still blocked (don't start)
- **Param validation and the full unmute path:** the param layout (Aaron).
- **analyzerd main.c and moving the pure functions into analysis/:** F4.
- **ui and tools/soakcheck.py:** ctld's socket protocol.
- **Switching audiod to `Requires=rt-tune.service`:** F11.
- **The F14 loop yield:** Aaron's check. **Note (2026-10-29, Aaron):** check came back
  negative for a fix -- real evidence (0 gaps over 100 µs, 180 s, both idle and with
  cores 0-2 loaded) says RT throttling doesn't stall the loop in practice, see
  issues.md F14. Marking this unblocked/no-fix-needed unless you disagree -- flag it if
  so and we'll re-open it there.
- **The C14 free-running rework:** only if the pop check says so.

## Verification (for every chunk)
- `make`, `make test` and `make asan` must pass, with the output shown (CLAUDE.md).
- Run `make memcheck` too, via the `battery` skill.
- Never weaken a test.
