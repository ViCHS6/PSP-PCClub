#!/usr/bin/env python3
"""T013: детерминированные OBJ-заглушки, без внешних зависимостей."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import math
from pathlib import Path
import sys


PALETTE = {
    "wood": "8A6440", "edge": "6B4C30",
    "plastic": "D8D0B8", "aged": "B8AE94", "dark_plastic": "8F8670",
    "cloth": "2F3A55", "black": "222222",
    "green": "5E7A5A", "whitewash": "D9D2BC",
    "floor": "7A5638", "ceiling": "BFB79F",
    "blue": "2050C0", "red": "E04030", "yellow": "F0C020",
    "accent_green": "30C050", "glass": "E8F0F0",
}
WORKPLACE = ("prop_desk_chipboard", "prop_crt17", "prop_pc_case", "prop_chair_office")
BUDGETS = {"prop_admin_counter": 300, "prop_shelf_discs": 300, "room_hall_a": 3000}


def number(value: float) -> str:
    result = f"{value:.4f}".rstrip("0").rstrip(".")
    return "0" if result in ("", "-0") else result


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


@dataclass
class Face:
    points: list
    color: str
    material: str
    tiled: bool = False


@dataclass
class Mesh:
    name: str
    faces: list[Face] = field(default_factory=list)

    def face(self, points, color, normal, material="mat_body", tiled=False):
        points = list(points)
        direction = cross(subtract(points[1], points[0]), subtract(points[2], points[0]))
        if sum(a * b for a, b in zip(direction, normal)) < 0:
            points.reverse()
        if len(points) > 4:
            for i in range(1, len(points) - 1):
                self.face([points[0], points[i], points[i + 1]], color, normal, material, tiled)
        else:
            self.faces.append(Face(points, color, material, tiled))

    @property
    def triangles(self):
        return sum(len(face.points) - 2 for face in self.faces)

    @property
    def bounds(self):
        points = [point for face in self.faces for point in face.points]
        return tuple(min(p[i] for p in points) for i in range(3)), tuple(
            max(p[i] for p in points) for i in range(3))

    def write(self, directory):
        lines = ["# T013; metres; Y up; front +Z; vertex RGB", f"mtllib {self.name}.mtl", f"o {self.name}"]
        vertices, uvs, faces = [], [], []
        for face in self.faces:
            rgb = tuple(int(PALETTE[face.color][i:i + 2], 16) / 255 for i in (0, 2, 4))
            normal = cross(subtract(face.points[1], face.points[0]), subtract(face.points[2], face.points[0]))
            axis = max(range(3), key=lambda i: abs(normal[i]))
            axes = [i for i in range(3) if i != axis]
            low = [min(p[i] for p in face.points) for i in axes]
            size = [max(p[i] for p in face.points) - low[j] for j, i in enumerate(axes)]
            indices = []
            for point in face.points:
                vertices.append("v " + " ".join(number(v) for v in (*point, *rgb)))
                uv = [(point[i] - low[j]) / (1 if face.tiled else size[j]) for j, i in enumerate(axes)]
                uvs.append("vt " + " ".join(number(v) for v in uv))
                indices.append(len(vertices))
            faces.append((face.material, indices))
        lines.extend(vertices)
        lines.extend(uvs)
        material = None
        for next_material, indices in faces:
            if material != next_material:
                lines.append(f"usemtl {next_material}")
                material = next_material
            lines.append("f " + " ".join(f"{i}/{i}" for i in indices))
        write_text(directory / f"{self.name}.obj", "\n".join(lines) + "\n")
        materials = sorted({f.material for f in self.faces})
        write_text(directory / f"{self.name}.mtl", "\n\n".join(
            f"newmtl {material}\nKd 1 1 1" for material in materials) + "\n")


def write_text(path, text):
    path.write_bytes(text.encode("utf-8"))


def box(x0, y0, z0, x1, y1, z1, color, detail=False):
    # Общая сетка исключает щели от .54 + .035 != .575 в float.
    return (tuple(round(v, 4) for v in (x0, y0, z0)),
            tuple(round(v, 4) for v in (x1, y1, z1)),
            color, "mat_detail" if detail else "mat_body")


def boxes(mesh, parts, wall_back=False):
    """Граница объединения коробок: стыки деталей не выгружаются."""
    coords = [sorted({part[end][axis] for part in parts for end in (0, 1)}) for axis in range(3)]
    cells = {}
    for part in parts:
        ranges = [range(coords[i].index(part[0][i]), coords[i].index(part[1][i])) for i in range(3)]
        for x in ranges[0]:
            for y in ranges[1]:
                for z in ranges[2]:
                    cells[x, y, z] = part[2:]
    surfaces = {}
    for cell, style in cells.items():
        for axis in range(3):
            a, b = (axis + 1) % 3, (axis + 2) % 3
            for sign in (-1, 1):
                neighbor = list(cell)
                neighbor[axis] += sign
                if tuple(neighbor) in cells:
                    continue
                plane = cell[axis] + (sign == 1)
                if axis == 1 and coords[axis][plane] == 0 and sign == -1:
                    continue
                if wall_back and axis == 2 and plane == 0 and sign == -1:
                    continue
                key = (axis, sign, plane, *style)
                surfaces.setdefault(key, set()).add((cell[a], cell[b]))
    for (axis, sign, plane, color, material), tiles in sorted(surfaces.items()):
        a, b = (axis + 1) % 3, (axis + 2) % 3
        while tiles:
            u, v = min(tiles)
            width = 1
            while (u + width, v) in tiles:
                width += 1
            height = 1
            while all((i, v + height) in tiles for i in range(u, u + width)):
                height += 1
            tiles.difference_update((i, j) for i in range(u, u + width) for j in range(v, v + height))
            points = []
            for i, j in ((u, v), (u + width, v), (u + width, v + height), (u, v + height)):
                p = [0, 0, 0]
                p[axis], p[a], p[b] = coords[axis][plane], coords[a][i], coords[b][j]
                points.append(tuple(p))
            normal = [0, 0, 0]
            normal[axis] = sign
            mesh.face(points, color, normal, material)


def desk():
    mesh = Mesh("prop_desk_chipboard")
    parts = [box(-.6, .715, -.35, .6, .75, .35, "wood"),
             box(-.52, 0, -.29, -.48, .715, .29, "edge"),
             box(.48, 0, -.29, .52, .715, .29, "edge"),
             box(-.48, .43, -.29, .48, .66, -.26, "wood")]
    boxes(mesh, parts)
    return mesh


def crt():
    mesh = Mesh("prop_crt17")
    boxes(mesh, [box(-.13, 0, -.12, .13, .035, .12, "aged"),
                 box(-.045, .035, -.045, .045, .10, .045, "aged"),
                 box(-.21, .10, -.05, .21, .43, .21, "plastic")])
    # Корпус продолжится хвостом и рамкой: общие крышки не нужны.
    mesh.faces = [f for f in mesh.faces if not all(abs(p[2] - .21) < 1e-8 for p in f.points)
                  and not (all(abs(p[2] + .05) < 1e-8 for p in f.points))]
    front = [(-.21, .10, .21), (.21, .10, .21), (.21, .43, .21), (-.21, .43, .21)]
    opening = [(-.165, .155, .21), (.165, .155, .21), (.165, .395, .21), (-.165, .395, .21)]
    screen = [(x, y, .195) for x, y, _ in opening]
    back = [(-.21, .10, -.05), (.21, .10, -.05), (.21, .43, -.05), (-.21, .43, -.05)]
    tail = [(-.10, .16, -.21), (.10, .16, -.21), (.10, .35, -.21), (-.10, .35, -.21)]
    for i, normal in enumerate(((0, -1, 0), (1, 0, 0), (0, 1, 0), (-1, 0, 0))):
        j = (i + 1) % 4
        mesh.face([front[i], front[j], opening[j], opening[i]], "plastic", (0, 0, 1))
        mesh.face([opening[i], opening[j], screen[j], screen[i]], "dark_plastic", tuple(-v for v in normal))
        mesh.face([back[i], back[j], tail[j], tail[i]], "aged", normal)
    mesh.face(screen, "black", (0, 0, 1), "mat_detail")
    mesh.face(tail, "aged", (0, 0, -1))
    return mesh


def pc_case():
    mesh = Mesh("prop_pc_case")
    # Цветовые вставки лежат в той же поверхности, без наложенных полигонов.
    parts = [box(-.095, 0, -.225, .095, .43, .225, "plastic")]
    for y in (.275, .33, .385):
        parts.append(box(-.075, y, .22, .075, y + .035, .225, "aged", True))
    parts.append(box(.047, .18, .22, .067, .20, .225, "black", True))
    boxes(mesh, parts)
    return mesh


def clip_polygon(points, axis, boundary, sign):
    result = []
    for start, end in zip(points, points[1:] + points[:1]):
        d0, d1 = sign * (start[axis] - boundary), sign * (end[axis] - boundary)
        if d0 >= 0:
            result.append(start)
        if (d0 < 0 < d1) or (d1 < 0 < d0):
            t = d0 / (d0 - d1)
            result.append(tuple(a + t * (b - a) for a, b in zip(start, end)))
    unique = []
    for p in result:
        if not unique or sum((a - b) ** 2 for a, b in zip(p, unique[-1])) > 1e-16:
            unique.append(p)
    if len(unique) > 1 and sum((a - b) ** 2 for a, b in zip(unique[0], unique[-1])) < 1e-16:
        unique.pop()
    return unique


def chair():
    mesh = Mesh("prop_chair_office")
    boxes(mesh, [box(-.035, .06, -.035, .035, .405, .035, "black", True),
                 box(-.225, .405, -.225, .225, .46, .225, "cloth"),
                 box(-.035, .46, -.225, .035, .70, -.19, "black", True),
                 box(-.21, .64, -.225, .21, .93, -.17, "cloth")])
    mesh.faces = [f for f in mesh.faces if not all(abs(p[1] - .06) < 1e-8 for p in f.points)]
    outline = []
    for i in range(5):
        angle = math.pi / 2 + i * 2 * math.pi / 5
        for radius, offset in ((.30, -.075), (.30, .075), (.075, math.pi / 5)):
            outline.append((radius * math.cos(angle + offset), .06, radius * math.sin(angle + offset)))
    # Центр AABB основания совпадает с origin даже при нечётном числе лучей.
    shift_z = (min(p[2] for p in outline) + max(p[2] for p in outline)) / 2
    outline = [(x, y, z - shift_z) for x, y, z in outline]
    for a, b in zip(outline, outline[1:] + outline[:1]):
        mesh.face([a, b, (b[0], 0, b[2]), (a[0], 0, a[2])], "black",
                  (b[2] - a[2], 0, a[0] - b[0]), "mat_detail")
        triangle = [(0, .06, 0), a, b]
        # Четыре неперекрывающихся области вокруг отверстия под стойку.
        regions = (((0, -.035, -1),), ((0, .035, 1),),
                   ((0, -.035, 1), (0, .035, -1), (2, -.035, -1)),
                   ((0, -.035, 1), (0, .035, -1), (2, .035, 1)))
        for region in regions:
            polygon = triangle
            for axis, boundary, sign in region:
                polygon = clip_polygon(polygon, axis, boundary, sign)
            if len(polygon) >= 3:
                mesh.face(polygon, "black", (0, 1, 0), "mat_detail")
    return mesh


def counter():
    mesh = Mesh("prop_admin_counter")
    boxes(mesh, [box(-.9, 1.01, -.35, .9, 1.05, .35, "wood"),
                 box(-.85, 0, -.30, -.81, 1.01, .30, "edge"),
                 box(.81, 0, -.30, .85, 1.01, .30, "edge"),
                 box(-.81, 0, .26, .81, 1.01, .30, "wood"),
                 box(-.81, .48, -.30, .81, .515, .26, "wood")])
    return mesh


def shelf():
    mesh = Mesh("prop_shelf_discs")
    parts = [box(-.45, 0, -.15, -.415, 1.8, .15, "edge"),
             box(.415, 0, -.15, .45, 1.8, .15, "edge")]
    for y in (0, .54, 1.08, 1.765):
        parts.append(box(-.415, y, -.15, .415, y + .035, .15, "wood"))
    colors = ("blue", "red", "yellow", "accent_green", "cloth")
    for row, y in enumerate((.035, .575, 1.115)):
        for i in range(5):
            x = -.375 + i * .15
            parts.append(box(x, y, -.12, x + .13, y + .20, .10, colors[(i + row) % 5], True))
    boxes(mesh, parts, wall_back=True)
    return mesh


def room():
    mesh = Mesh("room_hall_a")
    mesh.face([(0, 0, 0), (8, 0, 0), (8, 0, 6), (0, 0, 6)], "floor", (0, 1, 0), "mat_floor", True)
    mesh.face([(0, 2.8, 0), (8, 2.8, 0), (8, 2.8, 6), (0, 2.8, 6)], "ceiling", (0, -1, 0), "mat_ceiling", True)
    # Проём входа x=3.4..4.6, y=0..2.1 в стене z=0.
    for x0, x1, bottom in ((0, 3.4, 0), (3.4, 4.6, 2.1), (4.6, 8, 0)):
        for y0, y1, color in ((bottom, 1.2, "green"), (max(bottom, 1.2), 2.8, "whitewash")):
            if y1 > y0:
                mesh.face([(x0, y0, 0), (x1, y0, 0), (x1, y1, 0), (x0, y1, 0)],
                          color, (0, 0, 1), "mat_wall_low" if color == "green" else "mat_wall_high", True)
    for y0, y1, color, material in ((0, 1.2, "green", "mat_wall_low"), (1.2, 2.8, "whitewash", "mat_wall_high")):
        mesh.face([(0, y0, 6), (8, y0, 6), (8, y1, 6), (0, y1, 6)], color, (0, 0, -1), material, True)
        mesh.face([(0, y0, 0), (0, y0, 6), (0, y1, 6), (0, y1, 0)], color, (1, 0, 0), material, True)
        mesh.face([(8, y0, 0), (8, y0, 6), (8, y1, 6), (8, y1, 0)], color, (-1, 0, 0), material, True)
    # Цветное окно вместо части стены: нет совпадающих поверхностей.
    mesh.faces = [f for f in mesh.faces if not (f.material == "mat_wall_high" and all(p[0] == 8 for p in f.points))]
    for z0, z1, y0, y1, color, material in (
            (0, 6, 1.2, 1.4, "whitewash", "mat_wall_high"),
            (0, 6, 2.4, 2.8, "whitewash", "mat_wall_high"),
            (0, 2, 1.4, 2.4, "whitewash", "mat_wall_high"),
            (4, 6, 1.4, 2.4, "whitewash", "mat_wall_high"),
            (2, 4, 1.4, 2.4, "glass", "mat_window")):
        mesh.face([(8, y0, z0), (8, y0, z1), (8, y1, z1), (8, y1, z0)], color, (-1, 0, 0), material, True)
    return mesh


def report(meshes):
    rows = ["| id | Треугольники | Бюджет | AABB X × Y × Z, м | Проверка |",
            "|---|---:|---:|---|:---:|"]
    passed = True
    for mesh in meshes:
        budget = BUDGETS.get(mesh.name)
        ok = budget is None or mesh.triangles <= budget
        passed &= ok
        low, high = mesh.bounds
        size = " × ".join(number(b - a) for a, b in zip(low, high))
        rows.append(f"| {mesh.name} | {mesh.triangles} | {budget if budget is not None else 'общий 450'} | {size} | {'✓' if ok else '✗'} |")
    total = sum(m.triangles for m in meshes if m.name in WORKPLACE)
    ok = total <= 450
    passed &= ok
    rows.append(f"| Рабочее место (4 пропса) | {total} | 450 | — | {'✓' if ok else '✗'} |")
    if not ok:
        rows = [row.replace("| ✓ |", "| ✗ |") if any(name in row for name in WORKPLACE) else row for row in rows]
    return "\n".join(rows), passed


def readme(table):
    return f"""# T013 — процедурные заглушки

Генератор: `py -3.12 tools/gen_placeholders.py`, другая папка:
`py -3.12 tools/gen_placeholders.py -o DIR`. Требуется Python 3.11+,
только стандартная библиотека. Файлы UTF-8, LF; без времени и случайности.
Папка по умолчанию вычисляется относительно скрипта, независимо от cwd.
README также генерируется: править источник в `tools/gen_placeholders.py`.

{table}

Размеры в таблице — **X (ширина) × Y (высота) × Z (глубина)**.
Ориентиры ART.md «ширина × глубина × высота» переведены в этот порядок.
Бюджеты: ART.md §4, GDD §10.3. Индивидуальных лимитов для четырёх
пропсов рабочего места нет, проверяется их сумма. ART.md включает также
клавиатуру, мышь и наушники: оставшийся запас предназначен для них;
в состав T013 они по заданию не входят.

Оси: правая система, **Y вверх, +Z вперёд**, метры. Экран и передняя панель
ПК смотрят в +Z; человек на стуле сидит лицом в +Z (спинка со стороны −Z).
У всех пропсов min Y = 0, X/Z центрированы по AABB основания.
У монитора origin — центр нижней грани подставки. Для установки на стол
нужно прибавить 0,75 м по Y. У комнаты origin — угол (0, 0, 0), пол Y=0,
границы X=0..8, Z=0..6, потолок Y=2,8. Мебели в room_hall_a нет.

Силуэты: столешница, две боковины и царга; ЭЛТ с утопленным экраном,
рамкой, сужающимся хвостом и подставкой; midi-tower с тремя отсеками
5,25″ и кнопкой цветом; стул с сиденьем Y=0,46, спинкой и пятью лучами;
прямая стойка с полкой с рабочей стороны −Z; открытый стеллаж с тремя
рядами разноцветных коробок. Стеллаж ставится задней стороной −Z к стене.

Цвета RGB из ART.md §2 записаны в `v x y z r g b`; тёмно-серый экран
использует #222222. Все Kd = 1 1 1, текстур и нормалей нет. Материалы
mat_body/mat_detail общие для пропсов (всё рабочее место — два материала).
Комната использует пять материалов. На пропсах планарные UV 0..1 по
грани с сознательным повтором; в комнате 1 UV = 1 м (тайлинг).
Обход CCW снаружи пропсов, внутрь комнаты. Грани у пола, стыки коробок,
задние грани стеллажа у стены удалены. Комната — только внутренние
поверхности, вход открыт; окно выделено цветом без наложенной стены.

## Ориентир расстановки для T014

Это рекомендации, не формат сцены. Поворот — вокруг +Y по правилу правой
руки: +90° переводит +Z в +X. В строках мест указаны origin стола и
поворот экрана/ПК. Стул развёрнут к экрану на дополнительные 180°.

| Место | Стол (X, Y, Z), м | Экран/ПК, поворот Y |
|---|---|---:|
| 1 | (1,6; 0; 5,5) | 180° |
| 2 | (4,0; 0; 5,5) | 180° |
| 3 | (6,4; 0; 5,5) | 180° |
| 4 | (1,6; 0; 2,6) | 180° |
| 5 | (4,0; 0; 2,6) | 180° |
| 6 | (6,4; 0; 2,6) | 180° |

Локальные смещения относительно стола до поворота: монитор (−0,12;
0,75; −0,08), ПК (+0,37; 0; −0,02), стул (0; 0; +0,85).
Для всех шести мест повернуть также смещения на 180°: экраны смотрят
в сторону входа (−Z), стулья стоят перед столами и смотрят в +Z.
ПК стоит под столом: верх Y=0,43, внутренний край боковины X=0,48,
внешний край ПК X=0,465; корпус не пересекает боковину и царгу.

Стойка: origin (1,7; 0; 0,65), поворот 0°, клиентская сторона +Z,
рабочая полка −Z; от стены до стойки 0,30 м (там проход не предполагается).
Стеллаж: origin (6,7; 0; 0,15), поворот 0° (задняя сторона у z=0).
Вход: центр (4; 0; 0), ширина 1,2 м (X=3,4..4,6), высота 2,1 м,
направление входа +Z. Окно: стена X=8, Z=2..4, Y=1,4..2,4.

Столы занимают по X [1..2,2], [3,4..4,6], [5,8..7],
между ними 1,2 м, боковые проходы по 1 м. По Z столы занимают
[2,25..2,95] и [5,15..5,85], стулья (с запасом AABB 0,60 м)
[1,45..2,05] и [4,35..4,95]. Между рядами свободно 1,40 м
(Z=2,95..4,35). Между стойкой (до Z=1) и ближним столом 1,25 м;
путь от входа идёт между столами и обходит занятые стульями участки
по боковым проходам и промежуткам 1,2 м. Зазоры стол–стул и стена–стол
не считаются проходами. Все основные проходы ≥0,9 м.

## Проверка

`py -3.12 -m unittest discover -s tools/tests -p "test_gen_placeholders.py" -v`

Тесты запускают CLI во временной папке и независимо разбирают OBJ/MTL:
бюджеты, размеры и origin, индексы, RGB/UV, нормали граней и отсутствие
внутренних поверхностей, проём входа, палитра, детерминированность.
PC/PSP-сборки и проверку на PSP Go следует выполнять отдельно;
генератор не проверяет игровой рендер и производительность.
Изменения контрактов: нет.
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets" / "placeholders")
    args = parser.parse_args(argv)
    meshes = [desk(), crt(), pc_case(), chair(), counter(), shelf(), room()]
    table, passed = report(meshes)
    args.output.mkdir(parents=True, exist_ok=True)
    for mesh in meshes:
        mesh.write(args.output)
    write_text(args.output / "README.md", readme(table))
    # Windows-консоль может иметь cp1251 без символов галочек.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    print(table)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
