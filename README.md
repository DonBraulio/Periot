# Periot

Periot is an experimental low-power physical controller for WiZ lamps. A
battery-powered ATtiny85 reads a rotary encoder and sends relative brightness
events over 433 MHz OOK. An always-on ESP32-C3 receives those events and will
eventually translate them to Wi-Fi/UDP commands for the lamps.

The current milestone only validates the RF link. WiZ control and ATtiny sleep
are deliberately not integrated yet.

## Architecture

```text
Rotary encoder -> ATtiny85 -> SYN115 transmitter ))) 433 MHz ((( SYN480R -> ESP32-C3
```

Each encoder detent produces exactly one RF frame. Frames carry a relative
direction rather than an absolute brightness value. The ESP32 will own lamp
state in a later milestone.

## Hardware and pinout

### ATtiny85 remote

| Signal | Arduino pin | ATtiny85 pin | Physical pin |
| --- | ---: | --- | ---: |
| Encoder A | 3 | PB3 | 2 |
| Encoder B | 4 | PB4 | 3 |
| LED | 0 | PB0/MOSI | 5 |
| SYN115 DAT | 1 | PB1/MISO | 6 |

Connect the encoder common pin to GND; A and B use the internal pull-ups. PB1
is also used by ISP, so disconnecting the transmitter DAT wire may be necessary
while programming. The firmware clock must remain at 1 MHz.

### ESP32-C3 hub

| Signal | GPIO |
| --- | ---: |
| SYN480R DAT | 3 |

Power the receiver from 3.3 V so its DAT output is safe for the ESP32 input.
Both RF modules benefit from an approximately 17.3 cm antenna for initial tests.

## RF protocol

All timings below are nominal transmitter timings:

- Preamble: 16 cycles of 400 us HIGH + 400 us LOW.
- Sync: 400 us HIGH + 3000 us LOW.
- Payload: 10 bits, most-significant bit first.
- Bit 0: 400 us HIGH + 400 us LOW.
- Bit 1: 400 us HIGH + 1200 us LOW.

Payload layout:

```text
9          6 5 4       3 2          0
+------------+-+---------+------------+
| node_id    |D| sequence| checksum   |
+------------+-+---------+------------+
   4 bits    1    2 bits     3 bits
```

`D=1` means `+1`; `D=0` means `-1`. The checksum is:

```text
(node_id ^ (direction << 1) ^ (sequence << 2) ^ 0b101) & 0x07
```

The receiver accepts at least 12 complete preamble cycles to tolerate startup
loss while still requiring a strong noise discriminator. Timing windows are
constants near the top of `PeriotESP32/src/rf_protocol.cpp` and are intended to be
tuned from measurements.

## Firmware structure

Both firmware entry points only coordinate their local hardware and the RF
module:

- `ATMEL Tiny85/TinyDimmer/src/rf_protocol.*` creates and transmits frames.
- `PeriotESP32/src/rf_protocol.*` captures edges, decodes frames, and reports
  receiver diagnostics.
- Each `main.cpp` owns application concerns such as encoder handling or serial
  presentation.

## Build and upload

Install [PlatformIO](https://platformio.org/), then run:

```sh
cd "ATMEL Tiny85/TinyDimmer"
pio run
pio run --target upload
```

The ATtiny upload configuration expects an Arduino-as-ISP compatible with
`stk500v1`. Update `upload_port` in `platformio.ini` for the connected adapter.

For the ESP32-C3:

```sh
cd PeriotESP32
pio run
pio run --target upload
pio device monitor
```

Change `RF_RX_PIN` before building if DAT is wired to a different GPIO.

## RF validation

Turning the encoder should immediately produce output similar to:

```text
RF frame: node=1 direction=+1 sequence=2 payload=0b0001110110
```

The hub also prints counters every five seconds. Noise may increase invalid
pulse counts, but must not produce valid frames. `buffer_overflows` should stay
at zero. Valid-frame min/average/max timings are reported separately for HIGH,
short LOW, long LOW, and sync LOW pulses. Compare these with a Saleae capture
before tightening the windows.

## Current decisions

- One frame per detent; no retransmission or acknowledgement.
- Relative direction events; the hub will own brightness state.
- The original blocking LED feedback is retained to keep this protocol change
  focused; it should become non-blocking before optimizing fast encoder input.
- No Micronucleus, sleep mode, or WiZ command integration in this milestone.
