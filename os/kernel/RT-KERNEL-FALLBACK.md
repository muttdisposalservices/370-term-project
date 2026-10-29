# PREEMPT_RT kernel: fallback build notes (not run automatically)

Use only if no prebuilt `linux-image-rpi-v8-rt` package works on the Pi 5. Try apt first:

    apt search linux-image-rpi      # confirm exact package name on-device
    sudo apt install <package>
    sudo apt-mark hold <package>

If that fails or doesn't show PREEMPT_RT, build from source:

1. Clone the kernel source (confirm current branch/tag on-device -- do not assume a version).
2. `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make bcm2712_rt_defconfig`
   (RT is in-tree since 6.12 -- no patches needed, ProposedStructure.md §2).
3. `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -j$(nproc) Image.gz modules dtbs`
4. Install modules/Image/dtbs per Raspberry Pi's kernel-building docs for the Pi 5.
5. Reboot, then verify:
   - `uname -v`/`cat /proc/version` -> contains "PREEMPT_RT"
   - `grep PREEMPT_RT /boot/config-$(uname -r)` -> `CONFIG_PREEMPT_RT=y`
   - **Do not rely on `cat /sys/kernel/realtime`.** Confirmed absent on the apt-installed
     `linux-image-rpi-v8-rt` package despite `CONFIG_PREEMPT_RT=y` being true (os/VERIFY.md)
     -- that sysfs entry is a downstream Debian/SUSE/RHEL patch, not something a from-source
     mainline build carries either. Use the two checks above instead.
   - `cyclictest -a 3 -p 90` under `stress-ng` load -> worst case well under 0.67 ms
