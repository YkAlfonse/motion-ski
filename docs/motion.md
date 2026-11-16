# Motion input

The player steers with a chest-mounted IMU: lean left or right to change lanes, jump over rocks and bend forward to duck under branches. This covers the sensor, the driver, calibration, gesture detection, the tools and the results so far.

Code: `hal/imu.c`, `app/gesture.c`, `tests/gesture_test.c`, `tools/imu_log.c`, `tools/plot_imu.py`, `tools/gesture_replay.c`.

## Hardware

LSM6DSOX accelerometer + gyroscope on I²C bus 1 (`/dev/i2c-1`) of the BeagleY-AI at address 0x6A (0x6B also works).

| IMU | BeagleY-AI header |
| --- | --- |
| VIN (3.3 V) | pin 1 |
| GND | pin 6 |
| SDA | pin 3 |
| SCL | pin 5 |

For the final setup the IMU sits on the player's chest and the board stays at the TV. Two SparkFun QwiicBus EndPoints (PCA9615) carry I²C over a 10 ft Cat6 cable as a differential signal, so the wire length doesn't break the bus:

```
BeagleY-AI header -> EndPoint -> Cat6 -> EndPoint -> IMU on chest strap
```

The IMU can be mounted at any angle since calibration learns its orientation. It just has to be held firmly so it moves with the chest.

## Driver

`hal/imu.c` talks to the chip directly with the Linux `I2C_RDWR` ioctl (register address write, then a repeated start and read).

| Register | Value | Meaning |
| --- | --- | --- |
| WHO_AM_I (0x0F) | 0x6C | checked at both addresses to find the chip |
| CTRL3_C (0x12) | 0x01 then 0x44 | software reset, then block data update + address auto increment |
| CTRL1_XL (0x10) | 0x48 | accel 104 Hz, ±4 g |
| CTRL2_G (0x11) | 0x44 | gyro 104 Hz, 500 dps |
| STATUS (0x1E) | XLDA, GDA bits | new accel and gyro data ready |
| OUTX_L_G (0x22) | 12 bytes | gyro x,y,z then accel x,y,z, little endian |

- After the reset it reads CTRL1/CTRL2 back to make sure the writes went through, then waits 80 ms for the gyro to start.
- `imu_read()` polls STATUS until both data-ready bits are set (up to 20 ms), so the same sample is never returned twice.
- Scaling: 0.122 mg/LSB and 17.5 mdps/LSB, from the datasheet for these ranges.

Board test (`sudo ./imu_log -t 10`, board still on a desk):

```
imu: LSM6DSOX at 0x6A
1036 samples in 10.0 s (103.5 Hz), 0 failed reads
```

Sitting flat it read about 1.03 g on z with the gyro near 0 (about 1 deg/s offset on x, which calibration removes).

## Calibration

Calibration learns three directions in sensor coordinates: up, right and forward. Everything after that is measured relative to the player's own standing pose, so the mounting angle and posture don't matter.

| Step | Player does | Code |
| --- | --- | --- |
| Get ready | 3 s to get into position | nothing is measured yet |
| Still | stand still, face forward | averages 1 s of accel for a rough up, and the gyro offset |
| Lean right | lean right and hold | averages 0.5 s once tilted more than 12° and holding still |
| Center | stand up straight | waits until at least halfway back out of the lean and still |
| Bend forward | bend forward and hold | same as the lean |
| Return | stand up straight | averages 0.5 s, this becomes the final up |

- The final standing pose is used as up, not the first one. In testing the first pose was often about 12° off because the player was still leaning toward the laptop.
- Right and forward come from how the lean and bend moved gravity away from the final up. If they aren't exactly 90° apart the error is split evenly between them.
- The calibration is rejected (old axes kept) if the lean or bend was too small or the two directions were too alike.
- Quick calibration only re-measures up and the gyro offset, and keeps right and forward. If the IMU was moved too much since last time it carries on into the full calibration.
- `gesture_set_axes()` loads saved axes and rejects ones that point the same way.

The game saves the axes (and uses the saved sensitivity) through the settings file. The first start does the full calibration. After that each start only does the quick one, so the player just stands still for about 4 s. `motion_ski -c` ignores the saved axes and does the full calibration, for when the IMU is mounted differently.

## Tilt tracking

A complementary filter keeps track of which way gravity points:

- Each sample the estimate is rotated by the gyro reading (minus the offset from calibration). This follows fast movement.
- It is then pulled 2% toward the accel direction, which stops the gyro from drifting (time constant about 0.5 s at 104 Hz).
- The accel is only trusted when it reads within 0.2 g of 1 g. During a jump it doesn't point at gravity, so only the gyro is used.

From that estimate:

- **roll**: sideways tilt from the standing pose, positive to the right
- **pitch**: forward tilt, positive when bending forward
- **up**: acceleration along the vertical, about 1 g standing still

## Gesture detection

| Gesture | Fires when | Re-arms when |
| --- | --- | --- |
| LEFT / RIGHT | roll past 20° for 3 samples (about 29 ms) | roll back under 8° |
| CROUCH | pitch past 25° for 3 samples | pitch back under 10° |
| JUMP | up past 1.5 g for 2 samples (about 19 ms), on the push off | 0.7 s after takeoff and the landing has settled |

Rules to stop false triggers, each added after something went wrong in board testing:

- Only the bigger tilt counts. A lean needs more sideways than forward tilt (by size), a crouch needs more forward than sideways. A crouch that drifts sideways stays a crouch, and leaning back with some sideways drift isn't a lean.
- No leans or crouches while in the air or until the landing settles (150 ms of calm).
- No crouch until the jump is over, since a landing dips forward. The game can't crouch mid jump anyway.
- A second jump can't fire within 0.7 s, the same as the game's jump time. This stops a landing bounce from counting twice.

Sensitivity scales all thresholds:

| Level | Scale | Lean | Crouch | Jump |
| --- | --- | --- | --- | --- |
| Low | 1.3 | 26° | 32.5° | 1.65 g |
| Med | 1.0 | 20° | 25° | 1.5 g |
| High | 0.75 | 15° | 18.75° | 1.375 g |

All the public gesture functions share one mutex since the IMU thread and the menu call them from different threads. The game's IMU thread (`app/imu_worker.c`) runs `imu_read()` -> `gesture_update()` -> `events_push(ev, SRC_IMU)`. If there's no good reading for 0.5 s (for example a loose wire) the game shows the IMU as unavailable and keeps going on the keyboard, and goes back once readings return.

## Latency

| Step | Time |
| --- | --- |
| New sample | every 9.6 ms (104 Hz) |
| Lean or crouch after crossing the threshold | about 29 ms |
| Jump after the push off starts | about 19 ms |
| Player leaning past 20° | about 0.2 s (estimate) |

Jumping and then changing lanes takes about 0.8 s (land, settle, lean). At the game's top speed (26) rows come every 0.7 s, so that move is too tight there. At the start speed (14) rows are 1.3 s apart. A top speed around 18 gives about 1 s between rows. This will be timed properly once the IMU is on the chest strap.

## Tools

| Tool | Runs on | Use |
| --- | --- | --- |
| `imu_log` | board | records CSV, `-g` calibrates first and prints gestures live, `-t` stops after a time |
| `plot_imu.py` | PC | plots accel, gyro, roll/pitch and up with the thresholds and events |
| `gesture_replay` | PC | runs a recording back through the current gesture code and compares the events |
| `gesture_test` | PC | simulated IMU tests, no board needed |

See `data/README.md` for the recording and replay commands.

## Testing

`gesture_test` simulates an IMU mounted at a tilt, with noise and a gyro offset, and checks 22 cases:

- calibration finds the right axes, including starting hunched over and a new mounting angle
- standing still and small sway fire nothing
- each lean, crouch and jump fires exactly once, and holding a lean doesn't repeat
- a wobble in the air, a forward dip after landing, a crouch drifting sideways and leaning back with drift don't fire a lean or crouch
- sensitivity levels, quick calibration, cancel, saved axes and rejected axes

Board testing with `imu_log -g` (IMU held on a breadboard against the chest) found these problems, which were fixed and checked with replay:

| Problem | Cause (from the recording) | Fix |
| --- | --- | --- |
| Calibration stuck on the last "stand up straight" | first pose about 12° off, never matched again | 3 s ready time, return check based on leaving the hold, final pose used as up |
| Calibration rejected a sideways bend | bend went 14° to the side | error split between right and forward, looser check |
| RIGHT fired after crouches | 30° bend carried 15° sideways | bigger tilt wins |
| LEFT/RIGHT fired mid jump | body wobble while in the air | ignore tilt until the landing settles |
| JUMP counted twice | landing bounce | 0.7 s between jumps |
| CROUCH after landing | landing dips forward | no crouch until the jump is over |

Replaying the first recording (made before these fixes) with the current code:

| | LEFT | RIGHT | JUMP | CROUCH |
| --- | --- | --- | --- | --- |
| Recorded | 11 | 15 | 11 | 9 |
| Replayed | 9 | 10 | 10 | 6 |

A third of the RIGHTs in that recording were false and are gone. The full game was also played on the board with motion controls.

## Accuracy

To be filled in from chest strap recordings: 10 of each gesture and 30 s standing still, counted with `gesture_replay`.

| Gesture | Tries | Detected | Missed | Wrong gesture |
| --- | --- | --- | --- | --- |
| Left | 10 | - | - | - |
| Right | 10 | - | - | - |
| Jump | 10 | - | - | - |
| Crouch | 10 | - | - | - |
| Still (30 s) | - | - | - | - |

## Still to do

- Chest strap recordings, threshold tuning and the accuracy table above
- Time jump then lean on the strap and pick the game's top speed
- Check 104 Hz and 0 failed reads through the EndPoints and Cat6 cable
- Possibly use the INT1 data-ready pin instead of polling STATUS
