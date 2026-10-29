#!/bin/sh
# os/merge-boot-config.sh
# Idempotently merges os/cmdline.txt and os/config.txt's justified fragments
# into the live /boot/firmware/ files. Reads its values FROM those fragment
# files (siblings in the same directory, as rsynced by `make deploy`) rather
# than hardcoding its own copies, so the fragment files stay the single
# source of truth. `make deploy` only copies os/ to the Pi (copy-only by
# design -- it never touches the Pi's running/boot state itself); this
# script is the manual step that actually applies these two files, safe to
# re-run after every deploy without duplicating entries.
#
# What it does and why:
#   - cmdline.txt must stay a single line. Appending os/cmdline.txt's
#     fragment (isolcpus=3 nohz_full=3 rcu_nocbs=3, ProposedStructure.md §2,
#     reserves core 3 for audiod) blindly on every run would duplicate it on
#     each re-run; this checks first and only appends once.
#   - config.txt's Pi 5 firmware defaults to booting kernel_2712.img, not an
#     apt-installed RT kernel -- it has to be told explicitly via a `kernel=`
#     line under [pi5] (confirmed on hardware 2026-10-29, os/VERIFY.md). This
#     adds os/config.txt's kernel= line once; if a *different* kernel= is
#     already live, it warns instead of overwriting, since that'd silently
#     change what boots.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CMDLINE_FRAGMENT="$SCRIPT_DIR/cmdline.txt"
CONFIG_FRAGMENT="$SCRIPT_DIR/config.txt"
CMDLINE=/boot/firmware/cmdline.txt
CONFIG=/boot/firmware/config.txt

log() { echo "merge-boot-config: $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[ "$(id -u)" -eq 0 ] || die "must run as root"
[ -f "$CMDLINE" ] || die "$CMDLINE not found"
[ -f "$CONFIG" ] || die "$CONFIG not found"
[ -f "$CMDLINE_FRAGMENT" ] || die "$CMDLINE_FRAGMENT not found -- run this from a full os/ copy (make deploy's staging dir), not the script alone"
[ -f "$CONFIG_FRAGMENT" ] || die "$CONFIG_FRAGMENT not found -- run this from a full os/ copy (make deploy's staging dir), not the script alone"

ISOLCPUS_PARAMS=$(cat "$CMDLINE_FRAGMENT")
[ -n "$ISOLCPUS_PARAMS" ] || die "$CMDLINE_FRAGMENT is empty"

KERNEL_LINE=$(grep '^kernel=' "$CONFIG_FRAGMENT" | head -1)
[ -n "$KERNEL_LINE" ] || die "no 'kernel=' line found in $CONFIG_FRAGMENT"
RT_KERNEL_IMG=${KERNEL_LINE#kernel=}

# --- cmdline.txt: append once, never duplicate ---
if grep -qF "$ISOLCPUS_PARAMS" "$CMDLINE"; then
    log "cmdline.txt already has the isolcpus fragment, leaving alone"
else
    sed -i "1 s/\$/ $ISOLCPUS_PARAMS/" "$CMDLINE"
    log "appended '$ISOLCPUS_PARAMS' to cmdline.txt"
fi

# --- config.txt: add kernel= under [pi5] once, warn on mismatch ---
if grep -q "^kernel=" "$CONFIG"; then
    CURRENT=$(grep "^kernel=" "$CONFIG" | head -1)
    if [ "$CURRENT" = "kernel=$RT_KERNEL_IMG" ]; then
        log "config.txt already selects $RT_KERNEL_IMG, leaving alone"
    else
        log "WARNING: config.txt already has '$CURRENT' -- not touching it. Expected kernel=$RT_KERNEL_IMG (from $CONFIG_FRAGMENT). Resolve by hand."
    fi
else
    grep -q '^\[pi5\]' "$CONFIG" || die "no [pi5] section found in config.txt"
    sed -i "/^\[pi5\]/a kernel=$RT_KERNEL_IMG" "$CONFIG"
    log "added 'kernel=$RT_KERNEL_IMG' under [pi5] in config.txt"
fi

log "done -- reboot for changes to take effect"
