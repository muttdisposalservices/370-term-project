#!/bin/sh
# os/rt-tune.sh
# RT tuning for the Source-vs-System unit (ProposedStructure.md §2).
# Idempotent; run as root by systemd/rt-tune.service (oneshot, before audiod).
set -eu

OTHER_CORES="0-1"          # core 2 is analyzerd's; core 3 is audiod's RT thread only --
                           # no IRQ, including xhci_hcd, is pinned there (plan.md C6)

log() { echo "rt-tune: $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[ "$(id -u)" -eq 0 ] || die "must run as root"

XHCI_IRQS=$(awk -F: '/xhci[-_]hcd/ {gsub(/^[ \t]+/,"",$1); print $1}' /proc/interrupts)
[ -n "$XHCI_IRQS" ] || die "no xhci_hcd IRQ found in /proc/interrupts -- is the UMC202HD attached?"

set_affinity() {
    irq=$1; target=$2
    if [ -w "/proc/irq/$irq/smp_affinity_list" ]; then
        echo "$target" > "/proc/irq/$irq/smp_affinity_list" 2>/dev/null \
            || log "WARNING: could not set affinity for irq $irq; skipping"
    else
        log "WARNING: /proc/irq/$irq/smp_affinity_list missing, skipping"
    fi
}

ALL_IRQS=$(awk -F: '/^ *[0-9]+:/ {gsub(/^[ \t]+/,"",$1); print $1}' /proc/interrupts)
for irq in $ALL_IRQS; do
    case " $XHCI_IRQS " in
        *" $irq "*) log "moving xhci irq $irq to cores $OTHER_CORES (not core 3 -- plan.md C6)" ;;
    esac
    set_affinity "$irq" "$OTHER_CORES"
done

if systemctl list-unit-files irqbalance.service --no-legend 2>/dev/null | grep -q irqbalance; then
    systemctl disable --now irqbalance || die "failed to disable irqbalance"
else
    log "irqbalance not installed, skipping"
fi

found_any=0
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -e "$gov" ] || continue
    found_any=1
    echo performance > "$gov" || log "WARNING: could not set governor at $gov"
done
[ "$found_any" -eq 1 ] || die "no cpufreq scaling_governor sysfs entries found"

log "done"
exit 0
