# Self-balancing robot — Overbot

Wheel-legged balancing robot. Two gimbal motors drive the wheels with my own FOC code, two servos move the legs so it can crouch, and an MPU-6050 is used for balancing.

> **Status:** Work in progress. This repository contains the v2.0 custom PCB design, schematic and my own FOC + balance code. The robot balances — longest unaided run so far is **356 seconds** on open floor.

[DEMO](https://youtube.com/shorts/mz5cjng6o1k?si=qXF_rx4VsPLrTCHq)

## Parts

| Part | Notes |
|---|---|
| ESP32 NodeMCU-32S | 30 pin |
| 2x SimpleFOC Mini (DRV8313) | needs at least 8 V |
| 2x gimbal motor | 7 pole pairs, ~35 Ω |
| 2x AS5600 | encoders, address 0x36 (can't be changed) |
| MPU-6050 | 0x68 |
| 2x MG90S | leg servos |
| 2x VL53L0X | distance sensors, front one works, up one not used yet |
| passive buzzer | sounds, on GPIO 13 |
| 2S LiPo 2600 mAh | |
| XL6009 boost | 10 V for the drivers |
| Buck converter | 5 V for the ESP32 and servos |
| 2x 220 µF cap | one on each driver, needed or the ESP32 resets |

## Wiring


## Pin map

| Function | Pins |
|---|---|
| M1 phases / EN | 16, 17, 18 / 5 |
| M2 phases / EN | 25, 26, 14 / 27 |
| I2C bus 0 (encoder A, MPU, lasers) | SDA 21, SCL 19 |
| I2C bus 1 (encoder B) | SDA 22, SCL 23 |
| Servos SV1 / SV2 | 33 / 12 |
| Buzzer | 13 |
| Laser XSHUT front / up | 32 / 4 |
| Battery sense | 35 |

## Things to know

### Hardware

- GPIO 5, 12 and 27 need a 10k pulldown to GND. 5 and 12 are strapping pins, and if 12 is high at boot the ESP32 won't boot. Without the pulldown on the EN pins the drivers can turn on by themselves.
- AS5600: the chip has to face the magnet, about 1 mm away and flat. Check the AGC register, around 120-180 is good, 255 means too far. Mine read 49 and 92 when mounted properly.
- Trim the header pins! Two pins touched on a driver (EN and nFAULT), the driver stayed on all the time and it killed an ESP32.
- The 10 V rail is the torque limit. The commutation amplitude is `volts / (SUPPLY * 0.5) * 127`, so with a 10 V supply the sine already clips above 5 V. Going from 4 V to 6 V tripled how long the robot balanced.
- The 5 V rail browns out if both servos move at once, so the code moves one leg at a time.

### Firmware

- **Don't put the buzzer on LEDC.** The ESP32 only has four LEDC timers per speed mode, and the buzzer ended up sharing one with the motor PWM. Playing a tone changed that timer's frequency, so the motors switched at audio frequencies instead of 20 kHz and the electrical noise knocked an encoder off the I2C bus. It looks exactly like a dying encoder. Bit-bang the buzzer with `digitalWrite` instead.
- **Measure your IMU axes, don't assume them.** I had the pitch on the wrong gyro axis for days and spent all of it tuning gains that could never have worked. Hold the robot upright and write down the raw accelerometer values, then tilt it 30° forward and write them down again. On mine: gyro X is pitch (sign inverted), accelerometer Y is vertical, Z is forward, and tilt is `atan2(accZ, accY)`.
- Reading the MPU at 2 kHz on the same bus as an encoder didn't work. 200 Hz is fine.
- The control loop runs at a fixed 1 kHz. Two encoder reads cost about 490 µs, so 2 kHz doesn't fit — and a loop that measures its own DT instead of using a constant produced a limit cycle.
- A velocity-only outer loop isn't enough. It stops the wheels but doesn't care where, so the robot random-walks until it hits something. Adding a position term (remember where you started, lean to come back) cut the wandering by 18x and took the balance time from 22 s to 356 s.
- The VL53L0X needs a real init — hand-rolled register writes return plausible but meaningless numbers. Use the Pololu library, plus the long-range settings (signal rate limit 0.1, VCSEL 18/14, 50 ms timing budget).

### Testing

- **Test on open floor.** The robot wanders, and if it finds a wall or a cable it leans on it and the run time looks great. My "best" runs were mostly the robot resting on something.
- **Run-to-run spread is about 5x.** The same settings gave 31, 63 and 147 seconds back to back. One run tells you nothing about whether a gain change helped.

## Gains that work

| Gain | Value | What it does |
|---|---|---|
| `kp` | 1.00 | tilt error to motor voltage |
| `kd` | 0.020 | damping, on gyro rate |
| `kv` | 0.30 | wheel velocity to lean |
| `kpos` | 0.05 | wheel position to lean (station keeping) |
| `MAX_VOLTS` | 6.0 | output clamp |
| balance point | robot-specific | saved in NVS, re-trim whenever the mechanics move |

Trim the balance point by watching mean wheel **velocity** over a run, not mean error. Velocity near zero means the trim is right.

## Problems I had

**Board v1:**
- only one GND pin was really connected, it worked on USB but not on battery
- broken trace to SV2
- the two I2C buses were merged, so both encoders answered on 0x36 (couldn't fix)
- no caps on the drivers, the ESP32 reset during calibration
- encoder cables crossed and encoder direction inverted (fixed in code)

> **Note:** These v1 issues have all been resolved in the v2 layout included in this repository.

**Board v2:**
- motor 1 IN3 and EN were swapped in my code, so motor 1 only twitched. Took two weeks to find, and because the bug was in code and not copper it survived a whole board revision...
- SV2 is on GPIO 12, not 13
- encoders were mounted the wrong way and too far from the magnet

## Software

I don't use the SimpleFOC library. On my board it didn't output any PWM, so I wrote my own [FOC]() using `ledcWrite`: sine commutation, three phases 120° apart, electrical angle is mechanical angle × 7 pole pairs, torque applied 90° ahead of the rotor. The servos also use `ledcWrite` directly instead of ESP32Servo, so they don't fight with the motors over timers.

The control loop is a fixed 1 kHz tick: read both encoders, read the MPU every 5th tick, inner PD loop on tilt, outer loop on wheel velocity and position, then commutate.

## To do

- [x] pulldowns on 5, 12 and 27
- [x] balance loop
- [x] front laser working
- [ ] battery reading (divider on GPIO 35 doesn't work yet)
- [ ] finish the body
- [ ] obstacle avoidance with the lasers
- [ ] ESP-NOW remote — connect to Kode dot and the Pinetime watch to control the robot
- [ ] make the robot autonomous

## Mechanical

Coming later.

The MG90S servos aren't quite strong enough to lift the robot from crouch to standing, so it does it one leg at a time. Looking at a spring assist for v2.

## License

See [LICENSE](LICENSE.md).
