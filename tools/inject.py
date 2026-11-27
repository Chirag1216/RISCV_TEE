#!/usr/bin/env python3
"""
Turn a trajectory into a 20 Hz command stream, optionally with one attack injected.

    python3 tools/inject.py data/placeholder.txt --attack teleport --out data/ph_teleport

Writes two files:
    <out>.cmd         the commands the replayer sends (one per line)
    <out>.labels.csv  ground truth: which commands are attacks (for score.py)

Attacks (all start at --at, a fraction of the flight, default 0.5):
    clean      no attack: measures the false-positive rate
    teleport   the reported position jumps by --magnitude metres (default 2.0) and stays offset
    overspeed  the position runs away at --magnitude m/s (default 2 x V_MAX) for 10 frames, then holds
    fence      the position is pushed outside the geofence and stays there
    replay     one earlier command (1 s old) is sent again with its old sequence number
    flood      4 extra copies of one command are sent with no delay
    mode       one command asks for DISARMED while in flight
    drift      the position creeps away at --magnitude m/s (default 0.3), under the speed limit
               -> expected to go undetected until it leaves the geofence
"""
import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from traj import load_tum, resample, read_policy, DT

MODE_ARMED, MODE_FLIGHT, MODE_DISARMED = 1, 2, 0
DELAY_US = int(DT * 1e6)

ap = argparse.ArgumentParser()
ap.add_argument("traj")
ap.add_argument("--out", required=True)
ap.add_argument("--attack", default="clean",
                choices=["clean", "teleport", "overspeed", "fence", "replay", "flood", "mode", "drift"])
ap.add_argument("--at", type=float, default=0.5, help="start of the attack as a fraction of the flight")
ap.add_argument("--magnitude", type=float, default=None)
ap.add_argument("--max-frames", type=int, default=0, help="truncate the flight (quick tests)")
ap.add_argument("--seed", type=int, default=1)
args = ap.parse_args()

random.seed(args.seed)
policy = read_policy()
v_max = policy["V_MAX_MM_S"] / 1000.0

samples = resample(load_tum(args.traj))
if args.max_frames:
    samples = samples[:args.max_frames]
n = len(samples)
k = int(n * args.at)

# Each command: dict(idx, delay_us, seq, x, y, z, yaw, mode, label). seq None = assign later.
cmds = []
for i, (t, x, y, z, yaw) in enumerate(samples):
    cmds.append(dict(delay_us=DELAY_US, seq=None, x=x, y=y, z=z, yaw=yaw,
                     mode=MODE_ARMED if i == 0 else MODE_FLIGHT, label="clean", src=i))

a = args.attack
if a == "teleport":
    d = args.magnitude if args.magnitude is not None else 2.0
    for c in cmds[k:]:
        c["x"] += d
        c["label"] = a
elif a in ("overspeed", "drift"):
    # The extra motion is added along the direction the drone is already moving,
    # so it adds to the real speed instead of partly cancelling it.
    if a == "overspeed":
        v, ramp_frames = (args.magnitude if args.magnitude is not None else 2.0 * v_max), 10
    else:
        v, ramp_frames = (args.magnitude if args.magnitude is not None else 0.3), len(cmds)
    off = [0.0, 0.0, 0.0]
    prev = (cmds[k - 1]["x"], cmds[k - 1]["y"], cmds[k - 1]["z"]) if k > 0 else None
    for j, c in enumerate(cmds[k:]):
        cur = (c["x"], c["y"], c["z"])
        if j < ramp_frames:
            step = [cur[i] - prev[i] for i in range(3)] if prev else [1.0, 0.0, 0.0]
            norm = (step[0] ** 2 + step[1] ** 2 + step[2] ** 2) ** 0.5
            u = [s / norm for s in step] if norm > 1e-6 else [1.0, 0.0, 0.0]
            for i in range(3):
                off[i] += u[i] * v * DT
        prev = cur
        c["x"] += off[0]
        c["y"] += off[1]
        c["z"] += off[2]
        c["label"] = a
elif a == "fence":
    xmax = policy["FENCE_X_MAX"] / 1000.0
    x0 = cmds[k]["x"]
    for c in cmds[k:]:
        c["x"] = xmax + 1.0 + (c["x"] - x0)      # 1 m outside the fence, same relative motion
        c["label"] = a
elif a == "mode":
    cmds[k]["mode"] = MODE_DISARMED
    cmds[k]["label"] = a
elif a == "replay":
    old = dict(cmds[max(0, k - 20)])        # 1 s earlier
    old["label"] = a
    old["replay_of"] = max(0, k - 20)
    cmds.insert(k, old)
elif a == "flood":
    for _ in range(4):
        extra = dict(cmds[k])
        extra["delay_us"] = 0
        extra["label"] = a
        cmds.insert(k + 1, extra)

# Sequence numbers: increasing, except a replayed command keeps the old one.
seq = 0
for i, c in enumerate(cmds):
    c["idx"] = i
    if "replay_of" not in c:
        seq += 1
        c["seq"] = seq
for c in cmds:
    if "replay_of" in c:
        c["seq"] = cmds[c["replay_of"]]["seq"]

expected = dict(teleport="JUMP", overspeed="SPEED", fence="FENCE", replay="SEQ",
                flood="RATE", mode="MODE", drift="none", clean="none")

with open(args.out + ".cmd", "w") as f:
    f.write("# idx,delay_us,seq,x_mm,y_mm,z_mm,yaw_mdeg,mode\n")
    for c in cmds:
        f.write(f"{c['idx']},{c['delay_us']},{c['seq']},{round(c['x']*1000)},{round(c['y']*1000)},"
                f"{round(c['z']*1000)},{round(c['yaw']*180000/3.141592653589793)},{c['mode']}\n")
with open(args.out + ".labels.csv", "w") as f:
    f.write(f"# attack={a} at_frame={k} expected={expected[a]} source={os.path.basename(args.traj)}\n")
    f.write("idx,label\n")
    for c in cmds:
        f.write(f"{c['idx']},{c['label']}\n")

n_att = sum(1 for c in cmds if c["label"] != "clean")
print(f"{args.out}.cmd: {len(cmds)} commands ({len(cmds)*DT:.0f} s), attack={a}, "
      f"{n_att} attack frames from frame {k}")
