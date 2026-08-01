# Periot
IoT personal project

            +-------------------------------------------+
            |     Remote potentiometer (battery)        |
            |                                           |
User     ----> Rotary Encoder --> ATtiny85 --> TX 433   | ~~RF~~>
turns       |             (sleeps almost always)        |
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

