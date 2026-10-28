"""A012: независимые проверки выходов CLI; все записи только во временную папку."""

import itertools
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from convert_texture import convert
from validate_assets import validate_texture


PROGRAMS = ("cashier", "network", "shop", "mail", "ledger", "records", "games", "settings")
SYSTEM = ("computer", "trash", "folder", "document", "info", "warning", "error", "question", "start")
SIZES = {"ui_icons32": (128, 128), "ui_icons16": (64, 64), "ui_frame": (128, 64),
         "ui_cursor": (64, 32), "ui_wallpaper": (256, 128)}


def pixels(image):
    return list(struct.iter_unpack("4B", image.convert("RGBA").tobytes()))


class UiArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="a012-test-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.base = Path(cls.temporary.name)
        cls.outputs = (cls.base / "first", cls.base / "second")
        for output in cls.outputs:
            result = subprocess.run(
                [sys.executable, "-B", str(ROOT / "tools/gen_ui_art.py"), "-o", str(output)],
                cwd=cls.base, capture_output=True, text=True, encoding="utf-8",
                env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1"}, timeout=60)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        cls.output = cls.outputs[0]
        cls.mapping = json.loads((cls.output / "ui_atlas.json").read_text(encoding="utf-8"))

    def test_exact_outputs_and_sizes(self):
        self.assertEqual({p.name for p in self.output.iterdir()},
                         {k + ".png" for k in SIZES} | {"ui_atlas.json", "ui_sheet.png"})
        for name, size in SIZES.items():
            with self.subTest(texture=name), Image.open(self.output / (name + ".png")) as image:
                self.assertEqual(image.size, size)
                self.assertEqual(image.format, "PNG")
                self.assertEqual(image.mode, "P")
                self.assertEqual((self.output / (name + ".png")).read_bytes()[24], 4)

    def test_all_programs_and_system_icons(self):
        for name, size in itertools.product(PROGRAMS + SYSTEM, (16, 32)):
            key = f"icon{size}.{name}"
            with self.subTest(key=key):
                self.assertIn(key, self.mapping)
                r = self.mapping[key]
                self.assertEqual((r["w"], r["h"]), (size, size))
                with Image.open(self.output / (r["tex"] + ".png")) as image:
                    crop = image.crop((r["x"], r["y"], r["x"] + size, r["y"] + size)).convert("RGBA")
                    self.assertIsNotNone(crop.getchannel("A").getbbox())
        for size in (16, 32):
            self.assertEqual(sum(r["tex"] == f"ui_icons{size}" for r in self.mapping.values()), 16)

    def test_rectangles_in_bounds_and_disjoint(self):
        for key, r in self.mapping.items():
            with self.subTest(key=key):
                self.assertRegex(key, r"^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$")
                self.assertIn(r["tex"], SIZES)
                for field in ("x", "y", "w", "h"):
                    self.assertIs(type(r[field]), int)
                self.assertGreater(r["w"], 0)
                self.assertGreater(r["h"], 0)
                self.assertGreaterEqual(r["x"], 0)
                self.assertGreaterEqual(r["y"], 0)
                w, h = SIZES[r["tex"]]
                self.assertLessEqual(r["x"] + r["w"], w)
                self.assertLessEqual(r["y"] + r["h"], h)
        for (ka, a), (kb, b) in itertools.combinations(self.mapping.items(), 2):
            if a["tex"] == b["tex"]:
                overlaps = (a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"]
                            and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"])
                self.assertFalse(overlaps, f"{ka} overlaps {kb}")

    def test_nine_slice_and_chrome_coverage(self):
        required_slices = {"frame.window", "frame.field", "button.normal", "button.pressed",
                           "button.focus", "scroll.thumb", "taskbar.start_normal", "taskbar.start_pressed"}
        self.assertTrue(required_slices <= self.mapping.keys())
        for key in required_slices:
            with self.subTest(key=key):
                r = self.mapping[key]
                self.assertEqual(len(r["slice"]), 4)
                left, top, right, bottom = r["slice"]
                self.assertTrue(all(type(n) is int and n > 0 for n in r["slice"]))
                self.assertLess(left + right, r["w"])
                self.assertLess(top + bottom, r["h"])
        required = {"title.active", "title.inactive", "checkbox.on", "checkbox.off", "radio.on",
                    "radio.off", "progress.segment", "menu.separator", "taskbar.tile", "scroll.track"}
        required.update(f"caption.{action}_{state}" for action in ("minimize", "close")
                        for state in ("normal", "pressed"))
        required.update(f"scroll.{direction}_{state}" for direction in ("up", "down", "left", "right")
                        for state in ("normal", "pressed"))
        self.assertTrue(required <= self.mapping.keys())
        self.assertIs(self.mapping["button.focus"]["tile_edges"], True)

    def test_color_budget_alpha_and_shared_palette(self):
        shared = None
        for name in SIZES:
            with self.subTest(texture=name), Image.open(self.output / (name + ".png")) as image:
                rgba = image.convert("RGBA")
                colors = set(pixels(rgba))
                self.assertLessEqual({c[3] for c in colors}, {0, 255})
                opaque = {c for c in colors if c[3] == 255}
                if name == "ui_wallpaper":
                    self.assertEqual(rgba.getchannel("A").getextrema(), (255, 255))
                    self.assertLessEqual(len(opaque), 16)
                else:
                    self.assertLessEqual(len(opaque), 15)
                    self.assertIn(0, {c[3] for c in colors})
                    self.assertEqual(image.info["transparency"], 0)
                    if shared is None:
                        shared = image.getpalette()
                    self.assertEqual(image.getpalette(), shared)

    def test_cursors_and_animation(self):
        for name in ("arrow", "hand", "wait", "wait_1", "ibeam"):
            r = self.mapping["cursor." + name]
            self.assertEqual((r["w"], r["h"]), (16, 16))
            self.assertEqual(len(r["hot"]), 2)
            self.assertTrue(all(type(n) is int and 0 <= n < 16 for n in r["hot"]))
            with Image.open(self.output / (r["tex"] + ".png")) as image:
                crop = image.crop((r["x"], r["y"], r["x"] + 16, r["y"] + 16)).convert("RGBA")
                colors = set(pixels(crop))
                self.assertIn((34, 34, 34, 255), colors)
                self.assertIn((255, 255, 255, 255), colors)
                self.assertTrue(crop.getchannel("A").getpixel(tuple(r["hot"])))
        wait = self.mapping["cursor.wait"]
        self.assertEqual(wait["frames"][0], "cursor.wait")
        self.assertEqual(len(wait["frames"]), 2)
        self.assertGreater(wait["frame_ms"], 0)
        frames = [self.mapping[key] for key in wait["frames"]]
        self.assertEqual(frames[0]["hot"], frames[1]["hot"])
        self.assertNotEqual((frames[0]["x"], frames[0]["y"]), (frames[1]["x"], frames[1]["y"]))

    def test_byte_determinism(self):
        for path in self.outputs[0].iterdir():
            with self.subTest(file=path.name):
                self.assertEqual(path.read_bytes(), (self.outputs[1] / path.name).read_bytes())

    def test_validate_texture(self):
        for name in SIZES:
            with self.subTest(texture=name):
                self.assertEqual(validate_texture(self.output / (name + ".png")).errors, [])

    def test_t4_conversion_without_color_loss(self):
        # Линейный TEX1 позволяет независимо сравнить RGBA каждого пикселя.
        for name, size in SIZES.items():
            with self.subTest(texture=name):
                source = self.output / (name + ".png")
                target = self.base / (name + ".tex")
                convert(source, target, format_name="t4", no_swizzle=True)
                raw = target.read_bytes()
                header = struct.unpack_from("<4sHBB6HIII", raw)
                self.assertEqual(header[:3], (b"TEX1", 1, 4))
                self.assertEqual(header[4:6], size)
                stride, frames, clut_count = header[6:9]
                self.assertEqual((frames, clut_count), (1, 16))
                clut_offset, pixel_offset = header[10:12]
                clut = [tuple(raw[clut_offset + i * 4:clut_offset + i * 4 + 4]) for i in range(16)]
                with Image.open(source) as image:
                    expected = pixels(image)
                actual = []
                for y in range(size[1]):
                    for x in range(size[0]):
                        value = raw[pixel_offset + y * stride // 2 + x // 2]
                        actual.append(clut[(value >> (4 * (x % 2))) & 15])
                self.assertEqual(actual, expected)

    def test_preview_contains_full_screen_at_double_size(self):
        with Image.open(self.output / "ui_sheet.png") as image:
            self.assertEqual(image.size, (1240, 1490))
            screen = image.crop((20, 940, 980, 1484)).convert("RGB")
            self.assertEqual(screen.size, (960, 544))
            for y in range(0, 544, 2):
                for x in range(0, 960, 2):
                    a = screen.getpixel((x, y))
                    self.assertEqual(a, screen.getpixel((x + 1, y)))
                    self.assertEqual(a, screen.getpixel((x, y + 1)))
                    self.assertEqual(a, screen.getpixel((x + 1, y + 1)))


if __name__ == "__main__":
    unittest.main()
