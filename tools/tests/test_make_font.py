import contextlib
import gzip
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("make_font", ROOT / "tools/make_font.py")
font_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = font_tool
SPEC.loader.exec_module(font_tool)

# Независимая раскладка из docs/formats/font.md.
HEADER = struct.Struct("<4sHHHHBBBBHH12s")
GLYPH = struct.Struct("<IHHBBbbB3s")
CODES = [ord(c) for c in " ?AB01"]


def psf2(width=6, height=4, codes=None, sequence=False):
    codes = CODES if codes is None else codes
    stride = (width + 7) // 8
    images = []
    for cp in codes:
        rows = [0] * height
        if cp != 32:
            rows[0] = 0x20 if cp in (ord("A"), ord("1")) else 0x70
            rows[-1] = 0x20
        images.append(b"".join(bytes([value]) + bytes(stride - 1) for value in rows))
    table = bytearray()
    for cp in codes:
        table.extend(chr(cp).encode("utf-8"))
        if cp == ord("A"):
            table.extend("Ё".encode("utf-8"))
            if sequence:
                table.extend(b"\xfeXY")
        table.append(255)
    return (struct.pack("<8I", 0x864AB572, 0, 32, 1, len(codes), height * stride, height, width)
            + b"".join(images) + table)


def psf1(count=256, sequence=False):
    height = 4
    mode = 2 | int(count == 512) | (4 if sequence else 0)
    images = b"".join(bytes([0x70, 0x20, 0, 0x20]) for _ in range(count))
    entries = []
    for i in range(count):
        codes = [0x100 + i]
        if i == 65:
            codes.extend([0x41, 0x401])
            if sequence:
                codes.extend([0xFFFE, ord("X"), ord("Y")])
        entries.append(struct.pack("<" + "H" * (len(codes) + 1), *codes, 0xFFFF))
    return bytes([0x36, 0x04, mode, height]) + images + b"".join(entries)


def bdf():
    return b"""STARTFONT 2.1
FONT synthetic
SIZE 4 75 75
FONTBOUNDINGBOX 6 5 -1 -1
STARTPROPERTIES 2
FONT_ASCENT 4
FONT_DESCENT 1
ENDPROPERTIES
CHARS 3
STARTCHAR question
ENCODING 63
DWIDTH 6 0
BBX 3 3 1 0
BITMAP
E0
20
40
ENDCHAR
STARTCHAR A
ENCODING 65
DWIDTH 6 0
BBX 3 3 1 0
BITMAP
40
A0
E0
ENDCHAR
STARTCHAR space
ENCODING 32
DWIDTH 6 0
BBX 0 0 0 0
BITMAP
ENDCHAR
ENDFONT
"""


class MakeFontTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.src = self.directory / "tiny.psf"
        self.src.write_bytes(psf2())
        self.out = self.directory / "tiny"

    def generate(self, **options):
        return font_tool.generate(self.src, self.out, extra=None, charset=CODES, **options)

    def metrics(self, output=None):
        data = (output or self.out).with_suffix(".fnt").read_bytes()
        self.assertEqual(HEADER.size, 32)
        self.assertEqual(GLYPH.size, 16)
        header = HEADER.unpack_from(data)
        self.assertEqual(len(data), 32 + 16 * header[3])
        records = [GLYPH.unpack_from(data, 32 + 16 * i) for i in range(header[3])]
        return header, records

    def test_psf1_256_and_512_unicode_aliases_sequences_gzip(self):
        for count in (256, 512):
            for compressed in (False, True):
                with self.subTest(count=count, compressed=compressed):
                    data = psf1(count, sequence=True)
                    self.src.write_bytes(gzip.compress(data) if compressed else data)
                    font = font_tool.read_font(self.src)
                    self.assertEqual((font.width, font.height), (8, 4))
                    self.assertEqual(font.glyphs[0x41].tobytes(), font.glyphs[0x401].tobytes())
                    self.assertNotIn(ord("X"), font.glyphs)
                    self.assertEqual(font.glyphs[0x41].getpixel((1, 0)), 1)
                    self.assertEqual(font.glyphs[0x41].getpixel((0, 0)), 0)

    def test_psf2_unicode_aliases_sequences_gzip_and_wide_rows(self):
        for width in (6, 10):
            for compressed in (False, True):
                with self.subTest(width=width, compressed=compressed):
                    data = psf2(width=width, sequence=True)
                    self.src.write_bytes(gzip.compress(data) if compressed else data)
                    font = font_tool.read_font(self.src)
                    self.assertEqual((font.width, font.height), (width, 4))
                    self.assertEqual(font.glyphs[ord("A")].tobytes(), font.glyphs[0x401].tobytes())
                    self.assertNotIn(ord("X"), font.glyphs)
                    self.assertEqual(font.glyphs[ord("A")].getpixel((2, 0)), 1)
                    self.assertEqual(font.glyphs[ord("A")].getpixel((width - 1, 0)), 0)

    def test_psf_without_unicode_table_uses_index(self):
        data = bytearray(psf2())
        struct.pack_into("<I", data, 12, 0)
        self.src.write_bytes(data[:32 + 4 * len(CODES)])
        self.assertEqual(list(font_tool.read_font(self.src).glyphs), list(range(len(CODES))))
        self.src.write_bytes(bytes([0x36, 4, 0, 1]) + b"\x80" * 256)
        self.assertEqual(len(font_tool.read_font(self.src).glyphs), 256)

    def test_bdf_offsets_baseline_empty_glyph_and_export(self):
        self.src.write_bytes(bdf())
        font = font_tool.read_font(self.src)
        self.assertEqual((font.width, font.height, font.baseline), (6, 5, 4))
        self.assertEqual(font.glyphs[65].getpixel((3, 1)), 1)
        self.assertEqual(font.glyphs[65].getpixel((2, 1)), 0)
        self.assertIsNone(font.glyphs[32].getbbox())
        font_tool.generate(self.src, self.out, extra=None, charset=[65, 32, 63], mode="mono")
        header, records = self.metrics()
        self.assertEqual(header[6], 4)
        self.assert_atlas_pixels(font, records, mono=True)

    def test_fnt_layout_sorting_fallback_reserved_and_name(self):
        self.generate()
        header, records = self.metrics()
        self.assertEqual(header[:3], (b"FNT1", 1, 0))
        self.assertEqual(header[3], len(CODES))
        self.assertEqual(records[header[4]][0], ord("?"))
        self.assertEqual(header[5:9], (4, 2, 6, 3))
        self.assertEqual(header[11], b"tiny" + b"\0" * 8)
        self.assertEqual([r[0] for r in records], sorted(CODES))
        self.assertTrue(all(r[8] == b"\0\0\0" for r in records))

    def test_mono_all_advances_and_source_pixels(self):
        self.generate(mode="mono", space=1)
        header, records = self.metrics()
        self.assertEqual(header[2], 1)
        self.assertEqual(header[8], 6)
        self.assertEqual({r[7] for r in records}, {6})
        self.assert_atlas_pixels(font_tool.read_font(self.src), records, mono=True)

    def test_prop_crop_space_and_tabular_digits(self):
        self.generate(space=2)
        header, records = self.metrics()
        lookup = {r[0]: r for r in records}
        self.assertEqual(header[8], 2)
        self.assertEqual(lookup[32][1:8], (0, 0, 0, 0, 0, 0, 2))
        self.assertEqual(lookup[65][3:8], (1, 4, 0, 0, 2))
        self.assertEqual(lookup[66][3:8], (3, 4, 0, 0, 4))
        self.assertEqual({lookup[c][7] for c in (48, 49)}, {6})
        self.assert_atlas_pixels(font_tool.read_font(self.src), records)
        self.generate(tabular_digits=False)
        _, records = self.metrics()
        lookup = {r[0]: r for r in records}
        self.assertEqual((lookup[48][7], lookup[49][7]), (4, 2))

    def test_empty_nonspace_glyph_uses_space_advance(self):
        self.src.write_bytes(psf2(codes=[32, 63, 65]))
        data = bytearray(self.src.read_bytes())
        data[40:44] = b"\0" * 4
        self.src.write_bytes(data)
        font_tool.generate(self.src, self.out, extra=None, charset=[32, 63, 65])
        _, records = self.metrics()
        self.assertEqual(records[-1][3:8], (0, 0, 0, 0, 3))

    def test_extra_supplies_missing_glyph_and_preserves_source(self):
        extra = self.directory / "extra.txt"
        extra.write_text("[6x4 regular U+20BD]\n.###..\n.#.#..\n.###..\n.#....\n"
                         "\n[6x4 bold U+20BD]\n####..\n##.#..\n####..\n##....\n"
                         "\n[6x4 regular U+0041]\n######\n######\n######\n######\n", encoding="utf-8")
        font_tool.generate(self.src, self.out, extra=extra, charset=CODES + [0x20BD])
        _, records = self.metrics()
        lookup = {r[0]: r for r in records}
        self.assertEqual(lookup[0x20BD][3:8], (3, 4, 0, 0, 4))
        self.assertEqual(lookup[65][3], 1)
        self.assertEqual(font_tool.read_extra(extra, 6, 4, "bold")[0x20BD].getpixel((0, 0)), 1)
        with Image.open(self.out.with_suffix(".png")) as atlas:
            r = lookup[0x20BD]
            self.assertEqual(list(atlas.crop((r[1], r[2], r[1] + r[3], r[2] + r[4])).getdata()),
                             [1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0])

    def test_missing_lists_all_codes_and_writes_nothing(self):
        with self.assertRaisesRegex(ValueError, "missing glyphs: U\\+20BD, U\\+2603"):
            font_tool.generate(self.src, self.out, extra=None, charset=CODES + [0x2603, 0x20BD])
        self.assertFalse(self.out.with_suffix(".png").exists())
        self.assertFalse(self.out.with_suffix(".fnt").exists())

    def test_atlas_overflow(self):
        self.src.write_bytes(psf2(width=64, height=64, codes=CODES + list(range(100, 125))))
        with self.assertRaisesRegex(ValueError, "256x256"):
            font_tool.generate(self.src, self.out, extra=None,
                               charset=CODES + list(range(100, 125)), mode="mono")

    def test_png_4bit_palette_transparency_shelves_and_minimum_area(self):
        self.generate()
        raw = self.out.with_suffix(".png").read_bytes()
        self.assertEqual(raw[24:26], b"\x04\x03")
        header, records = self.metrics()
        with Image.open(self.out.with_suffix(".png")) as atlas:
            self.assertEqual(atlas.mode, "P")
            self.assertEqual(atlas.size, header[9:11])
            rgba = atlas.convert("RGBA")
            self.assertEqual(rgba.getpixel((0, 0)), (0, 0, 0, 0))
            self.assertEqual(atlas.getpalette()[:6], [0, 0, 0, 255, 255, 255])
            for r in records:
                _, x, y, w, h, *_ = r
                if not w:
                    continue
                self.assertGreaterEqual(x, 1)
                self.assertGreaterEqual(y, 1)
                self.assertLess(x + w, atlas.width)
                self.assertLess(y + h, atlas.height)
                self.assertEqual(rgba.getpixel((x + w, y)), (0, 0, 0, 0))
                for s in records:
                    if s is r or not s[3]:
                        continue
                    self.assertTrue(x + w < s[1] or s[1] + s[3] < x
                                    or y + h < s[2] or s[2] + s[4] < y)
            ink = next((x, y) for y in range(atlas.height) for x in range(atlas.width)
                       if atlas.getpixel((x, y)) == 1)
            self.assertEqual(rgba.getpixel(ink), (255, 255, 255, 255))
        # Отступы и полки требуют минимум 256 пикселей для этой фикстуры.
        self.assertEqual(header[9] * header[10], 256)

    def test_deterministic_bytes(self):
        self.generate()
        first = [self.out.with_suffix(ext).read_bytes() for ext in (".png", ".fnt")]
        self.generate()
        self.assertEqual(first, [self.out.with_suffix(ext).read_bytes() for ext in (".png", ".fnt")])

    def test_invalid_input_and_options(self):
        for data in (b"", b"\x36\x04", psf1()[:-1], psf2()[:-1],
                     bdf().replace(b"CHARS 3", b"CHARS 4")):
            with self.subTest(data=data[:8]):
                self.src.write_bytes(data)
                with self.assertRaises(ValueError):
                    font_tool.read_font(self.src)
        self.src.write_bytes(psf2())
        for options in ({"space": 0}, {"baseline": 100}, {"mode": "bad"}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.generate(**options)
        with self.assertRaisesRegex(ValueError, "1..11"):
            font_tool.generate(self.src, self.directory / "too_long_font_name", extra=None)
        with self.assertRaisesRegex(ValueError, "charset"):
            font_tool.generate(self.src, self.out, extra=None, charset=[65])

    def test_extra_rejects_malformed_duplicate_and_wrong_dimensions(self):
        extra = self.directory / "extra.txt"
        for text in ("bad\n", "[6x4 regular U+20BD]\n......\n",
                     "[6x1 regular U+20BD]\n......\n[6x1 regular U+20BD]\n......\n",
                     "[6x1 regular U+20BD]\n...X..\n"):
            extra.write_text(text, encoding="utf-8")
            with self.subTest(text=text), self.assertRaises(ValueError):
                font_tool.read_extra(extra, 6, 4)

    def test_cli_single_preview_and_missing_error(self):
        image = self.directory / "preview.png"
        # Полный набор подставляем только на уровне API; CLI проверяем на реальном PSF.
        src = ROOT / "assets/fonts/Uni2-Terminus12x6.psf.gz"
        result = subprocess.run([sys.executable, str(ROOT / "tools/make_font.py"), str(src),
                                 "--mode", "prop", "--no-tabular-digits", "--space", "2",
                                 "-o", str(self.out), "--preview", str(image)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with Image.open(image) as im:
            self.assertEqual(im.mode, "RGB")
            self.assertEqual(im.width % 3, 0)
            self.assertGreater(im.height, 200)
        result = subprocess.run([sys.executable, str(ROOT / "tools/make_font.py"), str(self.src),
                                 "-o", str(self.out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing glyphs:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_real_fonts_all_full_charset_pixels_and_preview_from_disk(self):
        base = self.directory / "assets/fonts"
        base.mkdir(parents=True)
        originals = ROOT / "assets/fonts"
        entries = json.loads((originals / "fonts.json").read_text(encoding="utf-8"))
        for path in originals.iterdir():
            if path.suffix == ".gz" or path.name in ("fonts.json", "extra_glyphs.txt"):
                (base / path.name).write_bytes(path.read_bytes())
        preview_dir = self.directory / "previews"
        with mock.patch.object(font_tool, "ROOT", self.directory), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(font_tool.main(["--all", "--extra", str(base / "extra_glyphs.txt"),
                                             "--preview", str(preview_dir)]), 0)
        first = {}
        for entry in entries:
            output = base / entry["id"]
            header, records = self.metrics(output)
            self.assertEqual([r[0] for r in records], sorted(set(font_tool.CHARSET)))
            self.assertIn(0x20BD, [r[0] for r in records])
            self.assertIn(0x401, [r[0] for r in records])
            self.assertEqual(records[header[4]][0], 63)
            self.assertTrue(all(v <= 256 and v & (v - 1) == 0 for v in header[9:11]))
            font = font_tool.read_font(base / entry["source"])
            font.glyphs.update({cp: im for cp, im in font_tool.read_extra(
                base / "extra_glyphs.txt", font.width, font.height,
                entry["options"]["style"]).items() if cp not in font.glyphs})
            self.assert_atlas_pixels(font, records, entry["mode"] == "mono", output)
            self.assertTrue((preview_dir / (entry["id"] + ".png")).exists())
            first[entry["id"]] = [output.with_suffix(ext).read_bytes() for ext in (".png", ".fnt")]
        with mock.patch.object(font_tool, "ROOT", self.directory), contextlib.redirect_stdout(io.StringIO()):
            font_tool.main(["--all", "--extra", str(base / "extra_glyphs.txt")])
        for entry in entries:
            self.assertEqual(first[entry["id"]], [(base / entry["id"]).with_suffix(ext).read_bytes()
                                                  for ext in (".png", ".fnt")])
        # Удаление исходников не мешает превью по готовой паре PNG/FNT.
        for path in base.glob("*.gz"):
            path.unlink()
        font_tool.preview((base / entries[0]["id"]).with_suffix(".fnt"), self.directory / "disk.png")

    def assert_atlas_pixels(self, font, records, mono=False, output=None):
        with Image.open((output or self.out).with_suffix(".png")) as atlas:
            for cp, x, y, w, h, _, _, _, _ in records:
                source = font.glyphs[cp]
                if not w:
                    self.assertIsNone(source.getbbox())
                    continue
                if not mono and not 48 <= cp <= 57:
                    left, _, right, _ = source.getbbox()
                    source = source.crop((left, 0, right, font.height))
                crop = atlas.crop((x, y, x + w, y + h))
                self.assertEqual(list(crop.getdata()), list(source.getdata()), f"U+{cp:04X}")


if __name__ == "__main__":
    unittest.main()
