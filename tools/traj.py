"""
Shared helpers: read a trajectory file and resample it to the command rate.

Input format (what ORB-SLAM3 writes, "TUM" format), one pose per line:
    timestamp  tx  ty  tz  qx  qy  qz  qw
Positions in metres, orientation as a quaternion. Lines starting with '#' are ignored.
Timestamps may be seconds (TUM) or nanoseconds (ORB-SLAM3's EuRoC output); detected automatically.
"""
import math

RATE_HZ = 20
DT = 1.0 / RATE_HZ


def load_tum(path):
    """Return a list of (t_seconds, x, y, z, yaw_rad), sorted by time."""
    poses = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) < 8:
                continue
            t, x, y, z, qx, qy, qz, qw = (float(p) for p in parts[:8])
            if t > 1e11:            # nanoseconds
                t /= 1e9
            yaw = math.atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz))
            poses.append((t, x, y, z, yaw))
    poses.sort()
    if len(poses) < 2:
        raise SystemExit(f"{path}: need at least two poses")
    return poses


def resample(poses, rate_hz=RATE_HZ):
    """Linear interpolation onto a fixed grid. Returns a list of (t, x, y, z, yaw)."""
    dt = 1.0 / rate_hz
    out = []
    t0, tn = poses[0][0], poses[-1][0]
    j = 0
    t = t0
    while t <= tn + 1e-9:
        while j + 1 < len(poses) - 1 and poses[j + 1][0] < t:
            j += 1
        a, b = poses[j], poses[j + 1]
        span = b[0] - a[0]
        f = 0.0 if span <= 0 else min(max((t - a[0]) / span, 0.0), 1.0)
        x = a[1] + f * (b[1] - a[1])
        y = a[2] + f * (b[2] - a[2])
        z = a[3] + f * (b[3] - a[3])
        yaw = a[4] if f < 0.5 else b[4]
        out.append((t - t0, x, y, z, yaw))
        t += dt
    return out


def speeds(samples):
    """Speed (m/s) of each step between consecutive samples."""
    v = []
    for a, b in zip(samples, samples[1:]):
        d = math.dist(a[1:4], b[1:4])
        dt = b[0] - a[0]
        v.append(d / dt if dt > 0 else 0.0)
    return v


def read_policy(path="verifier/policy.h"):
    """Pull the numeric #defines out of policy.h."""
    vals = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 3 and parts[0] == "#define":
                try:
                    vals[parts[1]] = int(parts[2].strip("()"))
                except ValueError:
                    pass
    return vals
