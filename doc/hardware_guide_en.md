# Hardware Guide

## What You Need

| Part | Requirement |
|---|---|
| Receiver MCU | Raspberry Pi Pico 2 (RP2350). **RP2040 (plain Pico/Pico W) will not work** — the HSTX peripheral is RP2350-specific |
| HDMI output addon | A board that wires RP2350's HSTX output (GP12-19) to an HDMI connector |
| Sender MCU | Anything (MicroPython or pico-sdk, as long as it can send as an SPI master) |
| HDMI cable + monitor | Anything that supports 640x480@60Hz (virtually all monitors handle this) |

### About the HDMI output addon

Not tied to any specific product — anything following the "RP2350's HSTX
output wired out to HDMI on the standard GP12-19 pins" convention will work.
Verified on real hardware:

- **PICO-HDMI-PLUS** (Micro Fun)
- **Pico-DVI-Sock**-compatible wiring

Both are designed to be soldered/stacked directly onto the back of a Pico 2,
connecting GP12-19 to the HDMI connector's TMDS signal lines. Other products
should work as long as they use the same GP12-19 pin assignment, but this is
unverified.

## GPIO Wiring

### SPI0 slave (receiving from the sender)

| Receiver Pico 2 | Function | Sender |
|---|---|---|
| GP0 | SPI0 RX | Sender's MOSI |
| GP1 | SPI0 CSn | Sender's dedicated CS pin (recommend a new one, e.g. GP28) |
| GP2 | SPI0 SCK | Sender's SCK |
| GP3 | (unused) | No wiring needed — this is a receive-only link |
| GND | Common GND | Sender's GND |

If the sender's SPI bus is shared with other uses (LCD/SD card, etc.), we
recommend adding a new, dedicated CS pin for this link (so it doesn't
collide with existing uses). In the example projects, the existing SPI1
(GP10=SCK, GP11=MOSI) is shared while a newly added GP28 serves as the
dedicated CS.

### HSTX output (to the HDMI addon)

| Receiver Pico 2 | Signal |
|---|---|
| GP12 | D0+ |
| GP13 | D0- |
| GP14 | CK+ (clock) |
| GP15 | CK- |
| GP16 | D2+ |
| GP17 | D2- |
| GP18 | D1+ |
| GP19 | D1- |

This assumes the HDMI addon board's own standard wiring matches. No wiring
work is needed for boards that stack directly onto the Pico 2.

## Real-Hardware Caveats

- **SPI mode 3 is required**: RP2350's PL022 SPI slave implementation has a
  known constraint where mode 0 (CPOL=0/CPHA=0) loses sync after a few bytes
  (multiple reports on the official Raspberry Pi forums). The sender must
  always reconfigure to mode 3 (CPOL=1/CPHA=1) immediately before use —
  especially important if the SPI bus is shared with other uses (e.g. an
  LCD).
- **Pull-up on the CS pin**: the receiver applies a weak pull-up
  (`gpio_pull_up()`) to GP1 (CSn), as insurance against the CS line
  momentarily floating during the sender's GPIO initialization causing
  misbehavior.
- **Signal verification procedure**: after wiring, it's easier to isolate
  problems by first checking HSTX alone with the `ENABLE_SPI_RX=0`
  diagnostic build (see [build_guide_en.md](build_guide_en.md)), then
  verifying the normal build with SPI receive included.
- **Visual confirmation marker**: a 16x16-pixel marker in the top-left of
  the screen alternates red/green every time a packet finishes being
  received. If it never changes, SPI reception itself isn't happening
  (suspect wiring or a mode mismatch). It's blue right after boot, before
  anything has been received.

## Minimum Requirements for a Sender Implementation

1. Initialize any SPI peripheral on the sender side as a master.
2. Initialize one dedicated CS pin as output, idle-high.
3. Reconfigure to mode 3 with an arbitrary baud rate immediately before every
   send (if sharing the bus with other uses).
4. Send an 8-byte header + payload in a single CS-low interval, following
   [protocol_en.md](protocol_en.md).

For concrete code, see the MicroPython sample in
[../demo_sender/](../demo_sender/), or real-project implementation examples
([PB-1000_emu_AG2](../../PB-1000_emu_AG2/src/lcd_controller.c),
[MSX_emu_pico2](../../MSX_emu_pico2/src/msx/msx_core.c)).
