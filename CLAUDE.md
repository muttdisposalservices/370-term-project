# CS-370 Term Project: team rules
## Commands
- Build: ‘make‘ Cross/deploy: ‘make deploy PI=pi@<host>‘
- Tests: ‘make test‘ Sanitizers: ‘make asan‘ Valgrind: ‘make memcheck‘
- Soak sanity: ‘make soakcheck‘ (1-hour miniature of the 48h run)
- A change is DONE only when build, test, and asan pass. Show output.
## Hard constraints
- The PRODUCT makes no network calls except serving its own LAN
interface. No LLM APIs, no cloud inference, no pretrained models.
The intelligence in analysis/ is ours. This is a graded boundary.
- Every daemon must be supervisable: clean exit codes, no orphaned
fds across restart, heartbeat within 60s of start.
- Every allocation checked; every syscall’s error path handled and
logged. The 48-hour soak is the test suite of last resort.
- NEVER weaken, skip, or delete a test to make the suite pass.
- Do not write to PROMPTLOG.md.
- Never push to the repo. This will only be done manually.
## Ownership
- Hazel:
-- audiod: input/output daemon
-- analyzerd: analysis and computation daemon
-- ui: ungraded but very necessary UI
-- tools: ungraded Python assists
- Aaron:
-- Hardware: Prototyping and constructing unit
-- Makefile: nested here to be able to test 'make deploy' on LAN
-- ctld: user input parameter control daemon
-- os/ various OS fragments needed for boot and runtime config
- Both:
-- Tests: Since we're both handling different approaches (hardware implementation vs. software processing), tests should be written together. We'll also be able to reassign work where necessary when we meet for these.
## Style
- Systems core: C17, -Wall -Wextra -Werror, no VLAs.
goto-cleanup for multi-resource functions.
- Python permitted only under tools/ and ui/. No graded mechanism
may live there.
- Smallest diff that passes. Do not refactor unrelated code.
## Workflow
- Multi-file or algorithmic change: plan first, wait for approval.
- Hardware bugs: paste real evidence (dmesg, timing capture,
/proc/interrupts). No fixes proposed from a verbal description.