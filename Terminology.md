Daemons (the trailing d)
A daemon is a long-running background process that systemd starts, watches and restarts. The d suffix is the Unix naming habit, as in sshd.

audiod: the real-time audio loop. It reads the ADC, applies EQ and dynamics, writes to the DAC, and feeds raw samples to the analyzer.
analyzerd: the graded part. It estimates delay, computes the transfer function and solves for the EQ.
ctld: the control daemon, not "controller". It is the control plane on a Unix socket, handling presets, limits, bypass and meters. Always a separate C17 daemon from the UI -- the UI is Python and never maps shm, and ctld is the sole producer of the shm param block.
src/common/
shm ring: a ring buffer in shared memory (/dev/shm), meaning a fixed-size circular queue that two processes map at the same time. Your design specifies a lock-free SPSC ring (single-producer, single-consumer): audiod writes and analyzerd reads.
seqlock: a sequence lock. The writer bumps a counter before and after each update. A reader retries if the counter was odd or changed while it was reading. Writers never wait, but readers may have to retry. It protects the audiod → ctld meter/status block (`src/common/seqlock.h`, resolved_issues.md C10). Coefficients and params go through triple buffers, not a seqlock.
sd_notify wrapper: sd_notify() is how a daemon tells systemd "I'm ready" (READY=1) and "I'm still alive" (WATCHDOG=1). This is the "heartbeat within 60s" from CLAUDE.md. "Wrapper" means your own small function around it, so that every caller gets the error handling the rules require.
analysis/
delay est.: estimating how far the System signal lags behind Source, using cross-correlation or GCC-PHAT. In your design, Source is the clean input and System is what a measurement mic picks up from the room.
transfer fn: the transfer function H(f) = System / Source per frequency. It describes how the speakers and room change the sound.
EQ solver: turns H(f) into bounded correction filters.
tests/
biquads: second-order IIR filters, the standard building block for a parametric EQ. The name comes from the bi-quadratic transfer function.
os/
cmdline.txt / config.txt fragments: the Pi's boot files. cmdline.txt holds kernel arguments, such as isolcpus=3 to reserve core 3. config.txt holds firmware settings like dtoverlay=. "Fragments" means you add lines to the stock files rather than replacing them.
nftables.conf: firewall rules. This is how you enforce "LAN only, no outbound".
sshd_config.d/: drop-in files that harden SSH without editing the main config.
rt-tune.sh: the real-time tuning script. It sets IRQ affinity, the CPU governor and similar settings.
kernel/ (fallback build notes only): no kernel source lives here. These are just notes on building a PREEMPT_RT kernel yourself in case the packaged one doesn't work. PREEMPT_RT is the Linux build option that makes the kernel preemptible enough for hard real-time scheduling.
systemd/
unit files: systemd's service definitions (.service).
oneshot: a unit type that runs once and exits instead of staying resident. rt-tune runs before audiod starts, then exits.
Makefile targets
asan: a build with AddressSanitizer, which catches memory errors at runtime.
memcheck: running under Valgrind's leak and invalid-access checker.
soakcheck: a one-hour version of the 48-hour "soak" endurance run. Soaking means running continuously to catch leaks, drift and fd exhaustion.
deploy: cross-builds, then copies binaries plus os/ and systemd/ to the Pi.
Things to clean up: none. The seqlock was the last unclear term and is now settled (resolved_issues.md C10). Everything else is standard terminology.