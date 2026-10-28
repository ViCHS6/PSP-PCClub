"""Независимые проверки ANM1, включая ручной пример из docs/formats/anim.md."""

import copy
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "convert_anim.py"


def example():
    return {"version": 1, "coordinate_system": "blender_z_up",
            "parts": [{"name": "pelvis", "parent": None, "offset": [0, 0, 1]}],
            "clips": [{"name": "walk", "fps": 15, "loop": True, "frames": [
                {"root": [0, 0, 0], "rotations": {"pelvis": [0, 0, 0, 1]}},
                {"root": [1, -2, 3], "rotations": {"pelvis": [1, 0, 0, 0]}}]}]}


class AnimTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "example.anim.json"
        self.output = Path(self.temp.name) / "example.anm"

    def convert(self, doc, error=None):
        self.source.write_text(json.dumps(doc), encoding="utf-8")
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), str(self.source),
                                 "-o", str(self.output), "--report"], capture_output=True, text=True)
        if error is not None:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(str(self.source), result.stderr)
            self.assertIn(error, result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse(self.output.exists())
            return None
        self.assertEqual(result.returncode, 0, result.stderr)
        return self.output.read_bytes()

    def test_manual_example_exact_bytes(self):
        # 32-byte header, part, clip, two 20-byte keys, 8-byte padding.
        expected = bytes.fromhex(
            "414e4d31 0100 0000 0100 0100 20000000 50000000 80000000 b0000000 00000000")
        expected += b"pelvis" + bytes(26) + bytes.fromhex("ffff 0000 00000000 0000803f 00000000")
        expected += b"walk" + bytes(28) + bytes.fromhex("0200 0f00 01000000 80000000 00000000")
        expected += bytes(12) + bytes.fromhex("0000 0000 0000 ff7f")
        expected += bytes.fromhex("0000803f 00004040 00000040 ff7f 0000 0000 0000") + bytes(8)
        self.assertEqual(len(expected), 176)
        self.assertEqual(self.convert(example()), expected)

    def test_quantization_decode_and_axes(self):
        doc = example()
        q = [0.2, -0.3, 0.4, math.sqrt(0.71)]
        doc["clips"][0]["frames"][1]["rotations"]["pelvis"] = q
        blob = self.convert(doc)
        packed = struct.unpack_from("<4h", blob, 160)
        decoded = [v / 32767 for v in packed]
        norm = math.sqrt(sum(v*v for v in decoded))
        decoded = [v / norm for v in decoded]
        for a, b in zip(decoded, [q[0], q[2], -q[1], q[3]]):
            self.assertAlmostEqual(a, b, delta=0.00004)
        self.assertEqual(struct.unpack_from("<3f", blob, 148), (1, 3, 2))

    def test_missing_rotation_part(self):
        doc = example()
        del doc["clips"][0]["frames"][0]["rotations"]["pelvis"]
        self.convert(doc, "clips[0].frames[0].rotations: missing fields: pelvis")

    def test_missing_parent_part(self):
        doc = example()
        doc["parts"][0]["parent"] = "absent"
        self.convert(doc, "parts[0].parent")

    def test_cycle(self):
        doc = example()
        doc["parts"][0]["parent"] = "head"
        doc["parts"].append({"name": "head", "parent": "pelvis", "offset": [0, 0, 1]})
        self.convert(doc, "cycle in hierarchy")

    def test_zero_frames(self):
        doc = example()
        doc["clips"][0]["frames"] = []
        self.convert(doc, "clips[0].frames")

    def test_wrong_frequency(self):
        doc = example()
        doc["clips"][0]["fps"] = 30
        self.convert(doc, "clips[0].fps")

    def test_bad_quaternion(self):
        doc = example()
        doc["clips"][0]["frames"][0]["rotations"]["pelvis"] = [0, 0, 0, 0]
        self.convert(doc, "clips[0].frames[0].rotations.pelvis")

    def test_nonfinite(self):
        doc = example()
        doc["parts"][0]["offset"][0] = float("nan")
        self.convert(doc, "parts[0].offset[0]")

    def test_unknown_field(self):
        doc = example()
        doc["clips"][0]["events"] = []
        self.convert(doc, "clips[0]: unknown fields: events")

    def test_duplicate_json_keys(self):
        self.source.write_text('{"version": 1, "version": 1}', encoding="utf-8")
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), str(self.source),
                                 "-o", str(self.output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(self.source), result.stderr)
        self.assertIn("JSON.version: duplicate object key", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(self.output.exists())

    def test_budget(self):
        doc = example()
        doc["clips"][0]["frames"] *= 2000
        self.convert(doc, "exceeds 65536")

    def test_sixteen_clips_accepted(self):
        doc = example()
        doc["clips"] = [dict(copy.deepcopy(doc["clips"][0]), name=f"clip_{i}") for i in range(16)]
        blob = self.convert(doc)
        self.assertEqual(struct.unpack_from("<H", blob, 10)[0], 16)
        self.assertEqual(len(blob), 1616)
        self.assertEqual(struct.unpack_from("<32s", blob, 80 + 15 * 48)[0].rstrip(b"\0"), b"clip_15")

    def test_seventeen_clips_rejected(self):
        doc = example()
        doc["clips"] = [dict(copy.deepcopy(doc["clips"][0]), name=f"clip_{i}") for i in range(17)]
        self.convert(doc, "clips: expected 1..16 clips")

    def test_multiple_clips_padding_and_flags(self):
        doc = example()
        other = copy.deepcopy(doc["clips"][0])
        other.update(name="sit_down", loop=False)
        doc["clips"].append(other)
        blob = self.convert(doc)
        self.assertEqual(struct.unpack_from("<I", blob, 20)[0], 176)
        self.assertEqual(struct.unpack_from("<I", blob, 120)[0], 176)
        self.assertEqual(struct.unpack_from("<III", blob, 164), (0, 224, 0))
        self.assertEqual(len(blob), 272)

    def test_placeholder_pipeline(self):
        folder = Path(self.temp.name) / "assets"
        result = subprocess.run([sys.executable, "-B", str(SCRIPT.with_name("gen_anim_placeholder.py")),
                                 "-o", str(folder)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        meshes = list(folder.glob("chr_placeholder_*.msh"))
        self.assertEqual(len(meshes), 11)
        triangles = sum(struct.unpack_from("<I", p.read_bytes(), 12)[0] // 3 for p in meshes)
        self.assertEqual(triangles, 156)
        blob = (folder / "chr_placeholder.anm").read_bytes()
        self.assertEqual(struct.unpack_from("<HH", blob, 8), (11, 3))
        self.assertEqual(len(blob), 8320)


if __name__ == "__main__":
    unittest.main()
