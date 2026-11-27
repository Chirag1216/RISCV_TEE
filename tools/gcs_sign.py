#!/usr/bin/env python3
"""Sign a struct gcs_mission with HMAC-SHA256 using K_GCS (matches verifier/verifier.c)."""

import hashlib
import hmac
import struct

K_GCS = bytes([
    0x52, 0x56, 0x54, 0x45, 0x45, 0x5f, 0x47, 0x43,
    0x53, 0x5f, 0x4b, 0x45, 0x59, 0x5f, 0x30, 0x31,
    0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
])

MAX_WAYPOINTS = 16


def pack_mission(
    mission_seq: int,
    fence_x: tuple[int, int],
    fence_y: tuple[int, int],
    fence_z: tuple[int, int],
    v_max_mm_s: int,
    corridor_radius_mm: int,
    waypoints: list[tuple[int, int, int]],
) -> bytes:
    assert len(waypoints) <= MAX_WAYPOINTS
    wps_padded = list(waypoints) + [(0, 0, 0)] * (MAX_WAYPOINTS - len(waypoints))
    flat_wps = [coord for wp in wps_padded for coord in wp]
    body = struct.pack(
        "<Q6i3I48i",
        mission_seq,
        fence_x[0], fence_x[1],
        fence_y[0], fence_y[1],
        fence_z[0], fence_z[1],
        v_max_mm_s,
        corridor_radius_mm,
        len(waypoints),
        *flat_wps,
    )
    tag = hmac.new(K_GCS, body, hashlib.sha256).digest()
    return body + tag


if __name__ == "__main__":
    # Cross-check RFC 4231 Test Case 2 ("Jefe" / "what do ya want for nothing?")
    rfc_expected = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
    rfc_got = hmac.new(b"Jefe", b"what do ya want for nothing?", hashlib.sha256).hexdigest()
    assert rfc_got == rfc_expected

    signed = pack_mission(
        mission_seq=1,
        fence_x=(-4000, 4000),
        fence_y=(-2500, 2500),
        fence_z=(-300, 2300),
        v_max_mm_s=0,
        corridor_radius_mm=300,
        waypoints=[(0, 0, 1000), (1000, 0, 1000)],
    )
    tag = signed[-32:]
    print(f"mission body bytes = {len(signed) - 32}, total = {len(signed)}")
    print("hmac[32] = {")
    for i in range(0, 32, 8):
        print("    " + ", ".join(f"0x{b:02x}" for b in tag[i : i + 8]) + ",")
    print("};")
