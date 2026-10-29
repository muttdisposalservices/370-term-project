# audioPiCS370 SD card reflash & recovery guide

**Only do this if the Pi is actually unbootable or the SD card is suspect.** This procedure
wipes the SD card completely. Everything the project depends on — the RT kernel, `isolcpus`
cmdline, `nftables` rules, SSH hardening, `rt-tune.sh`, the daemons themselves — is gone
afterward. This guide only gets you back to a stock, reachable Raspberry Pi OS. Re-applying
the project's own config is `make deploy` + `os/VERIFY.md`'s job, not this guide's.

## 1. When you actually need this

- The Pi won't boot at all (no activity, no link lights after a minute+).
- The SD card is corrupted/suspected bad (filesystem errors, repeated unexplained crashes).
- You've been told explicitly to start from a clean image.

If the Pi boots but something's merely misconfigured, fix that directly over SSH — don't
reflash.

## 2. What you need

- The microSD card (32GB) and a USB/SD reader on a PC.
- Raspberry Pi Imager (download from raspberrypi.com if not already installed).
- A free Raspberry Pi account. Imager prompts you to sign in (at id.raspberrypi.com) before
  it'll let you flash — it's free, just make one with any email if you don't already have
  one.
- The Pi 5, its USB-C PSU, and an Ethernet cable to the LAN.

## 3. Flash the card

1. Open Raspberry Pi Imager.
2. Choose Device: **Raspberry Pi 5**.
3. Choose OS: click **"Choose OS"** → **"Raspberry Pi OS (other)"** → **"Raspberry Pi OS
   Lite (64-bit)"**. Always pick **Lite**, never the plain/Desktop "Raspberry Pi OS
   (64-bit)" option on the main list — this box is headless and we want our picks
   consistent across reflashes. (64-bit matches the Makefile's `aarch64-linux-gnu-` cross
   toolchain — don't pick a 32-bit image.)
4. Choose Storage: the SD card, **not** anything else plugged into the PC. Double-check the
   size/label shown matches the SD card before continuing — this step is destructive to
   whatever you pick.
5. After Storage, Imager automatically asks whether to apply OS customisation settings and set:
   - **Hostname** — set it to `audioPiCS370`, our standing hostname for this Pi, so
     `make deploy PI=pi@<host>` and SSH always resolve it the same way.
   - **Username/password** — password auth is fine for now; SSH gets locked to key-only
     later as part of the real `os/` deploy, not during this reflash.
   - **Enable SSH** (password authentication) so you don't need a monitor/keyboard at all.
   - Locale/timezone/keyboard as appropriate.
6. Save, then confirm you want to write with these settings. Confirm any "this will erase
   the disk" prompt. Wait for Imager to finish writing *and* verifying.
7. Click **Finish**, then **eject the card** from the OS before physically removing it.

## 4. Physical handling — read before touching the card or the Pi

- **Never boot the card from the USB reader/dock plugged into the Pi.** Our Pi will not
  boot an SD card presented that way — it needs to be seated in the Pi's own microSD slot
  (on the underside of the board), not left in a USB adapter.
- **Make sure the Pi is fully unplugged before inserting or removing the SD card.**
  Hot-inserting/removing can fail to boot and can damage the card.
- **Never remove the SD card while the Pi is powered on.** This can corrupt or damage the
  card — exactly the failure mode this guide exists to recover from.
- **Always shut the Pi down cleanly before unplugging it**, whenever that's possible
  (`sudo shutdown -h now` over SSH, or `sudo poweroff`), and wait a few seconds after the
  activity LED stops before pulling power. Only yank power with no clean shutdown if the Pi
  is already unresponsive.

## 5. First boot

1. With the Pi unplugged, seat the SD card in the microSD slot.
2. Connect Ethernet, then connect power.
3. Give it 1-2 minutes on first boot (filesystem resize, SSH host key generation).
4. SSH in from another machine on the same LAN. On Windows, the `ssh` command is built in
   (Command Prompt or PowerShell, no extra install needed) — just run:
   ```
   ssh <user>@audioPiCS370.local
   ```
   using the username you set in Imager and the hostname `audioPiCS370`. If `.local`
   doesn't resolve on your network, find the Pi's IP instead (check your router's client
   list, or plug in a monitor/keyboard and run `ip addr` directly on the Pi) and use
   `ssh <user>@<ip>`.
   - First connection only: you'll get a prompt like
     `The authenticity of host '...' can't be established... Are you sure you want to
     continue connecting (yes/no)?` — type `yes`. This is normal for a brand-new Pi; it's
     just SSH recording the Pi's host key for next time.
   - Then enter the password you set in Imager when prompted.
5. Once logged in, confirm basics:
   ```
   hostnamectl          # hostname reads audioPiCS370
   uname -a             # arm64/aarch64 kernel, sanity check
   ip addr               # has a LAN address
   ```
6. Update the base OS:
   ```
   sudo apt update && sudo apt full-upgrade -y
   sudo reboot
   ```

## 6. Where this guide stops

At this point you have a stock, patched, SSH-reachable Raspberry Pi OS box on the LAN —
nothing project-specific yet. From here:

- The RT kernel, `cmdline.txt`/`config.txt` merge, `rt-tune.sh`, `nftables`, SSH
  key-only hardening, and the daemons themselves come from
  `make deploy PI=<user>@audioPiCS370.local` (see the Makefile's `deploy` target) followed
  by the manual install steps it prints.
- Confirm all of that actually took effect using `os/VERIFY.md`'s checklist — don't assume
  it worked from a verbal description, per `CLAUDE.md`'s hardware-evidence rule.

## 7. Project-specific setup (after `make deploy`'s copy step)

`make deploy` is copy-only by design — it rsyncs `bin/`/`os/`/`systemd/` to the Pi and
prints the install steps, but never runs them for you (Makefile header comment). Do these
by hand after a deploy, in order, confirming each against `os/VERIFY.md` with real pasted
output as you go.

1. **Install and hold the RT kernel** (needs the open network, before any firewall is
   enabled):
   ```
   apt search linux-image-rpi      # confirm the exact package name on-device, don't assume
   sudo apt install linux-image-rpi-v8-rt
   sudo apt-mark hold linux-image-rpi-v8-rt
   ```
2. **Merge the boot config and select the RT kernel.** The Pi 5 firmware defaults to
   `kernel_2712.img` and won't pick up the newly-installed RT image on its own, and
   `cmdline.txt` needs the CPU-isolation params appended without duplicating them on a
   repeat deploy. `os/merge-boot-config.sh` (rsynced to the Pi as part of `os/` already)
   does both idempotently:
   ```
   sudo sh ~/dspunit-deploy/os/merge-boot-config.sh
   sudo reboot
   ```
3. **Verify the RT kernel actually took**, per `os/VERIFY.md`: `uname -r`/`uname -v` show
   the RT kernel, `/proc/cmdline` contains `isolcpus=3 nohz_full=3 rcu_nocbs=3`,
   `CONFIG_PREEMPT_RT=y` in `/boot/config-$(uname -r)` — not `/sys/kernel/realtime`, that
   file doesn't exist on this kernel build (see `os/VERIFY.md` for why).
4. **Run `os/rt-tune.sh`** (needs the UMC202HD attached — it sanity-checks for an
   `xhci-hcd` IRQ):
   ```
   sudo sh ~/dspunit-deploy/os/rt-tune.sh
   ```
   Run it twice and confirm identical output (idempotency), then confirm the xHCI IRQs
   actually moved: `cat /proc/irq/<irq>/smp_affinity_list` should read `0-1`.
5. **Verify the RT isolation holds under load** before trusting it:
   ```
   sudo apt install -y rt-tests stress-ng
   sudo taskset -c 0-2 stress-ng --cpu 3 --timeout 90s &
   sudo cyclictest -a 3 -p 90 -i 200 -D 60 -q
   ```
   Start both lines back-to-back so the cyclictest window is fully covered by load. Target:
   worst-case `Max` well under 100 µs (measured ~5 µs on 2026-10-29).
6. **`os/nftables.conf` — do not enable until both placeholders are real:**
   - `UI_PORT`: not decided yet — `ui/` (Hazel's) hasn't picked a listen port. Sync with
     her before enabling.
   - `LAN_SUBNET`: currently set broad (all RFC1918 ranges) because the unit is still
     moving between development networks with a different subnet each time. **Once the
     final deploy/demo location is fixed**, narrow this to that location's real subnet:
     ```
     ip -4 addr show      # read the actual CIDR off the LAN-facing interface
     ```
     and replace the broad range in `os/nftables.conf` with that one real value — don't
     ship the demo on the development-wide version, it's looser than the design calls for.
   - Only once both are real: `sudo cp ~/dspunit-deploy/os/nftables.conf /etc/nftables.conf`
     then `sudo systemctl enable --now nftables`, and verify with `nft list ruleset` plus
     the LAN/off-LAN/outbound checks in `os/VERIFY.md`. **Keep a console session open while
     testing** — a bad rule can lock out the only SSH path.
