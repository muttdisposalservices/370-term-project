# CS-370 Term Project Components and Connections List
## Core Component
Raspberry Pi 5 4GB
- 2.5 GHz quad-core processor with per-core 512KB L2 caches and a shared 2MB L3 cache
- LPDDR4X-4627 SDRAM
- 27W power, active cooled, 32GB microSD card

## Converters
Texas Instruments PCM1808PW
- Analog-to-Digital converter (ADC)
- Single-ended, 24-bit 96 kHz stereo with I2S out

Texas Instruments PCM5102APW
- Digital-to-Analog converter (DAC)
- 112dB stereo with 32 bit 384 kHz PCM interface
- 2.1V RMS outputs

## Oscillator
Abracon ACHL-24.576MHZ-EK
- Clock oscillator
- RBP has acknowledged jitter on I2S circuit, so discrete clock is preferred
- 24.576 MHz, 3.3V, +-30 ppm frequency stability

## Signal Connections
THAT1512SO8-U
- Mic preamp
- +-5V supply

THAT1246SO8-U
- Line receiver
- 6-36V supply

Texas Instruments DRV134PA (2x)
- Balanced line drivers
- Converts output to +-6dB line outputs (14.7 dBu max output)

## Power
Traco TMR 0522
- 2W +-12V DC-DC power converter

MCP1700-3.3
- 3.3V voltage regulator for DAC analog supply
- Used to avoid noisy internal RBP 3.3V