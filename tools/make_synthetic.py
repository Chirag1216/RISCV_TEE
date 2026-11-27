#!/usr/bin/env python3
"""
PLACEHOLDER trajectory generator.

Writes a smooth figure-of-eight flight in TUM format, the same format ORB-SLAM3
produces. Use it to test the pipeline before the real ORB-SLAM3 output on
EuRoC is available. Replace its output with the real trajectory file.

    python3 tools/make_synthetic.py data/placeholder.txt
"""
import math
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "data/placeholder.txt"
duration = 60.0         # seconds
rate = 20.0             # Hz, like the EuRoC cameras
ax, ay = 3.0, 1.5       # half-sizes of the figure-of-eight, metres
period = 20.0           # seconds per loop

with open(out, "w") as f:
    f.write("# synthetic placeholder: timestamp tx ty tz qx qy qz qw\n")
    n = int(duration * rate)
    for i in range(n):
        t = i / rate
        w = 2 * math.pi * t / period
        x = ax * math.sin(w)
        y = ay * math.sin(2 * w)
        z = 1.0 + 0.3 * math.sin(w / 2)
        # heading along the direction of travel
        dx = ax * math.cos(w)
        dy = 2 * ay * math.cos(2 * w)
        yaw = math.atan2(dy, dx)
        qz, qw = math.sin(yaw / 2), math.cos(yaw / 2)
        f.write(f"{1403636579.0 + t:.6f} {x:.6f} {y:.6f} {z:.6f} 0 0 {qz:.6f} {qw:.6f}\n")
print(f"wrote {n} poses to {out}")
