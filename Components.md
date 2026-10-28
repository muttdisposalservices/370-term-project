# CS-370 Term Project Components and Connections List
## Core Component
Raspberry Pi 5 4GB
- 2.5 GHz quad-core processor with per-core 512KB L2 caches and a shared 2MB L3 cache
- LPDDR4X-4627 SDRAM
- 27W power, active cooled, 32GB microSD card

## Audio Interface
Behringer U-Phoria UMC202HD, USB 2.0, class-compliant. Decided over the earlier generic
"UMC202HD or equivalent" placeholder -- now the specific model, specs confirmed below.
- 2x combo XLR/TRS inputs, each with a MIDAS preamp, gain, line/instrument selector and
  pad, shared switchable +48V phantom: input 1 = line level (desk monitor out) = Source,
  input 2 = mic preamp (dynamic mic) = System. **+48V: OFF.** The phantom supply is shared
  across both inputs, so switching it on for the (unneeded) dynamic mic would also put 48V
  onto the desk line-out in input 1.
- 2x balanced 1/4" **TRS** line outputs (processed mono signal, duplicated to both) + a
  1/4" headphone jack. Not XLR connectors, but TRS carries the same balanced
  (tip/ring/sleeve = hot/cold/ground) signal XLR does -- a TRS->XLR adapter cable keeps
  the connection balanced, it's a connector swap, not a noise-rejection compromise.
- Sample rate 44.1-192 kHz, including the 96 kHz target in images/dsp-architecture.svg -- no
  rate-cascade numbers change from what's already in that diagram.
- Enumerates under Linux's stock snd-usb-audio driver -- no vendor driver, no
  device-tree overlay. Known quirk: a one-time "clock source N is not valid" dmesg
  message at USB probe, fixed upstream via an implicit-feedback-mode quirk (merged for
  the UMC 202/204/404 HD series around kernel 5.19+) and reported harmless once that fix
  is present. VERIFY the Pi's kernel version includes it; if not, needs a newer
  Raspberry Pi OS image rather than a code change.
- Bus-powered over USB (no phantom power needed; dynamic mic doesn't require +48V).

## Connections
- Desk monitor out -> interface input 1 (XLR/TRS combo, line level).
- Dynamic mic -> interface input 2 (XLR, mic preamp gain set on the interface's own
  front-panel knob, +48V phantom left off).
- Interface USB port -> Pi 5 USB port.
- Interface balanced TRS outputs L/R -> TRS->XLR adapter cables -> PA / speakers.

## Power
- Pi 5 USB-C PSU, 27W (unchanged)
- Interface draws power from the Pi's USB bus -- VERIFY current draw against the Pi 5's USB port budget; a powered USB hub is the fallback if it doesn't fit

## Removed from the custom-board design
THAT1512SO8-U, THAT1246SO8-U, PCM1808PW, PCM5102APW, DRV134PA x2, Abracon ACHL-24.576MHZ-EK, Traco TMR 0522, MCP1700-3.3 -- all folded into the off-the-shelf interface above.