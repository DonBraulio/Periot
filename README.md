# Periot

Periot is an experimental, low-power physical controller for WiZ lamps. A
battery-powered ATtiny85 reads a rotary encoder and transmits its accumulated
position over 433 MHz OOK. An always-on ESP32-C3 receives the position changes
and translates them into local Wi-Fi/UDP brightness commands for the lamps.

The current firmware includes encoder interrupts, ATtiny power-down sleep, RF
batching and packet recovery, persistent ESP32 state, WiZ discovery, and a
non-blocking Serial console for persistent node-to-lamp pairing.

## System architecture

```text
            +-------------------------------------------+
            |     Remote potentiometer (battery)        |
            |                                           |
User     ----> Rotary Encoder --> ATtiny85 --> TX 433   | ~~RF~~>
turns       |      PCINT + power-down + one-shot WDT    |
            |                                           |
            |           Battery powers ATtiny + TX      |
            +-------------------------------------------+


                 +------------------------+
                 | Central Hub (always ON)|
                 |                        |
                 |  +------------------+  |
RF 433 MHz ----> |  |      ESP32       | ---- WiFi/UDP ----> WiZ Bulbs
                 |  |  + RX 433 MHz    |  |
                 |  +------------------+  |
                 +------------------------+
```

The remote sends a rolling encoder position rather than individual `+1/-1`
events. This allows several detents to share one preamble and lets a later
packet recover movement from an earlier lost packet.

## Design principles

- Keep the battery remote asleep whenever there is no useful work.
- Make the first movement after inactivity feel immediate.
- Limit continuous RF transmission to approximately one frame every 250 ms.
- Preserve movement across lost RF frames using a cumulative position.
- Keep protocol layout and nominal timings in one shared source of truth.
- Keep RF transport separate from lamp and application behavior.
- Identify lamps by stable MAC address and rediscover their current IP at boot.
- Allow one physical controller to operate a small group of lamps.
- Avoid frequent EEPROM/NVS writes.

## Hardware

### ATtiny85 remote

```text
                        ATtiny85 DIP-8

                 +-------------------------+
 RESET / PB5   1 |                         | 8  VCC
 Encoder A PB3 2 |                         | 7  PB2 / ISP SCK
 Encoder B PB4 3 |                         | 6  PB1 / ISP MISO / RF DAT
 GND           4 |                         | 5  PB0 / ISP MOSI / LED
                 +-------------------------+
```

| Signal | Arduino pin | ATtiny85 pin | Physical pin |
| --- | ---: | --- | ---: |
| Encoder A | 3 | PB3 / PCINT3 | 2 |
| Encoder B | 4 | PB4 / PCINT4 | 3 |
| Optional debug LED | 0 | PB0 / MOSI | 5 |
| SYN115 DAT | 1 | PB1 / MISO | 6 |

Connect the encoder common pin to GND. A and B use the ATtiny internal
pull-ups; the encoder itself is not connected to VCC. The push button is not
used yet.

PB0, PB1, and PB2 are also ISP pins. Peripherals on those pins can interfere
with programming; disconnect SYN115 DAT if flash verification becomes
unreliable. The firmware is compiled for 1 MHz, and the physical clock/fuses
must agree with `board_build.f_cpu = 1000000L`.

### ESP32-C3 hub

The PlatformIO target is `esp32-c3-devkitm-1`.

```text
SYN480R                     ESP32-C3-DevKitM-1

VCC  -------------------->  3V3
GND  -------------------->  GND
DAT  -------------------->  IO3 / GPIO3
ANT  -------------------->  approximately 17.3 cm wire
```

Powering the receiver from 3.3 V keeps DAT compatible with the ESP32 GPIO.
Transmitter, receiver, Saleae, and microcontrollers must share their respective
ground reference when connected together for measurements.

## Remote execution model

### Encoder capture

Both encoder phases enable the ATtiny pin-change interrupt group. Every A or B
transition executes `PCINT0_vect`, which:

1. Reads both phases directly from `PINB`.
2. Validates the quadrature transition.
3. Accumulates four valid transitions per detent.
4. Enqueues `+1` or `-1` in a 16-entry ring buffer.

RF transmission and LED feedback never run inside the ISR. The queue preserves
movement that arrives while the main loop is transmitting or blinking. One
slot distinguishes full from empty, so its usable capacity is 15 detents.

An interrupt that occurs during `sendRfFrame()` temporarily pauses the
busy-wait transmitter. The active RF pulse is extended by the ISR duration.
The ISR is intentionally short and the receiver windows tolerate reasonable
jitter, but continuous-turn timing and CRC statistics must be checked on real
hardware.

### Sleep and rate limiting

```text
                         first complete detent
 POWER-DOWN  <---------------------------------------------+
     |                                                     |
     | PCINT                                               |
     v                                                     |
 drain queued steps --> update uint8 position              |
     |                                                     |
     +-- cooldown expired and position changed? -- no -----+ sleep
     |
    yes
     |
     v
 send current position --> arm one-shot WDT --> LED feedback
     |                         ~250 ms
     v
 POWER-DOWN while PCINT continues collecting movement
     |
     | WDT interrupt: disable WDT and expire cooldown
     v
 drain queue --> send latest position only if it changed
```

`SLEEP_MODE_PWR_DOWN` stops the normal timer used by `millis()`, so the cooldown
uses the watchdog oscillator. The watchdog is not periodic:

- It is armed only after a frame is sent.
- It wakes the ATtiny exactly once after approximately 250 ms.
- Its ISR disables it and marks the cooldown as expired.
- If no position changed, the ATtiny immediately returns to power-down and
  remains there until an encoder interrupt.

The first detent after inactivity is sent immediately. Detents during the
cooldown are accumulated and sent together when the watchdog expires. ADC,
analog comparator, and sleep BOD are disabled where supported.

LED feedback is compiled out by default, so it adds no delay or runtime work.
For hardware debugging it can be restored at build time: one flash indicates
positive movement and two indicate negative movement.

```ini
build_flags =
  -D PERIOT_ENABLE_LED_DEBUG=1
```

## Rolling position and boot identity

The remote position is an unsigned 8-bit counter. Increment and decrement wrap
naturally:

```text
positive:  ... 253, 254, 255,   0,   1,   2 ...
negative:  ...   2,   1,   0, 255, 254, 253 ...
```

The hub converts two positions into a signed modular delta:

```text
previous=254, current=2   -> delta=+4
previous=2,   current=254 -> delta=-4
```

This is unambiguous while the net movement between successfully received
frames is less than 128 detents. That is a safe assumption for a human encoder
and a nominal four-frame-per-second limit.

An ATtiny reset starts position at zero. To distinguish that from normal
wrap-around, every frame includes a two-bit `boot_id`. The ATtiny increments
`boot_id` once per boot and stores it at EEPROM address zero. It does not write
EEPROM for encoder movement.

The hub treats a changed `boot_id` as a new position origin and computes the
first delta relative to zero. Correct identification assumes the hub does not
miss four complete ATtiny boot cycles, because `boot_id` wraps modulo four.

### ESP32 persistence

ESP32 does not have a traditional standalone EEPROM. Arduino `Preferences`
stores data in ESP-IDF NVS, a wear-levelled flash-backed key/value store.

The hub persists, per node:

- whether a baseline exists;
- the last `boot_id`;
- the last received position.

It writes NVS once after ten seconds without a position change, rather than on
every frame. On ESP32 restart, the saved baseline prevents old remote movement
from being applied again. Losing hub power during that ten-second interval can
replay the unpersisted delta after restart; this is an accepted prototype
tradeoff and can later be addressed alongside lamp-state persistence.

## WiZ discovery, pairing, and control

The ESP32 runs the following sequence without requiring Serial interaction:

```text
boot
  |
  +--> restore RF positions from NVS
  +--> restore node-to-MAC pairings from NVS
  +--> connect to Wi-Fi
  +--> broadcast WiZ getPilot for 3 seconds
  +--> cache MAC + current IP + state + dimming
  +--> enable the 433 MHz receiver
  |
  +--> normal loop: RF decoder + Serial console + deferred persistence
                         |
                         +--> print help once when a monitor attaches
```

Discovery is intentionally completed before enabling the noisy 433 MHz input,
so its three-second startup window cannot fill the RF edge buffer. Lamp IP
addresses are treated as temporary: each persistent pairing stores only the
normalized 12-digit MAC in NVS, then resolves it against the new boot inventory.
The numeric lamp indices shown over Serial are only shortcuts for that current
inventory and need not remain stable across boots.

Open PlatformIO's bidirectional Serial monitor and send newline-terminated
commands:

```text
lights                         list lamps discovered during this boot
pairs                          list persistent node-to-MAC mappings
stats                          show cumulative RF statistics and timings
pair <node_id> <light_index>   add a lamp to a node
unpair <node_id> <light_index> remove a lamp from a node
clear <node_id>                remove every lamp from a node
help                           print the command reference
```

The ESP32-C3 USB CDC connection state lets the firmware detect a newly opened
monitor and print this help once per attachment. Pressing Enter on an empty line
also prints it. There is no periodic RF output; use `stats` when diagnostics are
needed.

For example, after `lights` reports `[0]` and `[1]`, these commands make remote
node 1 control both lamps as a group:

```text
pair 1 0
pair 1 1
pairs
```

Each node supports up to four lamps. A lamp may be paired with more than one
node. Pairing changes are written immediately because they are rare user
configuration events. `clear` also works when a previously paired lamp is
offline; individual `unpair` commands use the current discovered-lamp index.
Reboot the ESP32 to repeat discovery after adding or powering on a lamp.

Every received encoder detent changes brightness by five percentage points,
including batched deltas larger than one. Logical level zero sends `state=false`
and turns the lamp off. WiZ's positive dimming range starts at 10, so one
positive detent from zero turns the lamp on at 10 and one negative detent from
10 turns it off. Positive levels send only `state=true` and `dimming`, so they
do not force a color, scene, or color temperature. The discovery value is the
initial brightness baseline; later commands update the cached value
optimistically because this prototype does not wait for a WiZ acknowledgement.

## RF wire protocol

All nominal wire constants and the frame pack/unpack code live in:

```text
common/RfProtocol/src/rf_protocol_spec.h
```

Both PlatformIO projects consume that local library, so transmitter and
receiver cannot silently drift to different payload layouts or checksums.
Protocol 0.2 changes the payload from relative direction to rolling position;
ATtiny and ESP32 must therefore be flashed together when upgrading from 0.1.

### Waveform

```text
Idle       LOW
Preamble   16 x [MARK 400 us + ZERO_SPACE 400 us]
Sync       MARK 400 us + SYNC_SPACE 3000 us
Bit 0      MARK 400 us + ZERO_SPACE 400 us
Bit 1      MARK 400 us + ONE_SPACE 1200 us
Stop       MARK 400 us, then idle LOW
```

The stop mark supplies the rising edge that closes the final payload space at
the receiver. Without it, an edge-duration decoder cannot finish the last bit.

### Payload

Payload bits are transmitted most-significant bit first:

```text
17            14 13       12 11                   4 3       0
+----------------+-----------+----------------------+---------+
| node_id (4 bit)| boot_id(2)| position (8 bit)     | CRC-4   |
+----------------+-----------+----------------------+---------+
```

- `node_id`: identifies one of up to 16 remotes.
- `boot_id`: two-bit EEPROM-backed boot generation.
- `position`: rolling modulo-256 encoder position.
- `CRC-4`: CRC-4/ITU using polynomial `x^4 + x + 1`.

The 18-bit frame takes nominally 31.0 ms when all payload bits are zero. Every
one bit adds 0.8 ms, giving a 31.0–45.4 ms theoretical range. Batching saves
energy because several detents share one preamble despite the larger payload.

### Receiver state machine

The ESP32 ISR records each completed pulse duration and normalized MARK/SPACE
level in a ring buffer. Parsing occurs in `loop()`, never in the ISR:

```text
WaitingPreamble
       |
       | >= 12 valid MARK + ZERO_SPACE cycles
       v
WaitingSyncSpace
       |
       | valid SYNC_SPACE
       v
ReadingBitMark <----+
       |             |
       v             |
ReadingBitSpace -----+  until 18 bits
       |
       v
CRC check --> RfFrame --> modular position tracker
```

The transmitter sends 16 preamble cycles while the receiver requires 12, which
tolerates loss during receiver AGC/threshold stabilization.

Initial receiver windows:

| Element | Transmitted | Initially accepted |
| --- | ---: | ---: |
| Mark | 400 us | 250–700 us |
| Zero space | 400 us | 250–700 us |
| One space | 1200 us | 900–1600 us |
| Sync space | 3000 us | 2200–4200 us |

The receiver module can invert its digital output. If Saleae shows the 3000 us
transmitted LOW sync as HIGH on receiver DAT, set
`RECEIVER_OUTPUT_INVERTED = true` in the ESP32 RF module.

## Source layout

```text
common/RfProtocol/
  src/rf_protocol_spec.h       Shared timings, frame, CRC, modular math

ATMEL Tiny85/TinyDimmer/src/
  main.cpp                     PCINT, queue, sleep, WDT, EEPROM, optional LED
  rf_protocol.h/.cpp           Physical OOK frame transmission

PeriotESP32/src/
  app_config.h                 Wi-Fi, RF pin, and dimming configuration
  main.cpp                     Startup and application event flow
  rf_protocol.h/.cpp           Edge capture and waveform decoder
  rf_position_tracker.h/.cpp   Modular deltas and deferred NVS persistence
  serial_console.h/.cpp        Non-blocking pairing command parser
  wifi_connection.h/.cpp       Station-mode Wi-Fi connection
  wiz_control.h/.cpp           WiZ discovery and brightness-only UDP control
  wiz_lamp_controller.h/.cpp   RF delta to paired-lamp behavior
  wiz_pairing.h/.cpp           Persistent node-to-MAC mappings
```

## Build, upload, and monitor

Install PlatformIO, then build the ATtiny85 firmware:

```sh
cd "ATMEL Tiny85/TinyDimmer"
pio run
pio run --target upload
```

The upload environment expects an Arduino-as-ISP using `stk500v1`. Update
`upload_port` in `platformio.ini` for the connected adapter.

Build and monitor the ESP32-C3:

```sh
cd PeriotESP32
pio run
pio run --target upload
pio device monitor
```

The configured ESP32 upload/monitor port is local-machine-specific and may need
to be changed after reconnecting USB. List candidates with:

```sh
pio device list
```

## Validation checklist

1. With the encoder idle, confirm the ATtiny LED remains off and current drops
   to its power-down level.
2. Turn one detent after inactivity; one frame should arrive immediately.
3. Turn several detents quickly; subsequent frames should be limited to roughly
   one every 250 ms and carry position jumps larger than one.
4. Confirm positive and negative wrap-around, for example `255 -> 0` and
   `0 -> 255`.
5. Reset the ATtiny and confirm `new_boot=yes` with the next `boot_id`.
6. Restart the ESP32 after NVS persistence and confirm it restores the previous
   baseline instead of replaying movement.
7. Run `lights`, pair a discovered index with the transmitting `node_id`, then
   confirm `pairs` shows the MAC mapping.
8. Turn in both directions and confirm brightness changes in five-point steps,
   reaches OFF below 10, and returns to 10 on the next positive detent. Also
   test a rapid turn whose RF frame has a delta larger than one.
9. Reboot the ESP32 and confirm the pairing remains while the lamp IP is
   rediscovered.
10. Run `stats` after turning continuously; inspect RF timings and ensure CRC
   failures and incomplete frames remain negligible.
11. `buffer_overflows` may increase because the SYN480R is noisy while idle, but
   valid frames must not cause false actions.

Expected serial output resembles:

```text
RF frame: node=1 boot_id=2 position=17 delta=+4 new_boot=no payload=0b...
```

The `stats` command reports decoded frames, CRC failures, sync failures, invalid
noise pulses, incomplete frames, buffer overflows, and accepted timing
distributions.

## Current decisions and known tradeoffs

- One RF frame per position update; no retransmission or acknowledgement.
- First movement is immediate; continuous movement is batched at ~250 ms.
- Rolling position recovers movement after dropped intermediate frames.
- Eight-bit position assumes fewer than 128 net missed detents.
- Two-bit boot identity assumes fewer than four unseen remote boots.
- CRC-4 improves corruption detection but is not a cryptographic integrity
  mechanism.
- LED feedback is disabled by default and available only through a debug build
  flag.
- WiZ discovery runs only at boot; changing the available lamp set requires an
  ESP32 restart.
- Wi-Fi reconnect and WiZ command acknowledgements are not implemented yet.
- Pairing is persistent by MAC, while lamp IP, power, and brightness are cached
  from the current boot discovery.
- Internal encoder pull-up current must be measured in every stable detent
  state; an encoder contact held LOW can dominate sleep consumption.
- PCINT activity can stretch software-generated RF pulses and requires hardware
  validation under continuous rotation.
- WiZ integration must remain non-blocking enough to keep draining RF edges, or
  the receiver should later migrate to ESP32 RMT hardware.
- Micronucleus is not used; ATtiny programming remains ISP-based.
