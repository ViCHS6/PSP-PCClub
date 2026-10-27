"""Независимая проверка результатов CLI T013; генератор не импортируется."""

import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "gen_placeholders.py"
EXPECTED = {
    "prop_desk_chipboard": (1.2, .75, .7),
    "prop_crt17": (.42, .43, .42),
    "prop_pc_case": (.19, .43, .45),
    "prop_chair_office": (.60, .93, .60),
    "prop_admin_counter": (1.8, 1.05, .7),
    "prop_shelf_discs": (.9, 1.8, .3),
    "room_hall_a": (8, 2.8, 6),
}
WORKPLACE = ("prop_desk_chipboard", "prop_crt17", "prop_pc_case", "prop_chair_office")
LIMITS = {"prop_admin_counter": 300, "prop_shelf_discs": 300, "room_hall_a": 3000}
COLORS = {
    "5E7A5A", "4B6449", "D9D2BC", "BFB79F", "7A5638", "5E412A",
    "8A6440", "6B4C30", "D8D0B8", "B8AE94", "8F8670", "2F3A55",
    "222222", "E8F0F0", "FFD9A0", "2050C0", "30C050", "E04030", "F0C020",
}


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normal(points):
    return cross(sub(points[1], points[0]), sub(points[2], points[0]))


def ray_hit(origin, direction, triangle):
    a, b, c = triangle
    edge1, edge2 = sub(b, a), sub(c, a)
    p = cross(direction, edge2)
    det = dot(edge1, p)
    if abs(det) < 1e-12:
        return None
    offset = sub(origin, a)
    u = dot(offset, p) / det
    q = cross(offset, edge1)
    v = dot(direction, q) / det
    distance = dot(edge2, q) / det
    if u >= -1e-9 and v >= -1e-9 and u + v <= 1 + 1e-9 and distance > 1e-7:
        return distance
    return None


def inside(point, triangles):
    # Луч идёт вверх и от стены: отсутствующие нижние/задние грани не нужны.
    hits = sorted(t for triangle in triangles
                  if (t := ray_hit(point, (.413, .317, .853), triangle)) is not None)
    unique = []
    for t in hits:
        if not unique or abs(t - unique[-1]) > 1e-7:
            unique.append(t)
    return len(unique) % 2 == 1


class Obj:
    def __init__(self, path):
        self.path = path
        self.vertices, self.colors, self.uvs, self.faces = [], [], [], []
        self.objects, self.libraries, self.materials = [], [], set()
        self.numeric_tokens = []
        current_material = None
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            tag, values = fields[0], fields[1:]
            if tag == "v":
                if len(values) != 6:
                    raise ValueError("Vertex must contain XYZ RGB")
                numbers = tuple(map(float, values))
                self.vertices.append(numbers[:3])
                self.colors.append(numbers[3:])
                self.numeric_tokens.extend(values)
            elif tag == "vt":
                if len(values) != 2:
                    raise ValueError("UV must contain two values")
                self.uvs.append(tuple(map(float, values)))
                self.numeric_tokens.extend(values)
            elif tag == "f":
                refs = []
                for value in values:
                    indices = value.split("/")
                    if len(indices) != 2 or not all(indices):
                        raise ValueError("Expected v/vt, without normals")
                    refs.append(tuple(map(int, indices)))
                self.faces.append((refs, current_material))
            elif tag == "o":
                self.objects.append(" ".join(values))
            elif tag == "mtllib":
                self.libraries.append(" ".join(values))
            elif tag == "usemtl":
                current_material = " ".join(values)
                self.materials.add(current_material)
            else:
                raise ValueError(f"Unexpected OBJ record: {tag}")

    def points(self, face):
        return [self.vertices[v - 1] for v, _ in face[0]]

    @property
    def triangles(self):
        result = []
        for face in self.faces:
            points = self.points(face)
            result.extend((points[0], points[i], points[i + 1]) for i in range(1, len(points) - 1))
        return result

    @property
    def bounds(self):
        return tuple(min(p[i] for p in self.vertices) for i in range(3)), tuple(
            max(p[i] for p in self.vertices) for i in range(3))


class PlaceholderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="t013_")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.output = Path(cls.temporary.name) / "models with spaces"
        cls.result = cls.generate(cls.output)
        if cls.result.returncode != 0:
            raise AssertionError(cls.result.stdout + cls.result.stderr)
        cls.models = {name: Obj(cls.output / f"{name}.obj") for name in EXPECTED}

    @staticmethod
    def generate(directory, cwd=None, long_option=False):
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONIOENCODING="utf-8")
        return subprocess.run([sys.executable, "-B", str(SCRIPT),
                               "--output" if long_option else "-o", str(directory)],
                              cwd=cwd, env=env, capture_output=True, encoding="utf-8", timeout=30)

    def test_complete_output_and_report(self):
        expected = {f"{name}.{suffix}" for name in EXPECTED for suffix in ("obj", "mtl")} | {"README.md"}
        self.assertEqual({p.name for p in self.output.iterdir()}, expected)
        readme = (self.output / "README.md").read_text(encoding="utf-8")
        self.assertIn(self.result.stdout.strip(), readme)
        self.assertEqual(self.result.stderr, "")
        self.assertNotIn("✗", self.result.stdout)
        for name, obj in self.models.items():
            self.assertRegex(self.result.stdout, rf"\| {name} \| {len(obj.triangles)} \|")
        self.assertIn("+Z", readme)
        self.assertIn("T014", readme)

    def test_budgets(self):
        total = sum(len(self.models[name].triangles) for name in WORKPLACE)
        self.assertLessEqual(total, 450)
        for name, limit in LIMITS.items():
            with self.subTest(model=name):
                self.assertLessEqual(len(self.models[name].triangles), limit)

    def test_dimensions(self):
        for name, obj in self.models.items():
            with self.subTest(model=name):
                low, high = obj.bounds
                for actual, expected in zip(sub(high, low), EXPECTED[name]):
                    self.assertAlmostEqual(actual, expected, delta=expected * .1)

    def test_origins(self):
        for name, obj in self.models.items():
            low, high = obj.bounds
            with self.subTest(model=name):
                self.assertAlmostEqual(low[1], 0)
                if name.startswith("prop_"):
                    for axis in (0, 2):
                        self.assertAlmostEqual(low[axis] + high[axis], 0, delta=.00011)
                    base = [p for p in obj.vertices if p[1] == 0]
                    self.assertTrue(base)
                    for axis in (0, 2):
                        self.assertAlmostEqual(min(p[axis] for p in base) + max(p[axis] for p in base), 0, delta=.00011)
                else:
                    self.assertEqual(low, (0, 0, 0))

    def test_faces_indices_and_object_names(self):
        for name, obj in self.models.items():
            with self.subTest(model=name):
                self.assertEqual(obj.objects, [name])
                self.assertEqual(obj.libraries, [f"{name}.mtl"])
                used_vertices, used_uvs = set(), set()
                self.assertTrue(obj.faces)
                for refs, material in obj.faces:
                    self.assertIn(len(refs), (3, 4))
                    self.assertIsNotNone(material)
                    for v, uv in refs:
                        self.assertTrue(1 <= v <= len(obj.vertices))
                        self.assertTrue(1 <= uv <= len(obj.uvs))
                        used_vertices.add(v)
                        used_uvs.add(uv)
                self.assertEqual(used_vertices, set(range(1, len(obj.vertices) + 1)))
                self.assertEqual(used_uvs, set(range(1, len(obj.uvs) + 1)))

    def test_vertex_colors_and_palette(self):
        palette = [tuple(int(color[i:i + 2], 16) / 255 for i in (0, 2, 4)) for color in COLORS]
        for name, obj in self.models.items():
            with self.subTest(model=name):
                for rgb in obj.colors:
                    self.assertTrue(all(math.isfinite(c) and 0 <= c <= 1 for c in rgb))
                    self.assertTrue(any(all(abs(a - b) < .000051 for a, b in zip(rgb, entry)) for entry in palette))

    def test_materials(self):
        for name, obj in self.models.items():
            with self.subTest(model=name):
                records = (self.output / f"{name}.mtl").read_text(encoding="utf-8").splitlines()
                materials, kd_count = set(), 0
                for line in records:
                    if not line:
                        continue
                    tag, *values = line.split()
                    self.assertIn(tag, ("newmtl", "Kd"))
                    if tag == "newmtl":
                        self.assertEqual(len(values), 1)
                        self.assertRegex(values[0], r"^mat_[a-z_]+$")
                        materials.add(values[0])
                    else:
                        self.assertEqual(tuple(map(float, values)), (1, 1, 1))
                        kd_count += 1
                self.assertEqual(materials, obj.materials)
                self.assertEqual(kd_count, len(materials))
                self.assertLessEqual(len(materials), 6 if name.startswith("room_") else 2)
        self.assertLessEqual(len(set.union(*(self.models[n].materials for n in WORKPLACE))), 2)

    def test_uvs_and_precision(self):
        for name, obj in self.models.items():
            with self.subTest(model=name):
                for token in obj.numeric_tokens:
                    self.assertRegex(token, r"^-?\d+(?:\.\d{1,4})?$")
                for uv in obj.uvs:
                    self.assertTrue(all(math.isfinite(v) and v >= 0 for v in uv))
                    if name.startswith("prop_"):
                        self.assertTrue(all(v <= 1 for v in uv))
                for face in obj.faces:
                    points = obj.points(face)
                    uv = [obj.uvs[t - 1] for _, t in face[0]]
                    for i in range(1, len(points) - 1):
                        a, b = sub(uv[i], uv[0]), sub(uv[i + 1], uv[0])
                        uv_area = abs(a[0] * b[1] - a[1] * b[0])
                        self.assertGreater(uv_area, 1e-8)
                    if name.startswith("room_"):
                        uv_edges = [math.dist(a, b) for a, b in zip(uv, uv[1:] + uv[:1])]
                        edges = [math.dist(a, b) for a, b in zip(points, points[1:] + points[:1])]
                        for actual, expected in zip(uv_edges, edges):
                            self.assertAlmostEqual(actual, expected, delta=.0002)
        self.assertTrue(any(v > 1 for uv in self.models["room_hall_a"].uvs for v in uv))

    def test_faces_are_planar_convex_and_non_degenerate(self):
        for name, obj in self.models.items():
            for index, face in enumerate(obj.faces):
                with self.subTest(model=name, face=index):
                    points = obj.points(face)
                    n = normal(points)
                    length = math.sqrt(dot(n, n))
                    self.assertGreater(length, 1e-8)
                    for point in points[3:]:
                        self.assertAlmostEqual(dot(n, sub(point, points[0])) / length, 0, delta=.00011)
                    for i in range(len(points)):
                        turn = cross(sub(points[(i + 1) % len(points)], points[i]),
                                     sub(points[(i + 2) % len(points)], points[(i + 1) % len(points)]))
                        self.assertGreater(dot(n, turn), 0)

    def test_prop_winding_and_no_hidden_surfaces(self):
        for name, obj in self.models.items():
            if name.startswith("room_"):
                continue
            triangles = obj.triangles
            volume = sum(dot(a, cross(b, c)) / 6 for a, b, c in triangles)
            self.assertGreater(volume, 0, name)
            for index, face in enumerate(obj.faces):
                with self.subTest(model=name, face=index):
                    points = obj.points(face)
                    center = tuple(sum(p[i] for p in points) / len(points) for i in range(3))
                    n = normal(points)
                    length = math.sqrt(dot(n, n))
                    self.assertGreater(length, 1e-8)
                    scale = .00002 / length
                    outside = tuple(c + v * scale for c, v in zip(center, n))
                    interior = tuple(c - v * scale for c, v in zip(center, n))
                    self.assertFalse(inside(outside, triangles), "Normal points into solid or hidden face")
                    self.assertTrue(inside(interior, triangles), "Normal does not leave solid")

    def test_no_floor_caps_or_shelf_wall_caps(self):
        for name, obj in self.models.items():
            if name.startswith("room_"):
                continue
            for face in obj.faces:
                points = obj.points(face)
                self.assertFalse(all(p[1] == 0 for p in points), name)
                if name == "prop_shelf_discs":
                    self.assertFalse(all(p[2] == -.15 for p in points))

    def test_room_inward_faces_and_door(self):
        obj = self.models["room_hall_a"]
        center = (4, 1.4, 3)
        for face in obj.faces:
            points = obj.points(face)
            self.assertGreater(dot(normal(points), sub(center, points[0])), 0)
            self.assertTrue(any(all(p[axis] == boundary for p in points)
                                for axis, boundaries in enumerate(((0, 8), (0, 2.8), (0, 6)))
                                for boundary in boundaries))
        for x in (3.41, 4, 4.59):
            for y in (.01, 1, 2.09):
                self.assertFalse(any(ray_hit((x, y, -.1), (0, 0, 1), tri) is not None
                                     for tri in obj.triangles if all(p[2] == 0 for p in tri)))
        for point in ((3.39, 1, -.1), (4.61, 1, -.1), (4, 2.11, -.1)):
            self.assertTrue(any(ray_hit(point, (0, 0, 1), tri) is not None
                                for tri in obj.triangles if all(p[2] == 0 for p in tri)))

    def test_distinctive_shapes_and_front_direction(self):
        chair = self.models["prop_chair_office"]
        seat = [chair.points(f) for f in chair.faces if all(p[1] == .46 for p in chair.points(f))]
        self.assertTrue(seat)
        points = [p for face in seat for p in face]
        for axis in (0, 2):
            self.assertAlmostEqual(max(p[axis] for p in points) - min(p[axis] for p in points), .45)
        self.assertTrue(all(p[2] < 0 for p in chair.vertices if p[1] > .64))
        base = set(p for p in chair.vertices if p[1] == 0)
        # Два внешних угла каждого из пяти лучей.
        self.assertEqual(sum(math.hypot(p[0], p[2]) > .23 for p in base), 10)
        crt = self.models["prop_crt17"]
        screens = [crt.points(f) for f in crt.faces if f[1] == "mat_detail"]
        self.assertEqual(len(screens), 1)
        self.assertTrue(all(p[2] == .195 for p in screens[0]))
        self.assertGreater(normal(screens[0])[2], 0)
        rear = [p for p in crt.vertices if p[2] == -.21]
        self.assertAlmostEqual(max(p[0] for p in rear) - min(p[0] for p in rear), .20)
        pc = self.models["prop_pc_case"]
        details = [pc.points(f) for f in pc.faces if f[1] == "mat_detail"]
        self.assertEqual(len(details), 4)
        self.assertTrue(all(p[2] == .225 for face in details for p in face))
        self.assertTrue(all(normal(face)[2] > 0 for face in details))

    def test_determinism_and_arbitrary_cwd(self):
        snapshot = {p.name: p.read_bytes() for p in self.output.iterdir()}
        result = self.generate(self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, self.result.stdout)
        self.assertEqual(snapshot, {p.name: p.read_bytes() for p in self.output.iterdir()})
        second = Path(self.temporary.name) / "second"
        result = self.generate(second, cwd=self.temporary.name, long_option=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(snapshot, {p.name: p.read_bytes() for p in second.iterdir()})

    def test_checked_in_assets_match_generator(self):
        directory = ROOT / "assets" / "placeholders"
        for generated in self.output.iterdir():
            with self.subTest(file=generated.name):
                self.assertEqual((directory / generated.name).read_bytes(), generated.read_bytes())

    def test_default_output_is_relative_to_script(self):
        sandbox = Path(self.temporary.name) / "default output"
        (sandbox / "tools").mkdir(parents=True)
        script = sandbox / "tools" / SCRIPT.name
        shutil.copyfile(SCRIPT, script)
        result = subprocess.run([sys.executable, "-B", str(script)], cwd=self.temporary.name,
                                capture_output=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = {p.name: p.read_bytes() for p in self.output.iterdir()}
        actual = {p.name: p.read_bytes() for p in (sandbox / "assets" / "placeholders").iterdir()}
        self.assertEqual(expected, actual)

    def test_recommended_layout_fits_room_and_has_aisles(self):
        readme = (self.output / "README.md").read_text(encoding="utf-8")
        rows = re.findall(r"\| ([1-6]) \| \(([\d,]+); 0; ([\d,]+)\) \| (\d+)° \|", readme)
        self.assertEqual(len(rows), 6)
        footprints = []
        for _, x, z, angle in rows:
            x, z, angle = float(x.replace(",", ".")), float(z.replace(",", ".")), math.radians(float(angle))
            placed = []
            for name, offset in (("prop_desk_chipboard", (0, 0, 0)),
                                 ("prop_crt17", (-.12, .75, -.08)),
                                 ("prop_pc_case", (.37, 0, -.02)),
                                 ("prop_chair_office", (0, 0, .85))):
                for point in self.models[name].vertices:
                    local = tuple(a + b for a, b in zip(point, offset))
                    world = (x + local[0] * math.cos(angle) + local[2] * math.sin(angle),
                             local[1], z - local[0] * math.sin(angle) + local[2] * math.cos(angle))
                    self.assertTrue(all(-1e-6 <= v <= limit + 1e-6 for v, limit in zip(world, (8, 2.8, 6))),
                                    f"Outside room: {name}, {world}")
                    placed.append(world)
            footprints.append((min(p[0] for p in placed), max(p[0] for p in placed),
                               min(p[2] for p in placed), max(p[2] for p in placed)))
        for row in (footprints[:3], footprints[3:]):
            self.assertGreaterEqual(row[0][0], .9)
            self.assertGreaterEqual(8 - row[-1][1], .9)
            for a, b in zip(row, row[1:]):
                self.assertGreaterEqual(b[0] - a[1], .9)
        self.assertGreaterEqual(min(p[2] for p in footprints[:3]) - max(p[3] for p in footprints[3:]), .9)


if __name__ == "__main__":
    unittest.main()
