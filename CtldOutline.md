# ctld: design outline

## 1. Role
`ctld` is the control daemon: the control plane on a Unix domain socket, handling presets,
limits, bypass and meters (`Terminology.md`). It translates UI requests into parameter
writes and is the only producer of the `ctld → audiod` param block. It samples audiod's
status block at 20 Hz and serves meters to `ui` (`ProposedStructure.md` §3). Lang/placement:
C17, `src/ctl/`, `SCHED_OTHER`, cores 0-1.

## 2. Open question (not resolved here)
The design doc also says ctld "may be folded into the UI backend," in which case `src/ctl/`
disappears entirely (`Terminology.md`'s own "things to clean up" note). The `Makefile`
(`CTLD_OWN`, `$(BIN_NATIVE)/ctld`) and the `CLAUDE.md` ownership split ("Aaron: ctld") both
already treat it as a standalone daemon, and this outline follows that for concreteness —
but the fold-in alternative is still on the table and this doc does not settle it.

## 3. Responsibilities
- Serve `ui` requests over a Unix domain socket: presets, limits, bypass, meter queries.
  `ui` talks only to this socket and never maps shm directly (`ProposedStructure.md` §3).
- Be the sole producer of the `ctld → audiod` triple-buffered param block (bypass, limits,
  presets, dynamics); publish on change, capped at 50/s.
- Consume the `audiod → ctld` status/meter block (peaks, gain reduction, xrun count,
  heartbeat counter); sample it at 20 Hz and push meters onward to `ui`.
- Own preset storage as actual persisted data (§6).
- Be supervisable under systemd: `Type=notify`, watchdog heartbeat, clean exit codes, no
  orphaned fds across restart (`CLAUDE.md` hard constraints).

## 4. Module breakdown (`src/ctl/`)
- **`main.c`** — argument parsing; attaches to the shared param/status blocks; opens the
  Unix socket; runs the event loop (socket I/O + 20 Hz meter tick); `sd_notify`
  READY/WATCHDOG; `goto cleanup` teardown on exit (`CLAUDE.md` style rules).
- **`sock.c` / `sock.h`** — Unix domain socket server: accept/read/write framing for `ui`
  connections. No business logic here.
- **`params.c` / `params.h`** — owns in-memory param state (bypass, limits, dynamics,
  active preset); publishes it into the triple buffer whenever it changes.
- **`presets.c` / `presets.h`** — preset storage: defaults, load/save to disk.
- **`meters.c` / `meters.h`** — reads the status block at 20 Hz and formats it for the
  socket protocol.
- **`ctld.h`** — internal types shared across the files above.

## 5. Shared contracts (land in `src/common/`, coordinate with Hazel)
These are cross-daemon layouts, not ctld's to decide alone — `audiod` reads/writes the same
memory:
- Param block and status block struct layouts.
- The generic triple-buffer mechanism (also used by `analyzerd`'s coefficient block).
- `sd_notify` wrapper and logging, shared by every daemon.

ctld's module breakdown above assumes these live in `src/common/` and match whatever
`audiod`/`analyzerd` expect.

## 6. Persistence (candidate fix for an open project gap)
`rework.md` §1 flags mechanism D (storage with crash-consistency) as currently unclaimed —
the SPSC ring lives in `/dev/shm` (tmpfs) and is wiped on power loss, so there's no real
crash-consistency story yet. ctld, as the owner of presets, is the natural place for a
small on-disk append-only preset/session log that would close this gap. Flagged here as a
candidate, not a committed decision.

## 7. Socket protocol (decided and implemented, 2026-10-29)
Newline-delimited ASCII, `VERB arg1 arg2...\n`. Implemented in
`src/ctl/ctldproto.{h,c}`, tested in `tests/test_ctldproto.c`. Socket I/O itself
(`src/ctl/main.c`/`sock.c`) is not yet written.

## 8. Testability split (per the Makefile's existing philosophy)
**RESOLVED (2026-10-30):** `ctldproto.{h,c}`/`ctldstate.{h,c}` moved to `src/ctl/`, where
they semantically belong (they're ctld-only -- nothing in audiod or analyzerd ever
included them, so leaving them in `src/common/` meant those two daemons were compiling
and linking dead ctld-only code for no reason). `TESTABLE_SRCS` now adds
`$(filter-out src/ctl/main.c,$(CTLD_OWN))`, mirroring the existing `ANALYZERD_OWN`
pattern one line above it, and `CFLAGS` gained `-Isrc/ctl` so files outside `src/ctl/`
(the tests) can still resolve `ctldproto.h`/`ctldstate.h`. For ctld this still means:
- Pure logic — param validation, preset (de)serialization — lives in `src/ctl/` and is
  covered by the test targets via the `CTLD_OWN` filter above, same as analyzerd's.
- Socket/event-loop/`sd_notify` glue stays in `src/ctl/main.c`, filtered out of
  `TESTABLE_SRCS` by name once it exists, untestable directly, exercised only via
  `make soakcheck`.

## 9. Failure / supervision behavior
Per `ProposedStructure.md` §5, as it applies to ctld:
- Recoverable errors (e.g. a malformed request) are handled in-process and only counted.
- Unrecoverable errors (corrupt shm, repeated failures) go: log → cleanup → exit with a
  distinct nonzero code; systemd restarts (`Restart=on-failure`, `StartLimitBurst` guards
  against restart storms).
- On restart, ctld reattaches to the existing shm segments and checks their magic/version
  rather than assuming a cold start.
- A process that hangs misses its watchdog ping and is killed and restarted the same way.
