# Overbot wiring

Everything that connects to the ESP32, for the perfboard build.

Pins are from board v2, checked with a multimeter. Two of them were different from what I first wrote down, so don't trust old notes.

## Power

| From | To |
|---|---|
| Battery + | boost converter in, buck 1 in, buck 2 in, 100k resistor |
| Battery - | common ground |
| Boost out (10 V) | driver 1 motor supply, driver 2 motor supply |
| Buck 1 out (5 V) | ESP32 VIN |
| Buck 2 out (5 V) | servo power only |
| ESP32 3.3 V | encoders, MPU, lasers, driver logic VCC |

Servos get their own buck. On the old board they shared the logic rail and I lost two ESP32s that way.

Caps:
- 220 µF across each driver's motor supply, as close to the driver as possible
- 220 µF across the servo 5 V at the servo connector
- 100 nF next to each one if you have them

## Motor driver 1

| ESP32 | Driver |
|---|---|
| 16 | IN1 |
| 17 | IN2 |
| 18 | IN3 |
| 5 | EN |
| 3.3 V | VCC |
| GND | GND |

Motor phases go to the driver's three outputs, any order. Wrong order just reverses direction and the calibration detects it.

## Motor driver 2

| ESP32 | Driver |
|---|---|
| 25 | IN1 |
| 26 | IN2 |
| 14 | IN3 |
| 27 | EN |
| 3.3 V | VCC |
| GND | GND |

## I2C bus 0

Encoder for motor 1, MPU, and later the lasers.

| ESP32 | Signal |
|---|---|
| 21 | SDA |
| 19 | SCL |

## I2C bus 1

Encoder for motor 2 only. Both encoders are stuck on address 0x36 so they can't share a bus.

| ESP32 | Signal |
|---|---|
| 22 | SDA |
| 23 | SCL |

## Encoders (AS5600)

Each one:

| Encoder pin | To |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | SDA of its bus |
| SCL | SCL of its bus |
| DIR | GND |

Encoder A goes on bus 0, encoder B on bus 1. If they end up swapped it's a one line fix in the code, not a rewire.

## MPU-6050

| MPU pin | To |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | 21 |
| SCL | 19 |
| AD0 | GND (address 0x68) |
| INT | not used |

## Servos

| ESP32 | Servo |
|---|---|
| 33 | SV1 signal |
| 12 | SV2 signal |

Servo + goes to buck 2, servo - to common ground.

## Battery sense

| From | To |
|---|---|
| Battery + | 100k -> GPIO35 |
| GPIO35 | 22k -> GND |

Multiply the reading by 5.545. A 100 nF across the 22k makes it steadier.

## Pulldowns

10k to ground on each of these. They stop the drivers turning on by themselves and stop GPIO12 blocking the boot.

| Pin | Why |
|---|---|
| 5 | driver 1 EN, strapping pin |
| 27 | driver 2 EN |
| 12 | SV2, strapping pin, high at boot = no boot |

## Lasers (later)

| ESP32 | To |
|---|---|
| 21 / 19 | SDA / SCL, bus 0 |
| 32 | XSHUT laser A |
| 4 | XSHUT laser B |

Not checked on the real board yet. One laser gets moved to 0x30 at boot using XSHUT, since both start at 0x29.

## Don't use

| Pins | Why |
|---|---|
| 0, 2, 15 | strapping |
| 1, 3 | serial, needed for the console |
| 34-39 | input only, fine for the battery divider |
| ADC2 pins | unreadable when WiFi or ESP-NOW is on |

## After wiring, before power

- check 3.3 V, 5 V and 10 V are not shorted to ground
- check 5 V and 3.3 V are not shorted to each other
- check bus 0 and bus 1 are not connected (21 to 22, 19 to 23)
- check every joint from the pin itself to the board, wiggling the probe. A cold joint on TX/RX cost me a whole evening
- power the rails with no ESP32 first and check 5 V and 10 V
