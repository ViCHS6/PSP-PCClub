#!/usr/bin/env python3
"""PSF1/PSF2/BDF -> indexed PNG + FNT1. Python 3.11+, Pillow."""

import argparse
from dataclasses import dataclass
import gzip
import json
from pathlib import Path
import re
import struct
import sys

from PIL import Image


CHARSET = [
    *range(0x20, 0x7F), *range(0x410, 0x450), 0x401, 0x451,
    *map(ord, "№«»—–…·°×←↑→↓„“”‘’█░₽"),
]
ROOT = Path(__file__).resolve().parents[1]
HEADER = struct.Struct("<4sHHHHBBBBHH12s")
GLYPH = struct.Struct("<IHHBBbbB3x")


@dataclass
class Font:
    width: int
    height: int
    baseline: int
    glyphs: dict[int, Image.Image]


def scalar(cp):
    return 0 <= cp <= 0x10FFFF and not 0xD800 <= cp <= 0xDFFF


def bitmap(data, width, height):
    stride = (width + 7) // 8
    if len(data) != stride * height:
        raise ValueError("invalid bitmap length")
    result = Image.new("L", (width, height))
    result.putdata([int(bool(data[y * stride + x // 8] & (0x80 >> (x % 8))))
                    for y in range(height) for x in range(width)])
    return result


def read_psf(data):
    if data[:2] == b"\x36\x04":
        if len(data) < 4:
            raise ValueError("truncated PSF1 header")
        mode, height = data[2:4]
        if mode & ~7:
            raise ValueError("unsupported PSF1 mode")
        width, count, offset, size = 8, 512 if mode & 1 else 256, 4, height
        unicode_table, psf1 = bool(mode & 6), True
    elif data[:4] == b"\x72\xb5\x4a\x86":
        if len(data) < 32:
            raise ValueError("truncated PSF2 header")
        _, version, offset, flags, count, size, height, width = struct.unpack_from("<8I", data)
        if version != 0 or offset < 32 or flags & ~1:
            raise ValueError("unsupported PSF2 header")
        unicode_table, psf1 = bool(flags & 1), False
    else:
        raise ValueError("unknown PSF magic")
    if not (1 <= width <= 255 and 1 <= height <= 127 and 1 <= count <= 0x100000):
        raise ValueError("invalid PSF dimensions/count")
    row_bytes = (width + 7) // 8
    if size < row_bytes * height or offset + count * size > len(data):
        raise ValueError("truncated PSF bitmap data")
    images = [bitmap(data[offset + i * size:offset + i * size + row_bytes * height],
                     width, height) for i in range(count)]
    glyphs = {}
    if not unicode_table:
        glyphs = {i: im for i, im in enumerate(images) if scalar(i)}
    else:
        pos = offset + count * size
        for im in images:
            sequence = False
            while True:
                if psf1:
                    if pos + 2 > len(data):
                        raise ValueError("truncated PSF1 Unicode table")
                    cp, = struct.unpack_from("<H", data, pos)
                    pos += 2
                    if cp == 0xFFFF:
                        break
                    if cp == 0xFFFE:
                        sequence = True
                        continue
                    codes = [cp]
                else:
                    if pos >= len(data):
                        raise ValueError("truncated PSF2 Unicode table")
                    marker = data[pos]
                    if marker == 0xFF:
                        pos += 1
                        break
                    if marker == 0xFE:
                        pos += 1
                        sequence = True
                        continue
                    end = pos
                    while end < len(data) and data[end] not in (0xFE, 0xFF):
                        end += 1
                    codes = list(map(ord, data[pos:end].decode("utf-8")))
                    pos = end
                for cp in codes:
                    if not scalar(cp):
                        raise ValueError("invalid Unicode scalar in PSF")
                    if not sequence:
                        glyphs.setdefault(cp, im)
        if pos != len(data):
            raise ValueError("trailing PSF Unicode data")
    return Font(width, height, max(0, height - 2), glyphs)


def read_bdf(data):
    lines = data.decode("ascii").splitlines()
    bbox = None
    ascent = descent = None
    declared = None
    records = []
    current = None
    rows = None
    for raw in lines:
        line = raw.strip()
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key == "STARTCHAR":
            if current is not None:
                raise ValueError("nested BDF STARTCHAR")
            current, rows = {}, None
        elif key == "ENDCHAR":
            if current is None or not {"cp", "bbox", "rows"} <= current.keys():
                raise ValueError("incomplete BDF glyph")
            records.append(current)
            current, rows = None, None
        elif rows is not None:
            rows.append(line)
        elif current is not None:
            if key == "ENCODING":
                current["cp"] = int(parts[2] if parts[1] == "-1" and len(parts) > 2 else parts[1])
            elif key == "BBX":
                current["bbox"] = tuple(map(int, parts[1:]))
            elif key == "BITMAP":
                rows = []
                current["rows"] = rows
        elif key == "FONTBOUNDINGBOX":
            bbox = tuple(map(int, parts[1:]))
        elif key == "FONT_ASCENT":
            ascent = int(parts[1])
        elif key == "FONT_DESCENT":
            descent = int(parts[1])
        elif key == "CHARS":
            declared = int(parts[1])
    if current is not None or bbox is None or len(bbox) != 4 or declared != len(records):
        raise ValueError("incomplete BDF font / CHARS mismatch")
    width, box_height, box_x, box_y = bbox
    if (ascent is None) != (descent is None):
        raise ValueError("BDF needs both FONT_ASCENT and FONT_DESCENT")
    height = box_height if ascent is None else ascent + descent
    baseline = box_height + box_y if ascent is None else ascent
    if not (1 <= width <= 255 and 1 <= height <= 127 and 0 <= baseline <= height):
        raise ValueError("invalid BDF cell")
    glyphs = {}
    for record in records:
        cp = record["cp"]
        if cp == -1:
            continue
        if not scalar(cp) or cp in glyphs or len(record["bbox"]) != 4:
            raise ValueError("invalid/duplicate BDF encoding or BBX")
        w, h, bx, by = record["bbox"]
        x, y = bx - box_x, baseline - by - h
        if w < 0 or h < 0 or x < 0 or y < 0 or x + w > width or y + h > height:
            raise ValueError(f"BDF U+{cp:04X} extends outside cell")
        if len(record["rows"]) != h:
            raise ValueError("BDF bitmap row count mismatch")
        if any(len(row) != 2 * ((w + 7) // 8) for row in record["rows"]):
            raise ValueError("BDF bitmap row width mismatch")
        im = Image.new("L", (width, height))
        if w and h:
            im.paste(bitmap(bytes.fromhex("".join(record["rows"])), w, h), (x, y))
        glyphs[cp] = im
    return Font(width, height, baseline, glyphs)


def read_font(path):
    data = Path(path).read_bytes()
    if data.startswith(b"\x1f\x8b"):
        data = gzip.decompress(data)
    return read_bdf(data) if data.startswith(b"STARTFONT ") else read_psf(data)


def read_extra(path, width, height, style="regular"):
    if path is None:
        return {}
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    result, seen = {}, set()
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith(";"):
            continue
        match = re.fullmatch(r"\[(\d+)x(\d+) (regular|bold) U\+([0-9A-Fa-f]{4,6})\]", line)
        if not match:
            raise ValueError(f"invalid extra glyph header at line {i}")
        w, h, variant, cp = match.groups()
        w, h, cp = int(w), int(h), int(cp, 16)
        key = (w, h, variant, cp)
        if not (1 <= w <= 255 and 1 <= h <= 127 and scalar(cp)) or key in seen:
            raise ValueError(f"invalid/duplicate extra glyph at line {i}")
        seen.add(key)
        rows = lines[i:i + h]
        if len(rows) != h or any(len(row) != w or set(row) - {"#", "."} for row in rows):
            raise ValueError(f"invalid extra bitmap at line {i + 1}")
        i += h
        if (w, h, variant) == (width, height, style):
            im = Image.new("L", (w, h))
            im.putdata([int(c == "#") for row in rows for c in row])
            result[cp] = im
    return result


def power_two(value):
    return 1 << (max(1, value) - 1).bit_length()


def pack_shelves(images):
    candidates = []
    for limit in (8, 16, 32, 64, 128, 256):
        x = y = 1
        shelf = used_x = 0
        positions = []
        for im in images:
            if im is None:
                positions.append((0, 0))
                continue
            w, h = im.size
            if w + 2 > limit:
                break
            if x + w + 1 > limit:
                x, y, shelf = 1, y + shelf + 1, 0
            positions.append((x, y))
            used_x = max(used_x, x + w + 1)
            x += w + 1
            shelf = max(shelf, h)
        else:
            w, h = max(8, power_two(used_x)), max(8, power_two(y + shelf + 1))
            if h <= 256:
                candidates.append((w * h, max(w, h), w, h, positions))
    if not candidates:
        raise ValueError("atlas exceeds 256x256")
    _, _, w, h, positions = min(candidates)
    return w, h, positions


def generate(src, output, mode="prop", space=None, tabular_digits=True,
             extra=ROOT / "assets/fonts/extra_glyphs.txt", style="regular",
             baseline=None, charset=None):
    if mode not in ("mono", "prop") or style not in ("regular", "bold"):
        raise ValueError("invalid mode/style")
    output = Path(output)
    name = output.name
    if not re.fullmatch(r"[A-Za-z0-9_]{1,11}", name):
        raise ValueError("atlas id must contain 1..11 ASCII letters/digits/underscores")
    font = read_font(src)
    if baseline is not None:
        font.baseline = baseline
    if not 0 <= font.baseline <= font.height:
        raise ValueError("baseline must be inside the line")
    space = (font.width + 1) // 2 if space is None else space
    if not 1 <= space <= 255:
        raise ValueError("space must be in 1..255")
    if mode == "mono":
        space = font.width
    codes = sorted(set(CHARSET if charset is None else charset))
    if not codes or len(codes) > 65535 or any(not scalar(cp) for cp in codes) or ord("?") not in codes:
        raise ValueError("charset must contain '?' and valid Unicode scalars (<=65535 glyphs)")
    extras = read_extra(extra, font.width, font.height, style)
    missing = [cp for cp in codes if cp not in font.glyphs and cp not in extras]
    if missing:
        raise ValueError("missing glyphs: " + ", ".join(f"U+{cp:04X}" for cp in missing))
    images, metrics = [], []
    for cp in codes:
        im = font.glyphs.get(cp, extras.get(cp))
        bbox = im.getbbox()
        if cp == 0x20 or bbox is None:
            images.append(None)
            metrics.append((0, 0, 0, 0, space))
        elif mode == "mono" or (tabular_digits and ord("0") <= cp <= ord("9")):
            images.append(im)
            metrics.append((font.width, font.height, 0, 0, font.width))
        else:
            left, _, right, _ = bbox
            w = right - left
            if w + 1 > 255:
                raise ValueError("proportional advance exceeds uint8")
            images.append(im.crop((left, 0, right, font.height)))
            metrics.append((w, font.height, 0, 0, w + 1))
    w, h, positions = pack_shelves(images)
    atlas = Image.new("P", (w, h), 0)
    atlas.putpalette([0, 0, 0, 255, 255, 255] + [0, 0, 0] * 14)
    atlas.info["transparency"] = bytes([0, 255] + [0] * 14)
    data = bytearray(HEADER.pack(b"FNT1", 1, int(mode == "mono"), len(codes), codes.index(ord("?")),
                                 font.height, font.baseline, font.width, space, w, h,
                                 name.encode("ascii") + b"\0"))
    for cp, im, (x, y), (gw, gh, xo, yo, advance) in zip(codes, images, positions, metrics):
        if im is not None:
            atlas.paste(im, (x, y))
        data.extend(GLYPH.pack(cp, x, y, gw, gh, xo, yo, advance))
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output.with_suffix(".png"), bits=4, optimize=False, compress_level=9)
    output.with_suffix(".fnt").write_bytes(data)
    return w, h, len(codes)


def preview(metrics_path, output):
    # Читаем готовую пару файлов: исходный шрифт здесь не используется.
    metrics_path = Path(metrics_path)
    data = metrics_path.read_bytes()
    header = HEADER.unpack_from(data)
    magic, version, _, count, fallback, height, _, _, _, aw, ah, name = header
    if magic != b"FNT1" or version != 1 or len(data) != HEADER.size + count * GLYPH.size:
        raise ValueError("invalid FNT1 preview input")
    records = [GLYPH.unpack_from(data, HEADER.size + i * GLYPH.size) for i in range(count)]
    lookup = {record[0]: record for record in records}
    with Image.open(metrics_path.parent / (name.split(b"\0")[0].decode("ascii") + ".png")) as image:
        if image.size != (aw, ah):
            raise ValueError("FNT1/PNG dimensions differ")
        atlas = image.convert("RGBA")
    lines = ["Съешь же ещё этих мягких французских булок, да выпей чаю.",
             "СЪЕШЬ ЖЕ ЕЩЁ ЭТИХ МЯГКИХ ФРАНЦУЗСКИХ БУЛОК",
             "0123456789 — 1 500 ₽ № «Клуб» …",
             "".join(map(chr, range(0x20, 0x7F))),
             "№ « » — – … · ° × ← ↑ → ↓ „ “ ” ‘ ’ █ ░ ₽"]
    resolved = [[lookup.get(ord(c), records[fallback]) for c in line] for line in lines]
    width = max(sum(record[7] for record in line) for line in resolved)
    canvas = Image.new("RGBA", (width + 16, len(lines) * (height + 4) + 12), (24, 28, 36, 255))
    for i, line in enumerate(resolved):
        pen, top = 8, 6 + i * (height + 4)
        for _, x, y, w, h, xo, yo, advance in line:
            if w and h:
                canvas.alpha_composite(atlas.crop((x, y, x + w, y + h)), (pen + xo, top + yo))
            pen += advance
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").resize((canvas.width * 3, canvas.height * 3), Image.Resampling.NEAREST).save(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("src", nargs="?", type=Path)
    parser.add_argument("--all", action="store_true", help="build assets/fonts/fonts.json")
    parser.add_argument("--mode", choices=("mono", "prop"), default="prop")
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--space", type=int)
    parser.add_argument("--tabular-digits", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--extra", type=Path, default=ROOT / "assets/fonts/extra_glyphs.txt")
    parser.add_argument("--style", choices=("regular", "bold"), default="regular")
    parser.add_argument("--baseline", type=int, help="PSF has no baseline; default height-2")
    parser.add_argument("--preview", type=Path, help="PNG path; with --all, output directory")
    args = parser.parse_args(argv)
    if args.all and (args.src or args.output):
        parser.error("--all cannot be combined with src/-o")
    if not args.all and (args.src is None or args.output is None):
        parser.error("src and -o are required without --all")
    try:
        if args.all:
            base = ROOT / "assets/fonts"
            entries = json.loads((base / "fonts.json").read_text(encoding="utf-8"))
            if len({entry["id"] for entry in entries}) != len(entries):
                raise ValueError("duplicate font id")
            jobs = [(base / entry["source"], base / entry["id"],
                     {"mode": entry["mode"], **entry.get("options", {}), "extra": args.extra})
                    for entry in entries]
        else:
            jobs = [(args.src, args.output, dict(mode=args.mode, space=args.space,
                     tabular_digits=args.tabular_digits, extra=args.extra,
                     style=args.style, baseline=args.baseline))]
        for src, output, options in jobs:
            w, h, count = generate(src, output, **options)
            print(f"{output.name}: {w}x{h}, {count} glyphs")
            if args.preview:
                dest = args.preview / (output.name + ".png") if args.all else args.preview
                preview(output.with_suffix(".fnt"), dest)
    except (ValueError, OSError, EOFError, KeyError, TypeError, struct.error) as exc:
        parser.exit(1, f"make_font: {exc}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
