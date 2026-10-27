"""Проверки MSH1 по двоичному контракту, без импорта конвертера."""

import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "convert_model.py"


def parse_mesh(blob):
    header = struct.unpack_from("<4sHHHHIIIII16f", blob)
    names = ("magic version flags vertex_count submesh_count index_count vertices_offset "
             "indices_offset submeshes_offset reserved").split()
    result = dict(zip(names, header[:10]))
    result.update(pos_offset=header[10:13], pos_scale=header[13:16],
                  uv_offset=header[16:18], uv_scale=header[18:20],
                  bbox_min=header[20:23], bbox_max=header[23:26])
    result["submeshes"] = [struct.unpack_from("<II32sII", blob, result["submeshes_offset"] + 48 * i)
                           for i in range(result["submesh_count"])]
    result["vertices"] = [struct.unpack_from("<HH4BhhhH", blob, result["vertices_offset"] + 16 * i)
                          for i in range(result["vertex_count"])]
    result["indices"] = struct.unpack_from(f"<{result['index_count']}H", blob, result["indices_offset"])
    return result


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_model(self, text, success=True):
        source, output = self.root / "model.obj", self.root / "model.msh"
        source.write_text(text, encoding="utf-8")
        process = subprocess.run([sys.executable, "-B", str(SCRIPT), str(source), "-o", str(output), "--report"],
                                 capture_output=True, text=True, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        if success:
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertIn("triangles=", process.stdout)
            self.assertIn("AABB=", process.stdout)
            return output.read_bytes()
        self.assertNotEqual(process.returncode, 0)
        self.assertNotIn("Traceback", process.stderr)
        self.assertIn(str(source), process.stderr)
        return process.stderr

    def test_cube_two_materials_layout_decode_colors_and_uv(self):
        (self.root / "cube.mtl").write_text(
            'newmtl red\nKd .5 .25 1\nmap_Kd "folder/tex_red.png"\n'
            'newmtl blue\nKd 1 .5 .5\nmap_Kd C:\\textures\\tex_blue.png\n', encoding="utf-8")
        positions = [(x, y, z) for z in (4, 10) for y in (-2, 2) for x in (1, 3)]
        uv = [(-1, -.5), (2, -.5), (2, 1.5), (-1, 1.5)]
        faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4),
                 (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
        text = "mtllib cube.mtl\n"
        text += "".join(f"v {x} {y} {z} .5 1 .25\n" for x, y, z in positions)
        text += "".join(f"vt {u} {v}\n" for u, v in uv)
        expected = [[], []]
        unique = set()
        for i, face in enumerate(faces):
            mat = i % 2
            text += f"usemtl {'red' if mat == 0 else 'blue'}\nf "
            text += " ".join(f"{p + 1}/{j + 1}" for j, p in enumerate(face)) + "\n"
            rgb = (64, 64, 64, 255) if mat == 0 else (128, 128, 32, 255)
            for corner in (0, 1, 2, 0, 2, 3):
                key = (positions[face[corner]], (uv[corner][0], 1 - uv[corner][1]), rgb)
                expected[mat].append(key)
                unique.add(key)
        blob = self.run_model(text)
        mesh = parse_mesh(blob)
        self.assertEqual((mesh["magic"], mesh["version"], mesh["flags"], mesh["reserved"]), (b"MSH1", 1, 0, 0))
        self.assertEqual(mesh["submeshes_offset"], 96)
        self.assertEqual(mesh["vertices_offset"], 192)
        self.assertEqual(mesh["vertices_offset"] % 16, 0)
        self.assertEqual(mesh["vertex_count"], len(unique))
        self.assertEqual(mesh["index_count"], 36)
        self.assertEqual(mesh["submesh_count"], 2)
        self.assertEqual(mesh["indices_offset"], 192 + len(unique) * 16)
        end = mesh["indices_offset"] + 72
        self.assertEqual(len(blob), (end + 3) & ~3)
        self.assertEqual(blob[end:], bytes((-end) % 4))
        self.assertEqual(mesh["bbox_min"], (1, -2, 4))
        self.assertEqual(mesh["bbox_max"], (3, 2, 10))
        self.assertEqual(mesh["pos_offset"], (2, 0, 7))
        self.assertEqual(mesh["pos_scale"], (1, 2, 3))
        self.assertEqual(mesh["uv_offset"], (-1, -.5))
        for mat, subset in enumerate(mesh["submeshes"]):
            start, count, texture, r0, r1 = subset
            self.assertEqual((start, count, r0, r1), (mat * 18, 18, 0, 0))
            self.assertEqual(texture, (b"tex_red" if mat == 0 else b"tex_blue").ljust(32, b"\0"))
            for index, (position, texcoord, color) in zip(mesh["indices"][start:start + count], expected[mat]):
                self.assertLess(index, mesh["vertex_count"])
                vertex = mesh["vertices"][index]
                self.assertEqual(vertex[2:6], color)
                self.assertEqual(vertex[9], 0)
                for a in range(3):
                    decoded = mesh["pos_offset"][a] + vertex[6 + a] / 32768 * mesh["pos_scale"][a]
                    self.assertAlmostEqual(decoded, position[a], delta=mesh["pos_scale"][a] / 32768)
                for a in range(2):
                    decoded = mesh["uv_offset"][a] + vertex[a] / 32768 * mesh["uv_scale"][a]
                    self.assertAlmostEqual(decoded, texcoord[a], delta=mesh["uv_scale"][a] / 32768)

    def test_quad_fan_default_color_missing_uv_and_dedup(self):
        blob = self.run_model("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                              "v 0 0 0\nf 1 2 3 4\nf 5 2 3\n")
        mesh = parse_mesh(blob)
        self.assertEqual(mesh["vertex_count"], 4)
        self.assertEqual(mesh["indices"], (0, 1, 2, 0, 2, 3, 0, 1, 2))
        self.assertEqual(mesh["pos_scale"][2], 1)
        self.assertEqual(mesh["uv_scale"], (1, 1))
        for vertex in mesh["vertices"]:
            self.assertEqual(vertex[:6], (0, 0, 255, 255, 255, 255))
        self.assertEqual(blob[-2:], b"\0\0")

    def test_negative_indices_normals_and_material_without_vertex_colors(self):
        (self.root / "simple.mtl").write_text("newmtl gray\nKd .2 .4 .6\n", encoding="utf-8")
        mesh = parse_mesh(self.run_model("mtllib simple.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                                         "vt .25 .75\nvn 0 0 1\nusemtl gray\nf -3/-1/-1 -2/-1/-1 -1/-1/-1\n"))
        self.assertEqual(mesh["uv_offset"], (.25, .25))
        self.assertEqual(mesh["uv_scale"], (1, 1))
        for vertex in mesh["vertices"]:
            self.assertEqual(vertex[2:6], (51, 102, 153, 255))

    def test_deduplication_happens_after_quantization(self):
        mesh = parse_mesh(self.run_model("v 0 0 0\nv .00000001 0 0\nv 1 0 0\nv 0 1 0\n"
                                         "f 1 3 4\nf 2 3 4\n"))
        self.assertEqual(mesh["vertex_count"], 3)
        self.assertEqual(mesh["indices"], (0, 1, 2, 0, 1, 2))

    def test_large_index_count_is_uint32(self):
        mesh = parse_mesh(self.run_model("v 0 0 0\nv 1 0 0\nv 0 1 0\n" + "f 1 2 3\n" * 21846))
        self.assertEqual(mesh["index_count"], 65538)
        self.assertEqual(mesh["submeshes"][0][:2], (0, 65538))
        self.assertEqual(mesh["vertex_count"], 3)

    def test_ngon_winding_and_vertex_colors_without_material(self):
        mesh = parse_mesh(self.run_model("v 0 0 0 1 0 0\nv 2 0 0 0 1 0\nv 3 1 0 0 0 1\n"
                                         "v 1 3 0 1 1 1\nv 0 1 0 .5 .5 .5\nf 1 2 3 4 5\n"))
        self.assertEqual(mesh["indices"], (0, 1, 2, 0, 2, 3, 0, 3, 4))
        self.assertEqual([vertex[2:6] for vertex in mesh["vertices"]],
                         [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
                          (255, 255, 255, 255), (128, 128, 128, 255)])

    def test_broken_references_and_malformed_numbers(self):
        prefix = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        for face in ("f 1 2 4", "f 0 2 3", "f -4 2 3", "f 1/1 2 3", "f 1//1 2 3"):
            with self.subTest(face=face):
                error = self.run_model(prefix + face + "\n", success=False)
                self.assertIn("model.obj:4:", error)
                self.assertIn("reference", error)
        self.assertIn("model.obj:1:", self.run_model("v nan 0 0\n", success=False))

    def test_undefined_material_and_invalid_texture_id(self):
        text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl missing\nf 1 2 3\n"
        self.assertIn("undefined material", self.run_model(text, success=False))
        (self.root / "bad.mtl").write_text("newmtl missing\nmap_Kd " + "a" * 32 + ".png\n", encoding="utf-8")
        self.assertIn("bad.mtl:2:", self.run_model("mtllib bad.mtl\n" + text, success=False))

    def test_vertex_limit_after_deduplication(self):
        text = "v 0 0 0\n" + "".join(f"vt {i % 256} {i // 256}\n" for i in range(65536))
        text += "".join(f"f 1/{i + 1} 1/{i + 2} 1/{i + 3}\n" for i in range(0, 65535, 3))
        mesh = parse_mesh(self.run_model(text))
        self.assertEqual(mesh["vertex_count"], 65535)
        self.assertEqual(max(mesh["indices"]), 65534)
        error = self.run_model(text + "f 1/65536 1/1 1/2\n", success=False)
        self.assertIn("more than 65535 vertices", error)


if __name__ == "__main__":
    unittest.main()
