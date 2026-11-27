#!/usr/bin/env python3
"""
Convert EuRoC state_groundtruth_estimate0/data.csv to TUM trajectory format.

EuRoC CSV: timestamp_ns, tx, ty, tz, qw, qx, qy, qz, ...
TUM TXT:   timestamp_sec tx ty tz qx qy qz qw
"""
import sys


def convert(in_csv: str, out_txt: str) -> int:
    count = 0
    with open(in_csv, "r") as fin, open(out_txt, "w") as fout:
        fout.write("# timestamp tx ty tz qx qy qz qw\n")
        for line in fin:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if len(parts) < 8:
                continue
            ts_ns = int(parts[0])
            tx, ty, tz = parts[1], parts[2], parts[3]
            qw, qx, qy, qz = parts[4], parts[5], parts[6], parts[7]
            fout.write(f"{ts_ns / 1e9:.9f} {tx} {ty} {tz} {qx} {qy} {qz} {qw}\n")
            count += 1
    return count


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <input.csv> <output.txt>")
    n = convert(sys.argv[1], sys.argv[2])
    print(f"{sys.argv[2]}: wrote {n} poses")
