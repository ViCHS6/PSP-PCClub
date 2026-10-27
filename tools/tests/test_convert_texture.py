"""Проверки TEX1 по смещениям, битам GU и независимому обратному swizzle."""

import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


SCRIPT = Path(__file__).resolve().parents[1] / "convert_texture.py"
BITS = (16, 16, 16, 32, 4, 8)


def parse_texture(blob):
    names = ("magic version format flags width height stride frame_count clut_count reserved0 "
             "clut_offset pixels_offset frame_bytes").split()
    return dict(zip(names, struct.unpack_from("<4sHBB6HIII", blob)))


def unswizzle(blob, header, frame=0):
    row_bytes = header["stride"] * BITS[header["format"]] // 8
    data = blob[header["pixels_offset"] + frame * header["frame_bytes"]:
                header["pixels_offset"] + (frame + 1) * header["frame_bytes"]]
    if not header["flags"] & 1:
        return data
    result = bytearray(len(data))
    for y in range((header["height"] + 7) & ~7):
        for x in range(row_bytes):
            block = x // 16 + (y // 8) * (row_bytes // 16)
            result[y * row_bytes + x] = data[block * 128 + (y % 8) * 16 + x % 16]
    return bytes(result)


class TextureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_texture(self, image, *options, success=True):
        source, output = self.root / "source.png", self.root / "result.tex"
        image.save(source)
        process = subprocess.run([sys.executable, "-B", str(SCRIPT), str(source), "-o", str(output),
                                  "--report", *map(str, options)], capture_output=True, text=True,
                                 env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        if not success:
            self.assertNotEqual(process.returncode, 0)
            self.assertNotIn("Traceback", process.stderr)
            return process.stderr
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("frame_bytes=", process.stdout)
        blob = output.read_bytes()
        header = parse_texture(blob)
        self.assertEqual((header["magic"], header["version"], header["reserved0"]), (b"TEX1", 1, 0))
        self.assertEqual(header["flags"] & ~3, 0)
        self.assertEqual(header["pixels_offset"] % 16, 0)
        self.assertEqual(header["clut_offset"] % 16, 0)
        self.assertEqual(header["frame_bytes"] % 16, 0)
        self.assertEqual(len(blob), header["pixels_offset"] + header["frame_count"] * header["frame_bytes"])
        return blob, header, process.stderr

    def make_palette(self, colors):
        image = Image.new("P", (8, 8))
        image.putpalette([v for color in colors for v in color[:3]])
        image.info["transparency"] = bytes(color[3] for color in colors)
        path = self.root / "palette.png"
        image.save(path)
        return path

    def test_swizzle_roundtrip_all_formats(self):
        image = Image.frombytes("RGBA", (32, 16), bytes(
            v for y in range(16) for x in range(32) for v in (x * 8, y * 16, (x + y) * 5, 255)))
        for value, fmt in enumerate(("5650", "5551", "4444", "8888", "t4", "t8")):
            with self.subTest(format=fmt):
                blob, header, _ = self.run_texture(image, "--format", fmt)
                linear, plain, _ = self.run_texture(image, "--format", fmt, "--no-swizzle")
                self.assertEqual(header["flags"], 1)
                self.assertEqual(header["format"], value)
                self.assertEqual(plain["flags"], 0)
                self.assertEqual(unswizzle(blob, header), linear[plain["pixels_offset"]:])
                if fmt == "8888":
                    self.assertEqual(unswizzle(blob, header), image.tobytes())

    def test_swizzle_known_block_order(self):
        image = Image.frombytes("RGBA", (8, 16), bytes(
            v for y in range(16) for x in range(8) for v in (x, y, 0, 255)))
        blob, header, _ = self.run_texture(image, "--format", "8888")
        data = blob[32:]
        self.assertEqual(header["frame_bytes"], 512)
        self.assertEqual(data[:16], bytes((0, 0, 0, 255, 1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255)))
        self.assertEqual(data[16:20], bytes((0, 1, 0, 255)))
        self.assertEqual(data[112:116], bytes((0, 7, 0, 255)))
        self.assertEqual(data[128:132], bytes((4, 0, 0, 255)))
        self.assertEqual(data[256:260], bytes((0, 8, 0, 255)))
        self.assertEqual(data[384:388], bytes((4, 8, 0, 255)))

    def test_t4_nibbles_narrow_stride_and_fixed_palette(self):
        colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)]
        palette = self.make_palette(colors)
        image = Image.frombytes("RGBA", (8, 8), bytes(v for i in range(64) for v in colors[i % 3]))
        blob, header, _ = self.run_texture(image, "--palette", palette)
        self.assertEqual(header["format"], 4)
        self.assertEqual(header["stride"], 32)
        self.assertEqual((header["clut_count"], header["clut_offset"], header["pixels_offset"]), (16, 32, 96))
        self.assertEqual(header["frame_bytes"], 128)
        self.assertEqual(blob[32:44], bytes(v for color in colors for v in color))
        self.assertEqual(blob[44:96], bytes(52))
        data = unswizzle(blob, header)
        self.assertEqual(data[:4], bytes((0x10, 0x02, 0x21, 0x10)))
        for y in range(8):
            self.assertEqual(data[y * 16 + 4:(y + 1) * 16], bytes(12))

    def test_transparency_threshold_and_index_zero(self):
        image = Image.frombytes("RGBA", (8, 8), bytes(
            v for i in range(64) for v in ((255, 0, 99, 127) if i % 2 == 0 else (12, 200, 34, 128))))
        for fmt, count in (("t4", 16), ("t8", 256)):
            with self.subTest(format=fmt):
                blob, header, _ = self.run_texture(image, "--format", fmt)
                self.assertEqual(header["flags"], 3)
                self.assertEqual(header["clut_count"], count)
                self.assertEqual(blob[32:36], bytes(4))
                data = unswizzle(blob, header)
                row_bytes = header["stride"] * BITS[header["format"]] // 8
                for y in range(8):
                    for x in range(8):
                        index = ((data[y * row_bytes + x // 2] >> (4 * (x % 2))) & 15
                                 if fmt == "t4" else data[y * row_bytes + x])
                        if x % 2 == 0:
                            self.assertEqual(index, 0)
                        else:
                            self.assertGreater(index, 0)
                            self.assertEqual(blob[32 + index * 4:36 + index * 4], bytes((12, 200, 34, 255)))

    def test_all_transparent_and_opaque_black(self):
        for fmt in ("t4", "t8"):
            blob, header, _ = self.run_texture(Image.new("RGBA", (8, 8), (255, 123, 64, 0)), "--format", fmt)
            self.assertEqual(blob[32:header["pixels_offset"]], bytes(header["clut_count"] * 4))
            self.assertEqual(unswizzle(blob, header), bytes(header["frame_bytes"]))
        blob, header, _ = self.run_texture(Image.new("RGBA", (8, 8), (0, 0, 0, 255)))
        self.assertEqual(header["flags"], 1)
        self.assertEqual(blob[32:36], bytes((0, 0, 0, 255)))

    def test_frames_share_palette_and_swizzle_independently(self):
        image = Image.new("RGBA", (8, 16), (255, 0, 0, 255))
        image.paste((0, 0, 255, 255), (0, 8, 8, 16))
        for fmt in ("t4", "t8", "8888"):
            blob, header, _ = self.run_texture(image, "--format", fmt, "--frames", 2)
            self.assertEqual((header["width"], header["height"], header["frame_count"]), (8, 8, 2))
            for frame, expected in enumerate((bytes((255, 0, 0, 255)), bytes((0, 0, 255, 255)))):
                data = unswizzle(blob, header, frame)
                if fmt == "8888":
                    self.assertEqual(data, expected * 64)
                else:
                    index = data[0] & 15 if fmt == "t4" else data[0]
                    self.assertEqual(blob[32 + index * 4:36 + index * 4], expected)
                    self.assertEqual(set(data[:4 if fmt == "t4" else 8]), {index * 17 if fmt == "t4" else index})

    def test_direct_gu_bit_layouts_and_rgba_alpha(self):
        colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
                  (255, 255, 255, 255), (129, 65, 33, 255), (0, 0, 0, 255),
                  (255, 0, 0, 127), (0, 255, 0, 128)]
        image = Image.frombytes("RGBA", (8, 8), bytes(v for color in colors * 8 for v in color))
        expected = {"5551": (0x801F, 0x83E0, 0xFC00, 0xFFFF, 0x9110, 0x8000, 0x001F, 0x83E0),
                    "4444": (0xF00F, 0xF0F0, 0xFF00, 0xFFFF, 0xF248, 0xF000, 0x000F, 0xF0F0)}
        for fmt, values in expected.items():
            blob, header, _ = self.run_texture(image, "--format", fmt)
            self.assertEqual(struct.unpack_from("<8H", unswizzle(blob, header)), values)
            self.assertEqual(header["flags"], 3)
        blob, header, _ = self.run_texture(image, "--format", "8888")
        self.assertEqual(unswizzle(blob, header)[:32], bytes(v for r, g, b, a in colors
                                                        for v in (r, g, b, 0 if a < 128 else 255)))
        image.putalpha(255)
        blob, header, _ = self.run_texture(image, "--format", "5650")
        self.assertEqual(struct.unpack_from("<8H", unswizzle(blob, header)),
                         (0x001F, 0x07E0, 0xF800, 0xFFFF, 0x2210, 0x0000, 0x001F, 0x07E0))

    def test_invalid_dimensions_frames_and_5650_transparency(self):
        for size in ((12, 8), (8, 12), (4, 8), (1024, 8)):
            with self.subTest(size=size):
                self.assertIn("powers of two", self.run_texture(Image.new("RGB", size), success=False))
        image = Image.new("RGBA", (8, 8))
        for count in (0, -1, 65536):
            self.assertIn("--frames", self.run_texture(image, "--frames", count, success=False))
        self.assertIn("divisible", self.run_texture(image, "--frames", 3, success=False))
        self.assertIn("no alpha", self.run_texture(image, "--format", "5650", success=False))

    def test_fixed_palette_validation_and_dither(self):
        image = Image.new("RGBA", (8, 8), (100, 100, 100, 255))
        palette = self.make_palette([(0, 0, 0, 255), (255, 255, 255, 255)])
        plain, ph, _ = self.run_texture(image, "--palette", palette)
        dithered, dh, _ = self.run_texture(image, "--palette", palette, "--dither")
        self.assertNotEqual(unswizzle(plain, ph), unswizzle(dithered, dh))
        self.assertEqual(plain[32:96], dithered[32:96])
        image.putalpha(0)
        self.assertIn("entry 0", self.run_texture(image, "--palette", palette, success=False))
        bad = self.make_palette([(0, 0, 0, 255), (1, 1, 1, 0)])
        self.assertIn("entry 0", self.run_texture(image, "--palette", bad, success=False))
        many = self.make_palette([(i, i, i, 255) for i in range(17)])
        self.assertIn("at most 16", self.run_texture(image, "--palette", many, success=False))
        self.assertIn("only supported", self.run_texture(image, "--format", "8888", "--palette", palette, success=False))

    def test_fixed_transparent_palette_preserves_slots(self):
        colors = [(99, 1, 3, 0), (255, 0, 0, 255), (0, 0, 255, 255)]
        palette = self.make_palette(colors)
        image = Image.frombytes("RGBA", (8, 8), bytes(v for i in range(64) for v in colors[i % 3]))
        blob, header, _ = self.run_texture(image, "--palette", palette)
        self.assertEqual(blob[32:44], bytes(4) + bytes(colors[1]) + bytes(colors[2]))
        self.assertEqual(unswizzle(blob, header)[:3], bytes((0x10, 0x02, 0x21)))

    def test_large_texture_warning_and_t8_memory_budget(self):
        _, _, warning = self.run_texture(Image.new("RGB", (512, 8)))
        self.assertIn("warning:", warning)
        blob, header, _ = self.run_texture(Image.new("RGB", (128, 128)), "--format", "t8")
        self.assertEqual(header["frame_bytes"], 16 * 1024)
        self.assertEqual(header["clut_count"] * 4, 1024)
        self.assertEqual(len(blob), 32 + 1024 + 16 * 1024)


if __name__ == "__main__":
    unittest.main()
