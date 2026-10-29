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
