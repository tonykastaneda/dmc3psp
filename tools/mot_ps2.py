#!/usr/bin/env python3
"""DMC3 PS2 MOT (motion) parser -- format recovered from Dante's motions.

Header (0x50 bytes):
  +0x00 u32  header size (0x50)
  +0x04 char "MOT\\0"
  +0x08 f32  loop start frame
  +0x0C f32  end frame
  +0x10 f32  (unknown, usually 0)      +0x14 f32 (usually == end frame)
  +0x18 u16  flag (0/1)  +0x1A u16 (0 or 0x18)
  +0x1C u16  bone count (24)
  +0x1E u16[24] channel mask per bone: bits 3-5 rotation, 6-8 translation
  +0x4E u16  offset of a trailing section (0 = none; not decoded yet)
  +0x50 u32  channel count (== popcount of all masks)
Channel records follow from +0x54, ordered by bone then by channel bit:
  u16 record size (bytes, including this header)
  u16 key count
  u32 interpolation type:
    3 = quantized Hermite: f32[6] value/in/out-tangent base+scale, then
        key[count] x 8 bytes {u16 frame (bit 15 usually set), u16 value,
        u16 in-tangent, u16 out-tangent}
    2 = quantized linear (enemies): f32[2] value base+scale, then
        key[count] x 4 bytes {u16 frame, u16 value}
  (each u16 q maps to base + scale * q / 65535; tangents per frame)
"""

import struct

HEADER_SIZE = 0x50


def parse_mot(data: bytes):
    if len(data) < HEADER_SIZE + 4 or data[4:8] != b"MOT\0":
        raise ValueError("Not a DMC3 MOT")
    if struct.unpack_from("<I", data, 0)[0] != HEADER_SIZE:
        raise ValueError("Unexpected MOT header size")
    loop_start, end_frame, unknown_10, unknown_14 = struct.unpack_from("<4f", data, 8)
    flag_18, unknown_1a, bone_count = struct.unpack_from("<3H", data, 0x18)
    if not 0 < bone_count <= 24:
        raise ValueError(f"Bone count {bone_count} does not fit the 0x1E..0x4E mask table")
    masks = struct.unpack_from(f"<{bone_count}H", data, 0x1E)
    trailer_offset = struct.unpack_from("<H", data, 0x4E)[0]
    channel_count = struct.unpack_from("<I", data, HEADER_SIZE)[0]
    expected = [(bone, bit) for bone, mask in enumerate(masks) for bit in range(16) if mask >> bit & 1]
    if len(expected) != channel_count:
        raise ValueError(f"Masks give {len(expected)} channels, header says {channel_count}")

    channels = []
    offset = HEADER_SIZE + 4
    for bone, bit in expected:
        if offset + 32 > len(data):
            raise ValueError(f"Truncated before channel {len(channels)}")
        size, key_count, kind = struct.unpack_from("<HHI", data, offset)
        if kind == 3:
            header, stride = 32, 8
        elif kind == 2:
            header, stride = 16, 4
        else:
            raise ValueError(f"Channel {len(channels)} has interpolation type {kind}")
        if size != header + key_count * stride:
            raise ValueError(f"Channel {len(channels)} size {size} != {header} + {stride} * {key_count}")
        params = struct.unpack_from("<6f" if kind == 3 else "<2f", data, offset + 8)
        keys = []
        last_frame = -1
        for k in range(key_count):
            if kind == 3:
                raw_frame, qv, qi, qo = struct.unpack_from("<4H", data, offset + header + k * 8)
            else:
                raw_frame, qv = struct.unpack_from("<2H", data, offset + header + k * 4)
            frame = raw_frame & 0x7FFF
            if frame <= last_frame:
                raise ValueError(f"Channel {len(channels)} key frames not increasing")
            last_frame = frame
            value = params[0] + params[1] * qv / 65535.0
            if kind == 3:
                keys.append((frame, value, params[2] + params[3] * qi / 65535.0, params[4] + params[5] * qo / 65535.0))
            else:
                keys.append((frame, value, None, None))
        channels.append({"bone": bone, "channel": bit, "kind": kind, "keys": keys})
        offset += size
    return {
        "loop_start": loop_start,
        "end_frame": end_frame,
        "unknown": [unknown_10, unknown_14, flag_18, unknown_1a],
        "trailer_offset": trailer_offset,
        "bone_count": bone_count,
        "masks": list(masks),
        "channels": channels,
        "bytes_used": offset,
        "size": len(data),
    }


def hermite(keys, frame: float) -> float:
    """Evaluate a channel. Tangents are per-frame slopes (assumed)."""
    if frame <= keys[0][0]:
        return keys[0][1]
    for (f0, v0, _, out0), (f1, v1, in1, _) in zip(keys, keys[1:]):
        if frame <= f1:
            span = f1 - f0
            t = (frame - f0) / span
            if out0 is None or in1 is None:          # linear (type 2)
                return v0 + (v1 - v0) * t
            t2, t3 = t * t, t * t * t
            return ((2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * span * out0 +
                    (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * span * in1)
    return keys[-1][1]
