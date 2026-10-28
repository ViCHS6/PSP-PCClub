"""A003: синтетические OBJ и полный путь временного .blend (Blender через BLENDER/PATH)."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest

from PIL import Image


TOOLS = Path(__file__).resolve().parents[1]
ENV = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1"}
BLENDER = shutil.which(os.environ.get("BLENDER") or "blender")


def run_script(script, *args):
    return subprocess.run([sys.executable, "-B", str(TOOLS / script), *map(str, args)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENV,
                          timeout=300)


class ValidationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def obj(self, asset_id="prop_test", faces=1, offset=(0, 0, 0), material=""):
        positions = [(-0.5, 0, 0), (0.5, 0, 0), (0, 1, 0.5)]
        path = self.root / f"{asset_id}.obj"
        text = f"o {asset_id}\n" + material
        text += "".join("v " + " ".join(str(v + shift) for v, shift in zip(p, offset)) + "\n"
                        for p in positions)
        text += "f 1 2 3\n" * faces
        path.write_text(text, encoding="utf-8")
        return path

    def validate(self, path, *args):
        return run_script("validate_assets.py", path, *args)

    def test_within_budget(self):
        result = self.validate(self.obj(faces=60))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("60", result.stdout)

    def test_triangle_budget_exceeded(self):
        result = self.validate(self.obj(faces=61))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("бюджет треугольников", result.stdout)

    def test_category_budgets(self):
        for asset_id, limit in (("prop_fridge", 300), ("prop_workplace_a", 450),
                                ("room_test", 3000), ("chr_test", 500), ("scr_test", 2)):
            with self.subTest(asset_id=asset_id):
                self.assertEqual(self.validate(self.obj(asset_id, limit)).returncode, 0)
                result = self.validate(self.obj(asset_id, limit + 1))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("бюджет треугольников", result.stdout)

    def test_explicit_category(self):
        result = self.validate(self.obj(faces=200), "--category", "prop_test=large_prop")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_name(self):
        result = self.validate(self.obj("Prop-Wrong"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("неверное имя", result.stdout)

    def test_object_name_must_match(self):
        path = self.obj()
        path.write_text(path.read_text().replace("o prop_test", "o prop_other"), encoding="utf-8")
        result = self.validate(path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("не совпадает", result.stdout)

    def test_shifted_origin(self):
        for offset in ((2, 0, 0), (0, 2, 0), (0, 0, 2)):
            with self.subTest(offset=offset):
                result = self.validate(self.obj(offset=offset))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("origin", result.stdout)

    def textured_obj(self, size=(64, 64), names=("mat_test",)):
        texture = self.root / "tex_test.png"
        Image.new("RGBA", size, (100, 110, 120, 255)).save(texture)
        mtl = self.root / "prop_test.mtl"
        mtl.write_text("".join(f"newmtl {name}\nKd 1 1 1\nmap_Kd tex_test.png\n" for name in names),
                       encoding="utf-8")
        return self.obj(material=f"mtllib {mtl.name}\nusemtl {names[0]}\n")

    def test_texture_not_power_of_two(self):
        result = self.validate(self.textured_obj((63, 64)))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("степени двойки", result.stdout)

    def test_texture_256_is_warning(self):
        result = self.validate(self.textured_obj((256, 256)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("по согласованию", result.stdout)

    def test_material_budget(self):
        result = self.validate(self.textured_obj(names=("mat_test", "mat_second")))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("бюджет материалов", result.stdout)

    def test_invalid_material_name(self):
        result = self.validate(self.textured_obj(names=("Material.001",)))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("имя материала", result.stdout)

    def test_missing_texture(self):
        path = self.textured_obj()
        (self.root / "tex_test.png").unlink()
        self.assertNotEqual(self.validate(path).returncode, 0)

    def test_room_texture_budget(self):
        path = self.textured_obj((128, 128))
        room = path.with_name("room_test.obj")
        room.write_text(path.read_text().replace("o prop_test", "o room_test"), encoding="utf-8")
        result = self.validate(room)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("превышает (64, 64)", result.stdout)

    def test_screen_strip(self):
        path = self.root / "scr_test.png"
        Image.new("RGB", (64, 64 * 6)).save(path)
        result = self.validate(path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_build_does_not_need_blender(self):
        result = run_script("assets.py", "build", "--root", self.root, "--blender", "missing-blender")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / "assets" / "previews").exists())

    def test_underscore_blend_is_skipped(self):
        source = self.root / "assets" / "src" / "_template.blend"
        source.parent.mkdir(parents=True)
        source.write_text("not a blend", encoding="utf-8")
        result = run_script("assets.py", "build", "--root", self.root, "--blender", "missing-blender")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("нет .blend", result.stdout)

    def test_missing_blender_explains_configuration(self):
        source = self.root / "assets" / "src" / "props" / "prop_test.blend"
        source.parent.mkdir(parents=True)
        source.write_text("not a blend", encoding="utf-8")
        result = run_script("assets.py", "build", "--root", self.root, "--blender", "missing-blender")
        self.assertNotEqual(result.returncode, 0)
        for hint in ("Blender не найден", "--blender", "BLENDER", "PATH"):
            self.assertIn(hint, result.stderr)

    def test_only_missing_is_error(self):
        result = run_script("assets.py", "build", "--root", self.root, "--only", "prop_missing")
        self.assertNotEqual(result.returncode, 0)


FIXTURE = r'''
import bpy
from pathlib import Path
import sys

root = Path(sys.argv[sys.argv.index("--") + 1])
asset_id = "prop_test"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_cube_add(size=1)
obj = bpy.context.object
obj.name = asset_id
for vertex in obj.data.vertices:
    vertex.co.z += 0.5
col = obj.data.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="CORNER")
for polygon in obj.data.polygons:
    color = (0, 0, 1, 1) if polygon.normal.z > 0.5 else (
        (1, 0, 0, 1) if polygon.normal.y < -0.5 else (0, 1, 0, 1))
    for index in polygon.loop_indices:
        col.data[index].color_srgb = color
material = bpy.data.materials.new("mat_test")
material.use_nodes = True
shader = material.node_tree.nodes.get("Principled BSDF")
image = material.node_tree.nodes.new("ShaderNodeTexImage")
image.image = bpy.data.images.load(str(root / "assets/textures/tex_test.png"))
material.node_tree.links.new(image.outputs["Color"], shader.inputs["Base Color"])
obj.data.materials.append(material)
# Посторонние геометрия, камера, фон и разрешение не должны попасть в результат.
bpy.ops.mesh.primitive_cube_add(size=100, location=(50, 0, 0))
bpy.context.object.name = "unrelated"
scene = bpy.context.scene
scene.render.resolution_x = 13
scene.render.resolution_y = 17
scene.render.resolution_percentage = 10
scene.render.film_transparent = True
scene.world = bpy.data.worlds.new("bad_world")
scene.world.color = (1, 0, 1)
path = root / "assets/src/props" / (asset_id + ".blend")
path.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(path))
'''


@unittest.skipUnless(BLENDER, "Blender не найден: задайте BLENDER или добавьте blender в PATH")
class BlenderPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        texture = cls.root / "assets/textures/tex_test.png"
        texture.parent.mkdir(parents=True)
        Image.new("RGB", (32, 32), (220, 210, 190)).save(texture)
        script = cls.root / "fixture.py"
        script.write_text(textwrap.dedent(FIXTURE), encoding="utf-8")
        result = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec",
                                 "--python-exit-code", "1", "--python", str(script), "--", str(cls.root)],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENV,
                                timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.source = cls.root / "assets/src/props/prop_test.blend"

    def test_complete_pipeline(self):
        output = self.root / "custom_output"
        previews = self.root / "custom_previews"
        result = run_script("assets.py", "build", "--root", self.root, "--output", output,
                            "--previews", previews, "--only", "prop_test", "--blender", BLENDER)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        obj = output / "prop_test.obj"
        vertices = [tuple(map(float, line.split()[1:])) for line in obj.read_text().splitlines()
                    if line.startswith("v ")]
        self.assertTrue(vertices)
        self.assertTrue(all(len(v) == 6 for v in vertices))
        self.assertAlmostEqual(min(v[1] for v in vertices), 0.0)
        self.assertAlmostEqual(max(v[1] for v in vertices), 1.0)
        top = [v for v in vertices if v[5] > 0.99 and max(v[3:5]) < 0.01]
        front = [v for v in vertices if v[3] > 0.99 and max(v[4:]) < 0.01]
        self.assertTrue(top)
        self.assertTrue(front)
        self.assertTrue(all(abs(v[1] - 1.0) < 1e-6 for v in top))
        self.assertTrue(all(abs(v[2] - 0.5) < 1e-6 for v in front))
        faces = [line.split()[1:] for line in obj.read_text().splitlines() if line.startswith("f ")]
        self.assertEqual(len(faces), 12)
        self.assertTrue(all(len(face) == 3 for face in faces))
        for face in faces:
            a, b, c = [vertices[int(index.split("/")[0]) - 1][:3] for index in face]
            u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            normal = (u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0])
            center = [(a[i] + b[i] + c[i]) / 3 - (0.5 if i == 1 else 0) for i in range(3)]
            self.assertGreater(sum(normal[i] * center[i] for i in range(3)), 0)
        mtl = obj.with_suffix(".mtl").read_text()
        self.assertIn("newmtl mat_test", mtl)
        self.assertIn('map_Kd "tex_test.png"', mtl)
        mesh = obj.with_suffix(".msh").read_bytes()
        self.assertEqual(mesh[:4], b"MSH1")
        self.assertEqual(struct.unpack_from("<I", mesh, 12)[0], 36)
        self.assertEqual(struct.unpack_from("<6f", mesh, 72), (-0.5, 0.0, -0.5, 0.5, 1.0, 0.5))
        self.assertEqual((output / "tex_test.tex").read_bytes()[:4], b"TEX1")
        with Image.open(previews / "prop_test.png") as image:
            self.assertEqual(image.size, (256, 256))
            self.assertEqual(image.format, "PNG")
            self.assertGreater(len(image.getcolors(65536) or []), 10)
        self.assertFalse((self.root / "assets/previews").exists())
        self.assertFalse((self.root / "build/assets").exists())

    def test_rejects_unapplied_transforms_and_missing_object(self):
        for change in ("obj.scale.x = 2", "obj.rotation_euler.z = 0.25", 'obj.name = "wrong_name"'):
            with self.subTest(change=change):
                script = self.root / "mutate.py"
                script.write_text("import bpy, runpy, sys\n"
                                  "obj = bpy.data.objects['prop_test']\n" + change + "\n"
                                  "sys.argv = ['export', '--', '-o', " + repr(str(self.root / "bad.obj")) + "]\n"
                                  "runpy.run_path(" + repr(str(TOOLS / "blender_export.py")) + ", run_name='__main__')\n",
                                  encoding="utf-8")
                result = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", str(self.source),
                                         "--python-exit-code", "1", "--python", str(script)],
                                        capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENV,
                                        timeout=180)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("prop_test", result.stdout + result.stderr)
                self.assertFalse((self.root / "bad.obj").exists())


if __name__ == "__main__":
    unittest.main()
