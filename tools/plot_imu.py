# plots imu_log csv files, saves a png next to each one
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

EV_NAMES = ["LEFT", "RIGHT", "JUMP", "CROUCH"]

# same starting thresholds as gesture.c
LEAN_DEG = 15
CROUCH_DEG = 25
JUMP_G = 1.5


def load(path):
    with open(path) as f:
        rows = list(csv.DictReader(line for line in f if not line.startswith("#")))
    if not rows:
        return None
    d = {k: [float(r[k]) for r in rows] for k in rows[0]}
    t0 = d["t_ms"][0]
    d["t"] = [(x - t0) / 1000 for x in d["t_ms"]]
    return d


def plot(path):
    d = load(path)
    if d is None:
        print(f"skipped {path} (no data)")
        return
    t = d["t"]
    events = [(ti, int(e)) for ti, e in zip(t, d["ev"]) if e >= 0]

    fig, ax = plt.subplots(4, 1, sharex=True, figsize=(11, 9))

    for k in ("ax", "ay", "az"):
        ax[0].plot(t, d[k], lw=0.8, label=k)
    ax[0].set_ylabel("accel (g)")

    for k in ("gx", "gy", "gz"):
        ax[1].plot(t, d[k], lw=0.8, label=k)
    ax[1].set_ylabel("gyro (deg/s)")

    ax[2].plot(t, d["roll"], lw=0.8, label="roll")
    ax[2].plot(t, d["pitch"], lw=0.8, label="pitch")
    for y in (LEAN_DEG, -LEAN_DEG):
        ax[2].axhline(y, ls="--", c="k", lw=0.7)
    ax[2].axhline(CROUCH_DEG, ls=":", c="k", lw=0.7)
    ax[2].set_ylabel("angle (deg)")

    ax[3].plot(t, d["up"], lw=0.8, label="up")
    ax[3].axhline(JUMP_G, ls="--", c="k", lw=0.7)
    ax[3].set_ylabel("up accel (g)")
    ax[3].set_xlabel("time (s)")

    for a in ax:
        for ti, _ in events:
            a.axvline(ti, c="r", alpha=0.35, lw=1)
        a.grid(alpha=0.3)
        a.legend(loc="upper right", fontsize=7)

    top = ax[0].get_ylim()[1]
    for ti, e in events:
        name = EV_NAMES[e] if e < len(EV_NAMES) else str(e)
        ax[0].text(ti, top, name, rotation=90, va="top", fontsize=7, color="r")

    counts = "  ".join(f"{n} {sum(1 for _, e in events if e == i)}" for i, n in enumerate(EV_NAMES))
    ax[0].set_title(f"{path}    {counts}")

    fig.tight_layout()
    out = path.rsplit(".", 1)[0] + ".png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"saved {out}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: python3 plot_imu.py file.csv [more.csv ...]")
        sys.exit(1)
    for p in sys.argv[1:]:
        plot(p)
