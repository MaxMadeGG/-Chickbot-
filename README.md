# Self-balancing robot- Overbot

Wheel-legged balancing robot. Two gimbal motors drive the wheels with my own FOC code, two servos move the legs so it can crouch, and an MPU-6050 is used for balancing.
> **Status:** Work in progress. This repository currently contains the v2.0 custom PCB design, schematic and ownFOC code.
[DEMO](https://youtube.com/shorts/aNbwgdx8i7I?si=RKKSs-6FwYw2KxhV)
## Parts

| Part | Notes |
|---|---|
| ESP32 NodeMCU-32S | 30 pin |
| 2x SimpleFOC Mini (DRV8313) | needs at least 8 V |
| 2x gimbal motor | 7 pole pairs, ~35 Ω |
| 2x AS5600 | encoders, address 0x36 (can't be changed) |
| MPU-6050 | 0x68 |
| 2x MG90S | leg servos |
| 2x VL53L0X | distance sensors, not tested yet on the v2.0 |
| 2S LiPo 2600 mAh | |
| XL6009 boost | 10 V for the drivers |
| Buck converter | 5 V for the ESP32 and servos |
| 2x 220 µF cap | one on each driver, needed or the ESP32 resets |

## Schematic and PCB
<img width="2160" height="1925" alt="image" src="https://github.com/user-attachments/assets/1bbf493b-a859-4a68-800b-6bd59aa50618" />

The full schematic and PCB files are in the repo. The encoders both use address 0x36, so each one is on its own I2C bus.
It was made in easyEDA.
[gerber](self-balancing-robot-cat-gerber-v2.0.zip)
[schematic](schematic.epro2)
[PCB](PCB.epro2)

## Things to know

- GPIO 5, 12 and 27 need a 10k pulldown to GND. 5 and 12 are strapping pins, and if 12 is high at boot the ESP32 won't boot. Without the pulldown on the EN pins the drivers can turn on by themselves.
- AS5600: the chip has to face the magnet, about 1 mm away and flat. Check the AGC register, around 120-180 is good, 255 means too far.
- Reading the MPU at 2 kHz on the same bus as an encoder didn't work. 200 Hz is fine.
- Trim the header pins! Two pins touched on a driver (EN and nFAULT), the driver stayed on all the time and it killed an ESP32.

## Problems I had

Board v1:
- only one GND pin was really connected, it worked on USB but not on battery
- broken trace to SV2
- the two I2C buses were merged, so both encoders answered on 0x36 (couldn't fix)
- no caps on the drivers, the ESP32 reset during calibration
- encoder cables crossed and encoder direction inverted (fixed in code)
> **Note:** These v1 issues have all been resolved in the v2 layout included in this repository.

Board v2:
[gerber](self-balancing-robot-cat-gerber-v2.0.zip)
[schematic](schematic.epro2)
[PCB](PCB.epro2)
- motor 1 IN3 and EN were swapped in my code, so motor 1 only twitched. Took a while to find...
- SV2 is on GPIO12, not 13
- encoders were mounted the wrong way and too far from the magnet

## Software

I don't use the SimpleFOC library. On my board it didn't output any PWM, so I wrote my own [FOC](Quad-code-ownFOC.zip) using ledcWrite. The servos also use ledcWrite directly instead of ESP32Servo, so they don't fight with the motors over timers.

The control loop runs at a fixed 2 kHz. More about the code later.

## To do

- [ ] pulldowns on 5, 12 and 27
- [ ] test the lasers and battery reading
- [ ] finish the body
- [ ] balance loop
- [ ] ESP-NOW remote - connect to Kode dot and the Pinetime watch to control the robot by yourself
- [ ] make the robot autonomous

## Mechanical

Coming later...
You can still make your own robot with this working PCB, you will just need to tune the code (me i can't due to the ESP32 that broke)

## License

See [LICENSE](LICENSE.md).
