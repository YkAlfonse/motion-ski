# IMU recordings

Recorded on the board with `imu_log`. Stand still for 3 s at the start of each one.

```bash
sudo ./imu_log -g > jump_sehaj_01.csv      # calibrate first, then record
sudo ./imu_log -t 30 > still_sehaj_01.csv  # raw only, stop after 30 s
```

Ctrl+C stops a recording. When it stops it prints the sample count, rate and failed reads.

Plot on the PC:

```bash
python3 tools/plot_imu.py data/*.csv
```

## Names

`<gesture>_<person>_<nn>.csv`, for example `jump_sehaj_01.csv`.

Gestures: `left`, `right`, `jump`, `crouch`, `still`.

## Columns

| Column | Meaning |
| --- | --- |
| t_ms | time in ms (monotonic clock) |
| ax, ay, az | acceleration in g |
| gx, gy, gz | rotation rate in deg/s |
| roll, pitch | body angles in degrees (only with `-g`) |
| up | acceleration along the calibrated up direction in g (only with `-g`) |
| ev | detected gesture: -1 none, 0 LEFT, 1 RIGHT, 2 JUMP, 3 CROUCH |

With `-g` the file also starts with a `# axes` line holding the calibrated up, right and forward directions.

## Needed

- 10 each of left, right, jump and crouch
- 30 s of standing still
- jump and crouch from every teammate if possible
