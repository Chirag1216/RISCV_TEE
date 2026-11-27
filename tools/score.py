#!/usr/bin/env python3
"""
Compare the verifier's verdicts with the injected ground truth.

    python3 tools/score.py results/ph_teleport.log data/ph_teleport.labels.csv

The log is the console output of `make replay` (lines "R,idx,seq,verdict").
"""
import collections
import math
import os
import sys

VERDICTS = ["ACCEPT", "SEQ", "RATE", "MODE", "FENCE", "JUMP", "SPEED", "TIMEOUT", "FAULT"]
DT = 0.05

log, labels_file = sys.argv[1], sys.argv[2]

verdicts = {}
for line in open(log, errors="replace"):
    if line.startswith("R,"):
        _, idx, seq, v = line.strip().split(",")[:4]
        verdicts[int(idx)] = int(v)

meta = {}
labels = {}
for line in open(labels_file):
    if line.startswith("#"):
        for kv in line[1:].split():
            if "=" in kv:
                key, val = kv.split("=", 1)
                meta[key] = val
        continue
    if line.startswith("idx"):
        continue
    idx, label = line.strip().split(",")
    labels[int(idx)] = label

missing = [i for i in labels if i not in verdicts]
if missing:
    print(f"warning: {len(missing)} commands have no verdict in the log (first: {missing[0]})")

clean = [i for i, l in labels.items() if l == "clean" and i in verdicts]
attack = [i for i, l in labels.items() if l != "clean" and i in verdicts]

fp = sum(1 for i in clean if verdicts[i] != 0)
tp = sum(1 for i in attack if verdicts[i] != 0)

print(f"attack   : {meta.get('attack')}  (expected reason: {meta.get('expected')}, source {meta.get('source')})")
print(f"clean    : {len(clean)} frames, {fp} rejected  ->  FPR = {fp/len(clean)*100 if clean else 0:.2f} %")
if clean and fp:
    reasons = collections.Counter(VERDICTS[verdicts[i]] for i in clean if verdicts[i] != 0)
    print(f"           false rejects by reason: {dict(reasons)}")
if attack:
    print(f"attack   : {len(attack)} frames, {tp} rejected  ->  TPR = {tp/len(attack)*100:.2f} %")
    reasons = collections.Counter(VERDICTS[verdicts[i]] for i in attack if verdicts[i] != 0)
    print(f"           rejected by reason: {dict(reasons)}")
    first = int(meta.get("at_frame", min(attack)))
    det = [i for i in attack if verdicts[i] != 0]
    if det:
        lat = det[0] - first
        print(f"latency  : first rejection at frame {det[0]} ({VERDICTS[verdicts[det[0]]]}), "
              f"{lat} frames = {lat*DT:.2f} s after the attack began")
    else:
        print("latency  : never detected")

    # Size of the step the attack produced at its first frame (for the detectability bound).
    cmd_file = labels_file.replace(".labels.csv", ".cmd")
    if os.path.exists(cmd_file):
        pos = {}
        for line in open(cmd_file):
            if line.startswith("#"):
                continue
            f = line.strip().split(",")
            pos[int(f[0])] = (int(f[3]), int(f[4]), int(f[5]))
        if first in pos and first - 1 in pos:
            step = math.dist(pos[first], pos[first - 1])
            print(f"step     : {step:.0f} mm at frame {first} "
                  f"({'rejected' if verdicts.get(first, 0) else 'accepted'})")
