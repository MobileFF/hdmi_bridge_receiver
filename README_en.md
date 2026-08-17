# hdmi_bridge_receiver

A generic HDMI output bridge receiver firmware. Flash it onto a second
Raspberry Pi Pico 2 (RP2350); it receives compact frame data sent over SPI
from a main-unit microcontroller (another Pico/Pico W, etc.) and outputs a
DVI/HDMI-compatible signal via the RP2350's HSTX peripheral.

Because the sender-side protocol is fully self-describing, **this firmware
itself is not specific to any particular host emulator or project**. It is
currently shared, unmodified, by two projects — [PB-1000_emu_AG2](../PB-1000_emu_AG2/)
(a Casio PB-1000 pocket computer emulator) and [MSX_emu_pico2](../MSX_emu_pico2/)
(an MSX1 emulator) — simply by reflashing the sender side.

## Documentation

| Document | Contents |
|---|---|
| [doc/protocol_en.md](doc/protocol_en.md) | **Full protocol specification** (byte-level definition of every packet type, with worked examples) |
| [doc/hardware_guide_en.md](doc/hardware_guide_en.md) | Required hardware, wiring, real-hardware caveats |
| [doc/build_guide_en.md](doc/build_guide_en.md) | Build, flash, troubleshooting |
| [doc/architecture_en.md](doc/architecture_en.md) | Internal design notes (Core0/Core1 split, watchdog, etc.) |
| [demo_sender/](demo_sender/) | Generic MicroPython sender sample (for testing/learning the protocol) |

*(For the Japanese version, see the files without the `_en` suffix.)*

## Quick Start

1. Get a Raspberry Pi Pico 2 (RP2350) + an HDMI output addon (e.g.
   PICO-HDMI-PLUS — see [doc/hardware_guide_en.md](doc/hardware_guide_en.md)).
2. Get the firmware — two ways:
   - **Use the prebuilt binary**: flash [`firmware/hdmi_bridge_receiver.uf2`](firmware/)
     directly via BOOTSEL.
   - **Build it yourself**: `./build.sh` (below) produces
     `firmware/hdmi_bridge_receiver.uf2`. Use this if you've changed the source.
3. Run [demo_sender/](demo_sender/) on another Pico to verify it works, or
   enable the `[hdmi]` setting in an existing sender project
   (PB-1000_emu_AG2/MSX_emu_pico2).

```bash
./build.sh
```

## Known Limitations

- **Fixed 640x480@60Hz output**. No other resolutions or refresh rates are
  supported.
- **Video only, no audio**. There is no audio output over the HDMI cable.
- **One-way link, no ACK**. The protocol is strictly sender-to-receiver; the
  sender has no way to confirm the receiver is actually receiving (or even
  present) — visual confirmation (e.g. via [demo_sender/](demo_sender/)) is
  the only way.
- **One-to-one only**. Designed for one sender Pico talking to one receiver
  Pico 2. Fan-out to multiple receivers is not supported.
- **Payload/canvas size limits apply**. `PKT_FRAME`'s width/height cap out at
  256x192; command-stream (`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`) payloads cap out
  at 49,152 bytes (see "Implementation Constants" in
  [doc/protocol_en.md](doc/protocol_en.md)).
- **Receiver is RP2350-only**. RP2040 (plain Raspberry Pi Pico/Pico W) lacks
  the HSTX peripheral and cannot run this firmware. The sender side can be
  any microcontroller.

## Background

This firmware was originally developed inside the MSX_emu_pico2 project as
`hdmi_bridge/phase2_receiver/` (see `MSX_emu_pico2/doc/hdmi_bridge_phase2_report.md`
for how it was validated on real hardware, and `MSX_emu_pico2/hdmi_bridge/README.md`
for the earlier feasibility work), then later ported and generalized for
PB-1000_emu_AG2. Once the protocol became fully self-describing it was no
longer specific to either project, so it was split out into this shared,
independent project.

## License

MIT License — see [LICENSE](LICENSE). The font data (`font_petme128_8x8.h`)
and part of the HSTX/DVI output code incorporate third-party code, each
under its own retained license notice (see the LICENSE file for details).
