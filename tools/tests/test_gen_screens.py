"""A011: независимые проверки PNG из CLI во временных папках."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from validate_assets import texture_frames, validate_texture


EXPECTED = {
    "scr_shooter": 8, "scr_rts": 8, "scr_tbs": 8, "scr_rpg": 8,
    "scr_sim": 8, "scr_race": 8, "scr_action": 8, "scr_casual": 8,
    "scr_desktop": 1, "scr_loading": 8, "scr_bsod": 1, "scr_off": 1,
}
COLORS = {bytes.fromhex(color) for color in (
    "101416", "222222", "2F3A55", "4B6449", "5E7A5A", "5E412A",
    "8A6440", "8F8670", "B8AE94", "D9D2BC", "FFD9A0", "E8F0F0",
    "2050C0", "30C050", "E04030", "F0C020",
)}
ENV = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1"}


def run_script(script, *args, cwd=None, hash_seed="0"):
    return subprocess.run(
        [sys.executable, "-B", str(TOOLS / script), *map(str, args)],
        cwd=cwd, env={**ENV, "PYTHONHASHSEED": hash_seed},
        capture_output=True, text=True, encoding="utf-8", timeout=60,
    )


class ScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="a011-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.output = cls.root / "first"
        cls.second = cls.root / "second"
        for output, hash_seed in ((cls.output, "1"), (cls.second, "913")):
            result = run_script("gen_screens.py", "-o", output, cwd=cls.root, hash_seed=hash_seed)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)

    def test_complete_list(self):
        self.assertEqual({path.name for path in self.output.iterdir()},
                         {f"{name}.png" for name in EXPECTED} | {"scr_sheet.png"})

    def test_dimensions_and_frame_counts(self):
        for name, frames in EXPECTED.items():
            with self.subTest(screen=name), Image.open(self.output / f"{name}.png") as image:
                self.assertEqual(image.format, "PNG")
                self.assertEqual(image.size, (64, 64 * frames))
                self.assertEqual(texture_frames(self.output / f"{name}.png", image.size), frames)

    def test_palette_is_shared_by_entire_strip(self):
        for name in EXPECTED:
            with self.subTest(screen=name), Image.open(self.output / f"{name}.png") as image:
                colors = image.convert("RGB").getcolors(maxcolors=16)
                self.assertIsNotNone(colors, "more than 16 colors in the whole strip")
                self.assertTrue({bytes(rgb) for _, rgb in colors} <= COLORS)

    def test_fully_opaque(self):
        for path in self.output.glob("*.png"):
            with self.subTest(file=path.name), Image.open(path) as image:
                self.assertEqual(image.convert("RGBA").getchannel("A").getextrema(), (255, 255))

    def test_byte_determinism_across_processes(self):
        for path in self.output.glob("*.png"):
            with self.subTest(file=path.name):
                self.assertEqual(path.read_bytes(), (self.second / path.name).read_bytes())

    def test_no_png_metadata(self):
        for path in self.output.glob("*.png"):
            with self.subTest(file=path.name):
                raw, offset, chunks = path.read_bytes(), 8, []
                while offset < len(raw):
                    length = struct.unpack_from(">I", raw, offset)[0]
                    chunks.append(raw[offset + 4:offset + 8])
                    offset += 12 + length
                self.assertEqual(offset, len(raw))
                self.assertEqual(set(chunks), {b"IHDR", b"IDAT", b"IEND"})

    def test_animated_frames_pairwise_different(self):
        for name, count in EXPECTED.items():
            if count == 1:
                continue
            with self.subTest(screen=name), Image.open(self.output / f"{name}.png") as image:
                frames = [image.crop((0, i * 64, 64, (i + 1) * 64)).tobytes() for i in range(count)]
                self.assertEqual(len(set(frames)), count)

    def test_validator(self):
        for name in EXPECTED:
            with self.subTest(screen=name):
                report = validate_texture(self.output / f"{name}.png")
                self.assertEqual(report.errors, [])
                self.assertEqual(report.warnings, [])

    def test_t4_conversion(self):
        for name, count in EXPECTED.items():
            with self.subTest(screen=name):
                target = self.root / f"{name}.tex"
                result = run_script("convert_texture.py", self.output / f"{name}.png",
                                    "-o", target, "--format", "t4", "--frames", count)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                raw = target.read_bytes()
                header = struct.unpack_from("<4sHBB6HIII", raw)
                self.assertEqual(header[:6], (b"TEX1", 1, 4, 1, 64, 64))
                self.assertEqual(header[7:9], (count, 16))
                self.assertEqual(len(raw), 32 + 64 + count * 2048)
                with Image.open(self.output / f"{name}.png") as image:
                    colors = len(image.getcolors())
                self.assertEqual(set(raw[35:32 + colors * 4:4]), {255})

    def test_preview_contains_every_frame_at_triple_size(self):
        with Image.open(self.output / "scr_sheet.png") as sheet:
            self.assertEqual(sheet.size, (1624, 2684))
            for row, (name, count) in enumerate(EXPECTED.items()):
                with Image.open(self.output / f"{name}.png") as strip:
                    for frame in range(count):
                        with self.subTest(screen=name, frame=frame):
                            x, y = 16 + frame * 200, 16 + row * 221 + 21
                            preview = sheet.crop((x, y, x + 192, y + 192))
                            expected = strip.crop((0, frame * 64, 64, (frame + 1) * 64))
                            self.assertEqual(preview.tobytes(), expected.resize(
                                (192, 192), Image.Resampling.NEAREST).tobytes())

    def test_explicit_output_is_isolated_and_relative(self):
        result = run_script("gen_screens.py", "-o", "nested/textures", cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual({path.name for path in (self.root / "nested/textures").iterdir()},
                         {path.name for path in self.output.iterdir()})
        self.assertFalse((self.root / "nested/previews").exists())

    def test_default_paths_are_relative_to_script(self):
        project = self.root / "project"
        (project / "tools").mkdir(parents=True)
        script = project / "tools/gen_screens.py"
        shutil.copyfile(TOOLS / "gen_screens.py", script)
        result = run_script(script, cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual({path.name for path in (project / "assets/textures").iterdir()},
                         {f"{name}.png" for name in EXPECTED})
        self.assertEqual((project / "assets/previews/scr_sheet.png").read_bytes(),
                         (self.output / "scr_sheet.png").read_bytes())


if __name__ == "__main__":
    unittest.main()
