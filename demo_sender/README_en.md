# Demo Sender Sample

`demo_sender.py` is a demo sender script for the protocol, implemented in
plain MicroPython with zero dependency on any specific emulator project.
Use it to try out `hdmi_bridge_receiver` standalone, or to learn how the
protocol is implemented.

## What You Need

- Any Pico/Pico W/Pico 2 running MicroPython (the sender — it doesn't need
  to be the same RP2350 as the receiver)
- A Pico 2 flashed with `hdmi_bridge_receiver` (the receiver, see
  [../doc/build_guide_en.md](../doc/build_guide_en.md))
- The two connected per the wiring in
  [../doc/hardware_guide_en.md](../doc/hardware_guide_en.md)

## Running It

Once wired up, transfer `demo_sender.py` to the sender Pico and run it:

```bash
mpremote connect <port> run demo_sender.py
```

Or, from the REPL:

```python
>>> import demo_sender
>>> demo_sender.main()
```

Stop it with Ctrl-C.

## What the Demo Shows

It cycles through 3 scenes, switching roughly every 4 seconds (40 frames ×
100ms):

1. **Color bars + bezel** — displays 8 color bars scrolling horizontally via
   `PKT_FRAME` (bpp=8 direct color), while overlaying a border via
   `PKT_BEZEL_CMDS`. The bezel and the game screen are sent in the same
   logical coordinate system and scale, so you can see how, even though the
   receiver centers them independently, they end up sharing the same center
   on screen.
2. **Palette stripes + bezel** — sends a 16-color palette via `PKT_PALETTE`,
   then shows diagonal stripes via `PKT_FRAME` (bpp=4, palette-indexed).
3. **Menu-like screen** — shows a UI-style screen combining rect fills and
   text via `PKT_TEXT_CMDS` (with a counter that updates every frame).

## Using This in Your Own Project

The `HDMIBridge` class and the `cmd_rect()`/`cmd_text()` builder functions
in `demo_sender.py` can be copied directly into your own sender code as-is
(the only dependency is `machine.SPI`/`machine.Pin`). See
[../doc/protocol_en.md](../doc/protocol_en.md) for the full packet
specification.

If implementing in C (pico-sdk), see the real-project implementation
examples
([PB-1000_emu_AG2/src/lcd_controller.c](../../PB-1000_emu_AG2/src/lcd_controller.c),
[MSX_emu_pico2/src/msx/msx_core.c](../../MSX_emu_pico2/src/msx/msx_core.c)).
