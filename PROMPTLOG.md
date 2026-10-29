# CS-370 Promptlog/Devlog
## 10/16 [Hazel]
- Pointed Claude to component list, problem file, and CLAUDE rules. Asked for a walkthrough of the OS structure to be implemented.
- Asked about process naming convention clarifications, context on when proposed analyzer process switches buffers, full-system delay to compensate for latency, and audio/analyzer process restarts on failure. Clarified gaps regarding power supplies and dynamic vs. condenser requirement.
- Decided to use duplicate-mono stereo out due to single-ADC setup; adding a second input would allow for stereo in/out in a later model.
- Logged proposed structure in ProposedStructure.md.

## 10/23 [Hazel]
- Pointed Claude to proposed structure and asked for a walkthrough of the OS configuration. Discovered that the Pi doesn't have a native real-time kernel, so I pointed it to a copy-paste of a blog post and let it Google stuff. Took the usage hit for the sake of understanding how to implement PREEMPT_RT, since the forum website are godawful on the eyes and the information was nested multiple references deep. Decided to install an experimental RT build which people have had neutral-to-positive experiences with. For low-latency GPIO, the experimental risk is worth it.
- Update the firewall to accept LAN key-only SSH for deploy and port inputs only with no outbound traffic. Fumbled the first iteration because I misunderstood the header.
- Also directed away from PROMPTLOG. None of its business.
- Proposed structure updates now include data regarding expected latency with respect to sample rate and frame periods. Additionally, RBP OS stuff was included in the proposed repo to prevent updating RBP stuff on the Pi itself.
- Included tests specifically to ensure real-time kernel usage, plus proper watchdog monitoring.
- Updated repo.

## 10/26 [Hazel]
- Pointed Claude to proposed structure and asked for help making an architecture diagram. Clarified specific rates, buffer quantities, and EQ specifications. Clarified diagram type (also to exclude the section 3.2 bit from the sample diagram lol) and pushed to include a legend. Moving to failure-mode table.
- Updated ownership in CLAUDE.md.

## 10/28 [Hazel]
- Came back to figure out what changed after Aaron redirected the project to using an external audio interface. Pointed Claude at the repo to analyze for discrepancies between multiple design documents. Put discovered problems in discrepancies/issues.md.

## 10/28-29 [Hazel]
- Pushed Claude to look through the current version of issues.md for anything preventing work on audiod/analyzerd/systemd. Updated issues.md to reflect Aaron's recent resolutions. Created new resolved_issues.md and plan.md files to track problem fixes, current problems, and active decisions. Added tags to each task for myself and Aaron's sakes.
- Started working on shm contract. Began with the triple-buffer release protocol for audiod/analyzerd. Opted for triple-buffer for audiod/analyzerd; each owns one slot with one guaranteed free slot. Added common/tbuf.c, ".h and tests/test_tbuf.c. make build, test, deploy, asan pass, but there's a C++ library issue. tests for:
--bad arguments and a bad header;
--publish and poll, including a newer publish replacing an unread one;
--10,000 random interleavings, checking each side always holds a different slot and that a restarted producer recovers its slot;
--attach returning -EAGAIN when it catches audiod mid-swap, and -ESTALE after audiod re-initialises;
--a two-process test with 20,000 publishes over a shared mapping that checks no read is ever half-written.
Pushing to repo.
- Moved on to SPSC ring implementation to manage partial production/reading issues. Updated docs after other tests + ring stability test pass. Pushing to repo.
- Pointed Claude to stale text entry of issues.md to prevent miscomm. Just edited ring details to let Aaron handle his own data.

## 10/29 [Hazel]
- Asked for walkthrough and draft of src/common seqlock for audiod>ctld status block. Gave soakcheck access to status block through ctld socket, so soakcheck isn't a seqlock reader. Did not update the status block payload struct. Seqlock used for single writer/many reader config. Implemented with tests, and stress test confirms the seqlock prevents torn reads (removed seqlock + retest > fail after with seqlock > pass). If writer dies, readers get -EAGAIN and watchdog notifies systemd. Updated docs.
- Claude pointed out a potential race condition where the reader's memcpy outruns the writer's update, but the seqlock prevents torn reads, so that fence should be sufficient, since readers will often outpace the writer without issue by design.
- Claude did point out that Makefile has a flag which would cause audiod's RT to not be very RT (unoptimized because of a missing -O); flagged in issues.md for Aaron. Committed.
- Moved into startup defaults with the following preferences: flat EQ, default limiter, muted output until safe settings measured and applied to prevent pops. In my experience as an audio engineer, this is the best way to go, since people LOVE turning speakers on after processors, so this could maybe prevent a DC pop? audiod will self-check its audio and unmute when ctld wakes and responds. Compiled-in limiter setting is -6 dBfs to prevent loud output without being way too heavy. Just implementing docs now until EQ biquads and limiting is programmed. Status blocks will include mute status and reason so ctld/ui can understand why mutes happen. On audiod restart, re-init, producers get -ESTALE, audiod reattaches shm, and republishes current state. To handle audiod hanging, added a separate watchdog thread and threshold. Tests pass, committing.