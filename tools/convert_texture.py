#!/usr/bin/env python3
"""PNG -> TEX1 для PSP и PC. Контракт: docs/formats/texture.md."""

import argparse
from pathlib import Path
import struct
import sys

from PIL import Image


HEADER = struct.Struct("<4sHBB6HIII")
FORMATS = {"5650": 0, "5551": 1, "4444": 2, "8888": 3, "t4": 4, "t8": 5}
BITS = (16, 16, 16, 32, 4, 8)


class ConversionError(ValueError):
    pass


def rgba_pixels(image):
    raw = image.convert("RGBA").tobytes()
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def palette_colors(path, count, transparent):
    with Image.open(path) as image:
        if image.format != "PNG" or image.mode != "P":
            raise ConversionError(f"{path}: --palette requires an indexed PNG")
        raw = image.getpalette("RGB")
        if not raw or len(raw) // 3 > count:
            raise ConversionError(f"{path}: palette must contain at most {count} entries")
        alpha = image.info.get("transparency")
        colors = []
        for i in range(len(raw) // 3):
            a = (0 if i == alpha else 255) if isinstance(alpha, int) else (
                alpha[i] if isinstance(alpha, bytes) and i < len(alpha) else 255)
            if a < 128:
                if i != 0:
                    raise ConversionError(f"{path}: only palette entry 0 may be transparent")
                colors.append((0, 0, 0, 0))
            else:
                colors.append((*raw[3 * i:3 * i + 3], 255))
        if transparent and colors[0][3] != 0:
            raise ConversionError(f"{path}: transparent input requires transparent palette entry 0")
        return colors


def indexed_pixels(image, pixels, count, palette_path, dither):
    transparent = any(pixel[3] < 128 for pixel in pixels)
    if palette_path is not None:
        colors = palette_colors(palette_path, count, transparent)
    else:
        opaque = bytes(channel for pixel in pixels if pixel[3] >= 128 for channel in pixel[:3])
        colors = [(0, 0, 0, 0)] if transparent else []
        if opaque:
            sample = Image.frombytes("RGB", (len(opaque) // 3, 1), opaque)
            quantized = sample.quantize(colors=count - len(colors), method=Image.Quantize.MEDIANCUT,
                                       dither=Image.Dither.NONE)
            palette = quantized.getpalette("RGB")
            used = sorted(set(quantized.tobytes()))
            colors.extend((*palette[i * 3:i * 3 + 3], 255) for i in used)

    candidates = [(i, color[:3]) for i, color in enumerate(colors) if color[3] == 255]
    if not candidates:
        if any(pixel[3] >= 128 for pixel in pixels):
            raise ConversionError("palette has no opaque colors")
        indices = bytes(len(pixels))
    else:
        # Pillow требует 256 RGB-записей; дубликаты не добавляют новых цветов.
        mapping = Image.new("P", (1, 1))
        entries = [rgb for _, rgb in candidates]
        entries.extend([entries[-1]] * (256 - len(entries)))
        mapping.putpalette([value for rgb in entries for value in rgb])
        lookup = {}
        for index, rgb in candidates:
            lookup.setdefault(rgb, index)
        translation = bytes(lookup[rgb] for rgb in entries)
        rgb_image = image.convert("RGB")
        if transparent:
            # Скрытый RGB не должен влиять на распространение ошибки дизеринга.
            replacement = candidates[0][1]
            rgb_image = Image.frombytes("RGB", image.size, bytes(
                c for pixel in pixels for c in (pixel[:3] if pixel[3] >= 128 else replacement)))
        quantized = rgb_image.quantize(palette=mapping, dither=(
            Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE))
        indices = bytes(0 if pixel[3] < 128 else translation[q]
                        for pixel, q in zip(pixels, quantized.tobytes()))
    clut = b"".join(bytes(color) for color in colors)
    return indices, clut + bytes(count * 4 - len(clut))


def swizzle(linear, stride_bytes, height):
    result = bytearray(len(linear))
    for y in range(height):
        for x in range(0, stride_bytes, 16):
            block = x // 16 + (y // 8) * (stride_bytes // 16)
            offset = block * 128 + (y % 8) * 16
            result[offset:offset + 16] = linear[y * stride_bytes + x:y * stride_bytes + x + 16]
    return result


def convert(source, output, format_name="t4", frames=1, palette=None,
            dither=False, no_swizzle=False, report=False):
    source, output = Path(source), Path(output)
    if format_name not in FORMATS:
        raise ConversionError(f"unknown texture format {format_name!r}")
    if not 1 <= frames <= 65535:
        raise ConversionError("--frames must be in 1..65535")
    with Image.open(source) as original:
        if original.format != "PNG":
            raise ConversionError(f"{source}: expected PNG")
        if original.height % frames:
            raise ConversionError(f"{source}: strip height must be divisible by --frames")
        width, height = original.width, original.height // frames
        if any(n < 8 or n > 512 or n & (n - 1) for n in (width, height)):
            raise ConversionError(f"{source}: frame width and height must be powers of two in 8..512")
        image = original.convert("RGBA")
    if max(width, height) > 256:
        print(f"warning: {source}: {width}x{height} exceeds the recommended 256", file=sys.stderr)
    fmt = FORMATS[format_name]
    if palette is not None and fmt < 4:
        raise ConversionError("--palette is only supported for t4/t8")
    if dither and fmt < 4:
        raise ConversionError("--dither is only supported for t4/t8")
    pixels = rgba_pixels(image)
    transparent = any(pixel[3] < 128 for pixel in pixels)
    if transparent and fmt == 0:
        raise ConversionError(f"{source}: 5650 has no alpha; use 5551, 4444, 8888, t4 or t8")
    clut_count = (16 if fmt == 4 else 256) if fmt >= 4 else 0
    indices, clut = indexed_pixels(image, pixels, clut_count, palette, dither) if clut_count else (None, b"")
    stride_bytes = ((width * BITS[fmt] + 7) // 8 + 15) & ~15
    stride = stride_bytes * 8 // BITS[fmt]
    stored_height = (height + 7) & ~7
    frame_bytes = stride_bytes * stored_height
    clut_offset = HEADER.size if clut else 0
    pixels_offset = HEADER.size + len(clut)
    flags = (0 if no_swizzle else 1) | (2 if transparent else 0)
    header = HEADER.pack(b"TEX1", 1, fmt, flags, width, height, stride, frames,
                         clut_count, 0, clut_offset, pixels_offset, frame_bytes)
    with output.open("wb") as target:
        target.write(header)
        target.write(clut)
        for frame in range(frames):
            linear = bytearray(frame_bytes)
            for y in range(height):
                for x in range(width):
                    i = (frame * height + y) * width + x
                    offset = y * stride_bytes + x * BITS[fmt] // 8
                    if fmt == 4:
                        linear[offset] |= indices[i] << (4 * (x % 2))
                    elif fmt == 5:
                        linear[offset] = indices[i]
                    else:
                        r, g, b, a = pixels[i]
                        a = 0 if a < 128 else 255
                        if fmt == 3:
                            linear[offset:offset + 4] = bytes((r, g, b, a))
                        else:
                            if fmt == 0:
                                value = (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)
                            elif fmt == 1:
                                value = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15)
                            else:
                                value = (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12)
                            struct.pack_into("<H", linear, offset, value)
            target.write(linear if no_swizzle else swizzle(linear, stride_bytes, stored_height))
    if report:
        print(f"{output}: {width}x{height}, format={format_name}, stride={stride}, "
              f"frames={frames}, frame_bytes={frame_bytes}, clut={clut_count}, "
              f"swizzled={not no_swizzle}, transparent={transparent}, "
              f"bytes={pixels_offset + frames * frame_bytes}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--format", dest="format_name", choices=FORMATS, default="t4")
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--palette", type=Path)
    parser.add_argument("--dither", action="store_true")
    parser.add_argument("--no-swizzle", action="store_true")
    parser.add_argument("--report", action="store_true")
    args = vars(parser.parse_args(argv))
    source = args.pop("input")
    try:
        convert(source, **args)
    except (ConversionError, OSError, ValueError) as error:
        print(f"error: {source}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
