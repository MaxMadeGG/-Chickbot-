# Chickbot v2.0 — issues and fixes

Oct 26, 2026 · @Marc

Everything that went wrong across board v1, board v2 and the perfboard build, with what to change next time. All of it found by running the robot, not by guessing.

## Board and wiring

Two ESP32s died and two weeks went into one firmware typo. Most of these are layout and assembly problems a proper board removes.

| Problem | What happened | Fix in v2.0 |
| --- | --- | --- |
| Untrimmed driver header pins | EN and nFAULT touched on a SimpleFOC Mini. The driver stayed enabled permanently and killed an ESP32. | Trim pins, widen pad spacing so neighbours cannot bridge, or solder the drivers down instead of socketing them. |
| Single working ground (v1) | Only one GND pin was actually connected. Worked on USB, failed on battery. | Ground pour on both layers, stitching vias, every GND pin connected. |
| Broken trace to SV2 (v1) | Servo 2 did not respond at all. | Continuity-test every net before populating the board. |
| I2C buses merged (v1) | Both AS5600s answered on 0x36. Not fixable without a rework. | Keep the two buses physically separate end to end. |
| No bulk caps on the drivers (v1) | The ESP32 reset during motor calibration. | 220 uF per driver, plus a 100 nF ceramic right at each driver's VM pin. |
| Encoder cables crossed | Encoder direction came out inverted, patched in firmware. | Keyed connectors, labelled on the silkscreen. |
| Perfboard cold joints and bridges | SDA and SCL shorted together on encoder A; the module had to be replaced. | A real PCB removes this whole class of fault. |
| M1 IN3 and EN swapped in firmware | Motor 1 only twitched for two weeks. The fault was in code, so it survived a board revision untouched. | Print the real pin map on the silkscreen so firmware can be checked against copper. |
| Strapping pins floating | GPIO 5, 12 and 27 need 10k pulldowns. Without them the driver EN lines can come up on their own, and GPIO 12 high at boot stops the ESP32 booting. | Pulldowns on the board, not soldered on by hand afterwards. |

## Power

The 10 V rail is the real torque ceiling. With 35 ohm windings the motors stayed barely warm after six minutes of balancing at 6 V, so there is thermal room but no voltage room.

| Problem | What happened | Fix in v2.0 |
| --- | --- | --- |
| Sine clips above 5 V | The commutation amplitude is `volts / (SUPPLY * 0.5) * 127`, so with SUPPLY = 10 V, full modulation is reached at 5.0 V. Running at 6 V is already overmodulating, and 4 V to 6 V tripled balance time because it was the binding limit. | Raise the boost output and set SUPPLY to match. Check the DRV8313's rating first. |
| 5 V rail browns out | Moving both servos at once dropped the rail. Worked around in firmware by moving one leg at a time. | Bigger buck, plus bulk capacitance on the servo rail so a stall does not reach the ESP32. |
| Battery sense never worked | The divider on GPIO35 reads nothing useful. Never diagnosed, because it needs the shell open. | Re-check the divider values against the ADC range and verify on the bench before the shell closes. |
| Separate servo buck | Added after two board deaths on the theory that servo back-EMF on a shared rail was the cause. That was never proven: board 1 died from the EN/nFAULT short, board 2 is still unexplained. | Keep it. Good practice on its own merits, just not a proven fix for anything. |

## Sensors

The worst bug of the whole build looked like a failing encoder and was actually an LEDC timer: the buzzer and the motor PWM both use 8-bit resolution, the ESP32 has only four LEDC timers per speed mode, and `ledcWriteTone` on a shared timer drags the motor switching frequency down with it. Motors switching at audio frequencies instead of 20 kHz made enough noise to knock encoder B off the bus. Hours went into chasing the encoder before the buzzer was suspected.

| Problem | What happened | Fix in v2.0 |
| --- | --- | --- |
| Buzzer on LEDC | Shared a timer with the motor phases. Encoder B failed 200 times in a row within seconds of arming; the encoder itself tested clean at 51,200 reads. | Bit-bang the buzzer, or give it a hardware timer that nothing else can touch. Worth a note on the board so it is not repeated. |
| AS5600 address is fixed at 0x36 | Two encoders cannot share a bus, so each needs its own. | Keep two buses, or put a TCA9548A mux on one and free a peripheral. |
| Encoder cable routing | M2 ran slightly noisier than M1 all build. Not the real fault, but the asymmetry was real. | Route encoder cables away from the motor phase wires, twist SDA and SCL with a ground, 100 nF at each AS5600. |
| Magnet distance window is narrow | Roughly 0.5 to 3 mm. AGC 49 and 92 measured good; 255 means too far, and MD must read 1. | Design the magnet standoff into the part rather than shimming it by hand. |
| MPU on a shared bus | Reading it at 2 kHz alongside an encoder did not work. Dropped to 200 Hz. | Give the IMU its own bus, or keep 200 Hz and budget the tick. Two encoder reads already cost about 490 us of a 1000 us tick. |
| VL53L0X addressing | Both come up on 0x29, so one has to be held in reset via XSHUT and readdressed before the other is released. | Keep both XSHUT lines broken out. |
| VL53L0X initialisation | Hand-rolled register writes returned plausible but meaningless numbers. The Pololu library's full init is needed, plus long-range settings: signal rate limit 0.1, VCSEL 18/14, 50 ms timing budget. | Firmware note, not a board change. |
| VL53L0X mounting | The beam spreads about 25 degrees. A recessed sensor behind a narrow hole reflects off the walls and reads a constant short distance. | Sensor face flush with the outer surface, hole flared outward, no window material. |

## Mechanical

The leg joint is the one that costs the most time, because it moves the centre of mass. The balance point had to be retrimmed from -0.2 to -1.2 to -3.2 in a single day, which looked like the controller losing tune and was actually a joint working loose.

| Problem | What happened | Fix in v2.0 |
| --- | --- | --- |
| Leg joint fails | Glued twice, came apart twice. Each time the centre of mass moved and the balance point had to be found again. | A screw or pin through the joint, not glue. Deeper socket, or print the leg and mount as one piece. |
| MG90S too weak | Cannot lift the robot from crouch to standing. It manages by moving one leg at a time, which works but is a workaround. | A stronger servo in the same footprint, a linkage with more mechanical advantage, or a spring to assist the lift. |
| Body is not flat | The linkage sits proud, so the PCB and IMU mounting assumptions had to bend around it. | Design a flat mounting face for the board, with the IMU plane defined by the part rather than by whatever fits. |
| Servo rail current | Both servos moving together browns out 5 V, so the firmware staggers them. | Either a rail that can take both, or keep the stagger and accept it. |

The wheels, the printed rims and the TPU tyres gave no trouble at all and can carry over unchanged.

## Settings to carry over

These were all measured on the robot, several of them painfully. None should be rediscovered.

| Setting | Value | Why |
| --- | --- | --- |
| Pitch axis | Gyro X (index 0), sign -1 | Measured. Getting this wrong made tilt swing +/-10 degrees at rest and wasted days of tuning. |
| Tilt formula | `atan2(accZ, accY)` — forward over vertical | Upright reads about 0, nose-down goes negative. |
| Accelerometer axes | Z (index 2) forward, Y (index 1) vertical | Measured by tilting the robot 30 degrees and reading the raw values. |
| Motor signs | `{ -1.0, +1.0 }` | Mirror-mounted motors. |
| Pole pairs | 7 | Confirmed by the calibration sweep: 20 electrical revolutions at 8 V. |
| Control loop | 1 kHz, IMU every 5th tick (200 Hz) | Two encoder reads cost about 490 us, so 2 kHz did not fit and a measured DT produced a limit cycle. Keep DT constant. |
| Complementary filter | 0.99 gyro / 0.01 accelerometer | At 0.95/0.05 the robot's own acceleration corrupted the tilt estimate. |
| Gains | kp 1.00, kd 0.020, kv 0.30, kpos 0.05, MAX\_VOLTS 6.0 | Six minutes of unaided balancing on open floor. |
| Balance point | Robot-specific, stored in NVS under `bal2` | Moves whenever the mechanics move. Trim by `mean vel`, not `mean err`. |
| Servo endpoints | SV1 stand 2300 / crouch 2050, SV2 stand 701 / crouch 950 | Measured with the range-finder sketch. |

### Pin map

| Function | Pins |
| --- | --- |
| M1 phases / EN | 16, 17, 18 / 5 |
| M2 phases / EN | 25, 26, 14 / 27 |
| I2C bus 0 (encoder A, MPU, lasers) | SDA 21, SCL 19 |
| I2C bus 1 (encoder B) | SDA 22, SCL 23 |
| Servos SV1 / SV2 | 33 / 12 |
| Buzzer | 13 |
| Laser XSHUT front / up | 32 / 4 |
| Battery sense | 35 |

### Two lessons about testing

**Run-to-run spread is about 5x.** The same configuration gave 31, 63 and 147 seconds on three consecutive runs. A single run proves nothing about a gain change, and several tuning decisions were made on noise before this was measured.

**Test on open floor.** The robot wanders, and a wall or a cable it leans against extends a run enormously. Time-until-fall measured how long it took to find support, not how well it balanced, until the testing moved to a cleared space.
