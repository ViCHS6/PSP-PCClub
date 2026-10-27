#!/usr/bin/env python3
"""OBJ (+MTL) -> MSH1. Контракт: docs/formats/mesh.md."""

import argparse
import math
from pathlib import Path
import shlex
import struct
import sys


HEADER = struct.Struct("<4sHHHHIIIII16f")
SUBMESH = struct.Struct("<II32sII")
VERTEX = struct.Struct("<HH4BhhhH")


class ConversionError(ValueError):
    pass


def fail(path, line, message):
    raise ConversionError(f"{path}:{line}: {message}")


def records(path):
    with path.open(encoding="utf-8-sig") as source:
        for number, line in enumerate(source, 1):
            lexer = shlex.shlex(line, posix=True)
            lexer.whitespace_split = True
            lexer.escape = ""
            try:
                fields = list(lexer)
            except ValueError as error:
                fail(path, number, str(error))
            if fields:
                yield number, fields[0], fields[1:]


def numbers(fields, path, line):
    try:
        values = tuple(float(item) for item in fields)
    except ValueError:
        fail(path, line, "expected numeric values")
    if not all(math.isfinite(value) for value in values):
        fail(path, line, "NaN and infinity are not supported")
    return values


def read_mtl(path):
    materials = {}
    current = None
    for line, command, fields in records(path):
        if command == "newmtl":
            if not fields:
                fail(path, line, "newmtl needs a name")
            current = " ".join(fields)
            materials[current] = {"kd": (1.0, 1.0, 1.0), "texture": b""}
        elif command in ("Kd", "map_Kd"):
            if current is None:
                fail(path, line, f"{command} before newmtl")
            if command == "Kd":
                values = numbers(fields, path, line)
                if len(values) != 3 or any(v < 0 or v > 1 for v in values):
                    fail(path, line, "Kd needs three values in 0..1")
                materials[current]["kd"] = values
            else:
                if not fields or fields[0].startswith("-"):
                    fail(path, line, "map_Kd needs a filename; bake map options before export")
                name = Path(" ".join(fields).replace("\\", "/")).stem
                try:
                    encoded = name.encode("ascii")
                except UnicodeEncodeError:
                    fail(path, line, "texture id must be ASCII")
                if not encoded or len(encoded) > 31 or b"\0" in encoded:
                    fail(path, line, "texture id must contain 1..31 ASCII bytes")
                materials[current]["texture"] = encoded
    return materials


def reference(token, count, path, line, kind):
    try:
        value = int(token)
    except ValueError:
        fail(path, line, f"invalid {kind} reference: {token!r}")
    index = value - 1 if value > 0 else count + value
    if value == 0 or not 0 <= index < count:
        fail(path, line, f"{kind} reference {token} out of range (count {count})")
    return index


def float32(value):
    result = struct.unpack("<f", struct.pack("<f", value))[0]
    if not math.isfinite(result):
        raise OverflowError("non-finite float32")
    return result


def convert(source, output, report=False):
    source, output = Path(source), Path(output)
    positions, colors, uvs, normals = [], [], [], []
    materials, groups = {}, {}
    material = None
    for line, command, fields in records(source):
        if command == "v":
            values = numbers(fields, source, line)
            if len(values) not in (3, 6):
                fail(source, line, "v needs x y z [r g b]")
            color = values[3:] if len(values) == 6 else (1.0, 1.0, 1.0)
            if any(c < 0 or c > 1 for c in color):
                fail(source, line, "vertex colors must be in 0..1")
            positions.append(values[:3])
            colors.append(color)
        elif command == "vt":
            values = numbers(fields, source, line)
            if len(values) not in (2, 3):
                fail(source, line, "vt needs u v [w]")
            uvs.append((values[0], 1.0 - values[1]))
        elif command == "vn":
            values = numbers(fields, source, line)
            if len(values) != 3:
                fail(source, line, "vn needs x y z")
            normals.append(values)
        elif command == "mtllib":
            if not fields:
                fail(source, line, "mtllib needs a filename")
            for filename in fields:
                try:
                    materials.update(read_mtl(source.parent / filename.replace("\\", "/")))
                except (OSError, ConversionError) as error:
                    fail(source, line, f"cannot read MTL {filename!r}: {error}")
        elif command == "usemtl":
            if not fields:
                fail(source, line, "usemtl needs a name")
            material = " ".join(fields)
            groups.setdefault(material, [])
        elif command == "f":
            if len(fields) < 3:
                fail(source, line, "face needs at least three vertices")
            face = []
            for item in fields:
                parts = item.split("/")
                if len(parts) > 3 or not parts[0]:
                    fail(source, line, f"invalid face reference {item!r}")
                position = reference(parts[0], len(positions), source, line, "vertex")
                uv = None
                if len(parts) > 1 and parts[1]:
                    uv = reference(parts[1], len(uvs), source, line, "UV")
                if len(parts) == 3 and parts[2]:
                    reference(parts[2], len(normals), source, line, "normal")
                face.append((position, uv, line))
            target = groups.setdefault(material, [])
            for i in range(1, len(face) - 1):
                target.extend((face[0], face[i], face[i + 1]))
        elif command not in ("o", "g", "s"):
            fail(source, line, f"unsupported OBJ statement {command!r}")

    groups = {key: value for key, value in groups.items() if value}
    if not groups:
        fail(source, 1, "no faces")
    if len(groups) > 65535:
        fail(source, 1, "more than 65535 submeshes")
    for name, refs in groups.items():
        if name is not None and name not in materials:
            fail(source, refs[0][2], f"undefined material {name!r}")
    refs = [ref for group in groups.values() for ref in group]
    bbox_min = tuple(min(positions[p][a] for p, _, _ in refs) for a in range(3))
    bbox_max = tuple(max(positions[p][a] for p, _, _ in refs) for a in range(3))
    uv_values = [uvs[t] if t is not None else (0.0, 0.0) for _, t, _ in refs]
    uv_min = tuple(min(uv[a] for uv in uv_values) for a in range(2))
    uv_max = tuple(max(uv[a] for uv in uv_values) for a in range(2))
    try:
        pos_offset = tuple(float32(lo / 2 + hi / 2) for lo, hi in zip(bbox_min, bbox_max))
        pos_scale = tuple(float32((hi / 2 - lo / 2) or 1.0) for lo, hi in zip(bbox_min, bbox_max))
        uv_offset = tuple(map(float32, uv_min))
        uv_scale = tuple(float32((hi - lo) * (32768 / 65535) or 1.0)
                         for lo, hi in zip(uv_min, uv_max))
        bounds = tuple(map(float32, bbox_min + bbox_max))
    except (OverflowError, struct.error):
        fail(source, refs[0][2], "coordinates cannot be represented as float32")
    if any(s <= 0 for s in pos_scale + uv_scale):
        fail(source, refs[0][2], "coordinate range is too small for float32")

    vertices, indices, subsets, seen = bytearray(), [], bytearray(), {}
    for name, group in groups.items():
        mat = materials[name] if name is not None else {"kd": (1, 1, 1), "texture": b""}
        start = len(indices)
        for p, t, line in group:
            uv = uvs[t] if t is not None else (0.0, 0.0)
            q_pos = [max(-32768, min(32767, round((positions[p][a] - pos_offset[a])
                                                / pos_scale[a] * 32768))) for a in range(3)]
            q_uv = [max(0, min(65535, round((uv[a] - uv_offset[a]) / uv_scale[a] * 32768)))
                    for a in range(2)]
            rgb = [round(colors[p][a] * mat["kd"][a] * 255) for a in range(3)]
            vertex = VERTEX.pack(*q_uv, *rgb, 255, *q_pos, 0)
            if vertex not in seen:
                if len(seen) == 65535:
                    fail(source, line, "more than 65535 vertices after quantization and deduplication")
                seen[vertex] = len(seen)
                vertices.extend(vertex)
            indices.append(seen[vertex])
        subsets.extend(SUBMESH.pack(start, len(indices) - start, mat["texture"], 0, 0))
    if len(indices) > 0xFFFFFFFF:
        fail(source, refs[-1][2], "index_count exceeds uint32")
    vertices_offset = (HEADER.size + len(subsets) + 15) & ~15
    indices_offset = vertices_offset + len(vertices)
    end = indices_offset + len(indices) * 2
    if end > 0xFFFFFFFF:
        fail(source, refs[-1][2], "file offsets exceed uint32")
    header = HEADER.pack(b"MSH1", 1, 0, len(seen), len(groups), len(indices),
                         vertices_offset, indices_offset, HEADER.size, 0,
                         *pos_offset, *pos_scale, *uv_offset, *uv_scale, *bounds)
    with output.open("wb") as target:
        target.write(header)
        target.write(subsets)
        target.write(bytes(vertices_offset - HEADER.size - len(subsets)))
        target.write(vertices)
        target.write(struct.pack(f"<{len(indices)}H", *indices))
        target.write(bytes((-end) % 4))
    if report:
        print(f"{output}: vertices={len(seen)}, triangles={len(indices) // 3}, "
              f"submeshes={len(groups)}, AABB={bbox_min}..{bbox_max}")


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
