#!/usr/bin/env python3
"""
Compare Static Room Box Geofence (Phase 1) vs. 16-waypoint GCS-Signed Corridor
(Phase 2, R = 500 mm) across drift rates 0.1, 0.2, 0.3, 0.5 m/s starting at at=0.5.

Uses the exact integer point-to-segment math from verifier/verifier.c and signs
the 16-waypoint mission via tools/gcs_sign.py.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gcs_sign import MAX_WAYPOINTS, pack_mission
from traj import DT, load_tum, read_policy, resample

LEG_FRAMES = 400  # 20 s at 20 Hz


def in_box_bounds(px: int, py: int, pz: int,
                  fx: tuple[int, int], fy: tuple[int, int], fz: tuple[int, int]) -> bool:
    return fx[0] <= px <= fx[1] and fy[0] <= py <= fy[1] and fz[0] <= pz <= fz[1]


def in_box(px: int, py: int, pz: int, pol: dict) -> bool:
    return in_box_bounds(
        px, py, pz,
        (pol["FENCE_X_MIN"], pol["FENCE_X_MAX"]),
        (pol["FENCE_Y_MIN"], pol["FENCE_Y_MAX"]),
        (pol["FENCE_Z_MIN"], pol["FENCE_Z_MAX"]),
    )


def in_corridor(px: int, py: int, pz: int,
                wps: list[tuple[int, int, int]], radius_mm: int) -> bool:
    """Exact integer dot-product math matching verifier/verifier.c:in_corridor()."""
    r2 = radius_mm * radius_mm
    for (ax, ay, az), (bx, by, bz) in zip(wps, wps[1:]):
        ux, uy, uz = bx - ax, by - ay, bz - az
        wx, wy, wz = px - ax, py - ay, pz - az
        len2 = ux * ux + uy * uy + uz * uz
        dot = wx * ux + wy * uy + wz * uz
        if len2 == 0 or dot <= 0:
            if wx * wx + wy * wy + wz * wz <= r2:
                return True
        elif dot >= len2:
            vx, vy, vz = px - bx, py - by, pz - bz
            if vx * vx + vy * vy + vz * vz <= r2:
                return True
        else:
            w2 = wx * wx + wy * wy + wz * wz
            if w2 * len2 - dot * dot <= r2 * len2:
                return True
    return False


def build_drift(samples, k: int, v_drift: float) -> tuple[list[tuple[int, int, int]], list[float]]:
    """Apply lateral +Y off-course drift starting at frame k; return (pts_mm, dev_m)."""
    off_y = 0.0
    out = []
    devs = []
    for i, (_, x, y, z, _) in enumerate(samples):
        if i >= k:
            off_y += v_drift * DT
        devs.append(off_y)
        out.append((round(x * 1000), round((y + off_y) * 1000), round(z * 1000)))
    return out, devs


def eval_detector(mm_pts: list[tuple[int, int, int]], devs_m: list[float],
                  k: int, pred) -> tuple[float, str, float]:
    flags = [not pred(i, px, py, pz) for i, (px, py, pz) in enumerate(mm_pts) if i >= k]
    tpr = sum(flags) / len(flags) * 100.0 if flags else 0.0
    det = next((idx for idx, f in enumerate(flags) if f), None)
    if det is not None:
        lat_str = f"{det} fr ({det * DT:.2f} s)"
        max_err = max(devs_m[k:k + det], default=0.0)
    else:
        lat_str = "never"
        max_err = max(devs_m[k:], default=0.0)
    return tpr, lat_str, max_err


def build_legs(samples, v_max_mm_s: int, radius_mm: int) -> list[dict]:
    """Stream 20 s (400-frame) signed mission legs with 16 waypoints and +1 m room margin."""
    legs = []
    n = len(samples)
    for leg_idx, start in enumerate(range(0, n, LEG_FRAMES)):
        end = min(start + LEG_FRAMES, n)
        seg = samples[start:end]
        fx = (math.floor((min(s[1] for s in seg) - 1.0) * 1000),
              math.ceil((max(s[1] for s in seg) + 1.0) * 1000))
        fy = (math.floor((min(s[2] for s in seg) - 1.0) * 1000),
              math.ceil((max(s[2] for s in seg) + 1.0) * 1000))
        fz = (math.floor((min(s[3] for s in seg) - 1.0) * 1000),
              math.ceil((max(s[3] for s in seg) + 1.0) * 1000))

        wps = []
        if len(seg) == 1:
            wps = [(round(seg[0][1] * 1000), round(seg[0][2] * 1000), round(seg[0][3] * 1000))] * 2
        else:
            for i in range(MAX_WAYPOINTS):
                idx = round(i * (len(seg) - 1) / (MAX_WAYPOINTS - 1))
                _, x, y, z, _ = seg[idx]
                wps.append((round(x * 1000), round(y * 1000), round(z * 1000)))

        blob = pack_mission(
            mission_seq=leg_idx + 1,
            fence_x=fx,
            fence_y=fy,
            fence_z=fz,
            v_max_mm_s=v_max_mm_s,
            corridor_radius_mm=radius_mm,
            waypoints=wps,
        )
        legs.append(dict(start=start, end=end, fx=fx, fy=fy, fz=fz, wps=wps, blob=blob))
    return legs


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("traj")
    ap.add_argument("--max-frames", type=int, default=0)
    ap.add_argument("--radius-mm", type=int, default=500)
    ap.add_argument("--at", type=float, default=0.5)
    args = ap.parse_args()

    name = os.path.splitext(os.path.basename(args.traj))[0]
    pol = read_policy()
    samples = resample(load_tum(args.traj))
    if args.max_frames:
        samples = samples[:args.max_frames]
    n = len(samples)
    k = int(n * args.at)

    legs = build_legs(samples, pol["V_MAX_MM_S"], args.radius_mm)

    def corr_ok(frame_idx: int, px: int, py: int, pz: int) -> bool:
        leg = legs[frame_idx // LEG_FRAMES]
        return (
            in_box_bounds(px, py, pz, leg["fx"], leg["fy"], leg["fz"])
            and in_corridor(px, py, pz, leg["wps"], args.radius_mm)
        )

    clean_pts = [(round(s[1] * 1000), round(s[2] * 1000), round(s[3] * 1000)) for s in samples]
    box_fp = sum(1 for (x, y, z) in clean_pts if not in_box(x, y, z, pol))
    cor_fp = sum(1 for i, (x, y, z) in enumerate(clean_pts) if not corr_ok(i, x, y, z))

    lines = [
        f"Drift Comparison: {name} ({n} frames, {len(legs)} legs x {LEG_FRAMES} fr, "
        f"lateral +Y drift at frame {k}, R={args.radius_mm} mm, V_MAX={pol['V_MAX_MM_S']} mm/s)",
        f"Clean FPR: Phase1_Box={box_fp}/{n} ({box_fp/n*100:.2f}%)  "
        f"Phase2_Corridor={cor_fp}/{n} ({cor_fp/n*100:.2f}%)",
        "",
        f"{'drift_m/s':<10} {'box_TPR':<10} {'box_latency':<18} {'box_max_err':<13} "
        f"{'corr_TPR':<10} {'corr_latency':<18} {'corr_max_err'}",
    ]
    for v_drift in (0.1, 0.2, 0.3, 0.5):
        pts, devs = build_drift(samples, k, v_drift)
        b_tpr, b_lat, b_err = eval_detector(pts, devs, k, lambda i, x, y, z: in_box(x, y, z, pol))
        c_tpr, c_lat, c_err = eval_detector(pts, devs, k, corr_ok)
        lines.append(
            f"{v_drift:<10.1f} {b_tpr:<9.2f}% {b_lat:<18} {b_err:<11.2f} m "
            f"{c_tpr:<9.2f}% {c_lat:<18} {c_err:.2f} m"
        )

    os.makedirs("results", exist_ok=True)
    out_path = f"results/drift_compare_{name}.txt"
    text = "\n".join(lines) + "\n"
    with open(out_path, "w") as f:
        f.write(text)
    print(text, end="")


if __name__ == "__main__":
    main()
