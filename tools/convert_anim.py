"""Конвертер *.anim.json (Blender Z-up) в ANM1; контракт docs/formats/anim.md."""

import argparse
import json
import math
from pathlib import Path
import re
import struct
import sys


HEADER = struct.Struct("<4s4H5I")
PART = struct.Struct("<32shH3f")
CLIP = struct.Struct("<32sHHIII")
MAX_BYTES = 64 * 1024


class ConversionError(ValueError):
    pass


def encode(document, source="<memory>"):
    """Полная проверка до записи вывода; результат — детерминированные bytes."""
    def fail(field, message):
        raise ConversionError(f"{source}: {field}: {message}")

    def fields(value, expected, field):
        if not isinstance(value, dict):
            fail(field, "expected object")
        missing, extra = set(expected) - value.keys(), value.keys() - set(expected)
        if missing:
            fail(field, f"missing fields: {', '.join(sorted(missing))}")
        if extra:
            fail(field, f"unknown fields: {', '.join(sorted(extra))}")

    def name(value, field):
        if not isinstance(value, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,30}", value):
            fail(field, "expected snake_case ASCII id, 1..31 bytes")
        return value.encode("ascii")

    def vector(value, count, field, limit=None):
        if not isinstance(value, list) or len(value) != count:
            fail(field, f"expected array of {count} numbers")
        for i, v in enumerate(value):
            if isinstance(v, bool) or not isinstance(v, (int, float)):
                fail(f"{field}[{i}]", "expected finite number")
            try:
                finite = math.isfinite(v)
            except OverflowError:
                finite = False
            if not finite or (limit is not None and abs(v) > limit):
                fail(f"{field}[{i}]", f"expected finite number with abs <= {limit}")
        return value

    def xyz(value, field):
        x, y, z = vector(value, 3, field, 10)
        return x, z, -y

    fields(document, ("version", "coordinate_system", "parts", "clips"), "$")
    if type(document["version"]) is not int or document["version"] != 1:
        fail("version", "expected 1")
    if document["coordinate_system"] != "blender_z_up":
        fail("coordinate_system", "expected blender_z_up")
    parts, clips = document["parts"], document["clips"]
    if not isinstance(parts, list) or not 1 <= len(parts) <= 11:
        fail("parts", "expected 1..11 parts")
    if not isinstance(clips, list) or not 1 <= len(clips) <= 16:
        fail("clips", "expected 1..16 clips")
    indices = {}
    for i, p in enumerate(parts):
        field = f"parts[{i}]"
        fields(p, ("name", "parent", "offset"), field)
        name(p["name"], field + ".name")
        if p["name"] in indices:
            fail(field + ".name", "duplicate part")
        indices[p["name"]] = i
    parents = []
    for i, p in enumerate(parts):
        parent = p["parent"]
        if parent is not None and (not isinstance(parent, str) or parent not in indices):
            fail(f"parts[{i}].parent", "unknown/missing part")
        parents.append(-1 if parent is None else indices[parent])
    for i in range(len(parts)):
        visited, current = set(), i
        while current != -1:
            if current in visited:
                fail(f"parts[{i}].parent", "cycle in hierarchy")
            visited.add(current)
            current = parents[current]
    part_data = bytearray()
    for i, p in enumerate(parts):
        if (i == 0 and parents[i] != -1) or (i > 0 and not 0 <= parents[i] < i):
            fail(f"parts[{i}].parent", "single root at index 0; parents must precede children")
        part_data.extend(PART.pack(name(p["name"], f"parts[{i}].name"), parents[i], 0,
                                   *xyz(p["offset"], f"parts[{i}].offset")))
    keys_offset = HEADER.size + len(part_data) + CLIP.size * len(clips)
    table, keys, clip_names = bytearray(), bytearray(), set()
    for ci, clip in enumerate(clips):
        field = f"clips[{ci}]"
        fields(clip, ("name", "fps", "loop", "frames"), field)
        clip_name = name(clip["name"], field + ".name")
        if clip_name in clip_names:
            fail(field + ".name", "duplicate clip")
        clip_names.add(clip_name)
        if type(clip["fps"]) is not int or clip["fps"] != 15:
            fail(field + ".fps", "expected 15 frames/s")
        if type(clip["loop"]) is not bool:
            fail(field + ".loop", "expected boolean")
        frames = clip["frames"]
        if not isinstance(frames, list) or not 1 <= len(frames) <= 65535:
            fail(field + ".frames", "expected 1..65535 frames")
        end = keys_offset + len(keys) + len(frames) * (12 + len(parts) * 8)
        if ((end + 15) & ~15) > MAX_BYTES:
            fail(field + ".frames", "ANM1 set exceeds 65536 bytes")
        table.extend(CLIP.pack(clip_name, len(frames), 15, int(clip["loop"]),
                               keys_offset + len(keys), 0))
        for fi, frame in enumerate(frames):
            ff = f"{field}.frames[{fi}]"
            fields(frame, ("root", "rotations"), ff)
            keys.extend(struct.pack("<3f", *xyz(frame["root"], ff + ".root")))
            fields(frame["rotations"], indices, ff + ".rotations")
            for p in parts:
                qf = ff + ".rotations." + p["name"]
                q = vector(frame["rotations"][p["name"]], 4, qf, 1.001)
                norm = sum(v * v for v in q)
                if abs(norm - 1) > 0.001:
                    fail(qf, "quaternion squared norm must be within 0.001 of 1")
                x, y, z, w = (v / math.sqrt(norm) for v in q)
                keys.extend(struct.pack("<4h", *(round(v * 32767) for v in (x, z, -y, w))))
        keys.extend(bytes((-len(keys)) % 16))
    size = keys_offset + len(keys)
    return (HEADER.pack(b"ANM1", 1, 0, len(parts), len(clips), 32,
                        32 + len(part_data), keys_offset, size, 0) + part_data + table + keys)


def convert(source, output, report=False):
    def object_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ConversionError(f"{source}: JSON.{key}: duplicate object key")
            result[key] = value
        return result

    try:
        document = json.loads(source.read_text(encoding="utf-8-sig"), object_pairs_hook=object_pairs)
    except json.JSONDecodeError as error:
        raise ConversionError(f"{source}:{error.lineno}:{error.colno}: JSON: {error.msg}") from error
    except RecursionError as error:
        raise ConversionError(f"{source}: $: JSON nesting is too deep") from error
    blob = encode(document, source)
    output.write_bytes(blob)
    if report:
        print(f"{output}: parts={len(document['parts'])}, clips={len(document['clips'])}, bytes={len(blob)}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args(argv)
    try:
        convert(args.input, args.output, args.report)
    except (ConversionError, OSError, UnicodeError) as error:
        message = str(error) if isinstance(error, ConversionError) else f"{args.input}: {error}"
        print(f"error: {message}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
