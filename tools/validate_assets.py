#!/usr/bin/env python3
"""Проверка OBJ/MTL/PNG без Blender по ART.md. Допуск origin: 1 см.

py -3.12 tools/validate_assets.py build/assets --textures assets/textures
Для неоднозначного prop_*: --category prop_custom=large_prop.
"""

import argparse
from dataclasses import dataclass, field
import math
from pathlib import Path
import re
import shlex
import sys

from PIL import Image


# ART.md §4: треугольники, материалы, текстуры, максимальный размер текстуры.
BUDGETS = {
    "small_prop": (60, 1, 1, (128, 128)),
    "large_prop": (300, 2, 1, (128, 128)),
    "workplace": (450, 2, 1, (128, 128)),
    "room": (3000, 6, 6, (64, 64)),
    "character": (500, 1, 1, (64, 128)),
    "screen": (2, 1, 1, (64, 64)),
}
ASSET_NAME = re.compile(r"(?:prop|room|chr|scr)_[a-z0-9]+(?:_[a-z0-9]+)*\Z")
TEXTURE_NAME = re.compile(r"(?:prop|room|chr|scr|tex|ui)_[a-z0-9]+(?:_[a-z0-9]+)*\Z")
MATERIAL_NAME = re.compile(r"mat_[a-z0-9]+(?:_[a-z0-9]+)*\Z")


@dataclass
class Report:
    asset_id: str
    category: str = "—"
    triangles: int = 0
    materials: int = 0
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    textures: set[Path] = field(default_factory=set)


def records(path):
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        lexer = shlex.shlex(line, posix=True)
        lexer.whitespace_split = True
        lexer.escape = ""
        try:
            fields = list(lexer)
        except ValueError as error:
            raise ValueError(f"{path.name}:{number}: {error}") from error
        if fields:
            yield fields[0], fields[1:]


def category_for(asset_id):
    if asset_id.startswith("room_"):
        return "room"
    if asset_id.startswith("chr_"):
        return "character"
    if asset_id.startswith("scr_"):
        return "screen"
    kind = asset_id.removeprefix("prop_").split("_")[0]
    if kind in {"workplace", "workstation"}:
        return "workplace"
    if kind in {"counter", "shelf", "fridge", "refrigerator", "desk", "rack", "cabinet", "door"}:
        return "large_prop"
    return "small_prop"


def category_arguments(values):
    result = {}
    for value in values:
        asset_id, separator, category = value.partition("=")
        if not separator or not ASSET_NAME.fullmatch(asset_id) or category not in BUDGETS:
            raise ValueError("--category: нужно ID=" + "|".join(BUDGETS))
        result[asset_id] = category
    return result


def texture_frames(path, size):
    # ART.md §4, TEX1: анимация экрана хранится вертикальной лентой 64×64.
    if path.stem.startswith("scr_") and size[0] == 64 and size[1] > 64:
        frames, remainder = divmod(size[1], 64)
        if remainder or not 4 <= frames <= 8:
            raise ValueError("экран: нужны 4–8 кадров 64×64 в вертикальной ленте")
        return frames
    return 1


def validate_texture(path):
    report = Report(path.stem, "texture")
    if not TEXTURE_NAME.fullmatch(path.stem):
        report.errors.append("неверное имя текстуры (ART.md §7)")
    try:
        with Image.open(path) as image:
            if image.format != "PNG":
                raise ValueError("нужен PNG")
            image.load()
            frames = texture_frames(path, image.size)
            size = (image.width, image.height // frames)
            if any(n not in {32, 64, 128, 256} for n in size):
                report.errors.append(f"размер {size}: нужны степени двойки 32/64/128 (256 по согласованию)")
            elif 256 in size:
                report.warnings.append("256 — только по согласованию (ART.md §5)")
            if any(0 < a < 255 for a in image.convert("RGBA").getchannel("A").getdata()):
                report.errors.append("полупрозрачность запрещена: альфа только 0 или 255")
    except (OSError, ValueError) as error:
        report.errors.append(str(error))
    return report


def validate_asset(path, texture_dir=None, category=None, tolerance=0.01):
    path = Path(path)
    report = Report(path.stem, category or category_for(path.stem))
    if not ASSET_NAME.fullmatch(path.stem):
        report.errors.append("неверное имя ассета: prop_/room_/chr_/scr_ + snake_case (ART.md §7)")
    vertices, used, objects, libraries = [], set(), [], []
    materials, used_materials = {}, set()
    active = None
    try:
        for command, fields in records(path):
            if command == "v":
                values = tuple(float(v) for v in fields)
                if len(values) not in {3, 6} or not all(math.isfinite(v) for v in values):
                    raise ValueError("неверная вершина OBJ")
                if len(values) == 6 and any(not 0 <= c <= 1 for c in values[3:]):
                    raise ValueError("цвет вершины вне 0..1")
                vertices.append(values[:3])
            elif command == "o":
                objects.append(" ".join(fields))
            elif command == "mtllib":
                libraries.extend(path.parent / f.replace("\\", "/") for f in fields)
            elif command == "usemtl":
                active = " ".join(fields)
            elif command == "f":
                if len(fields) not in {3, 4}:
                    raise ValueError("разрешены только треугольники/квады")
                report.triangles += len(fields) - 2
                used_materials.add(active)
                for token in fields:
                    index = int(token.split("/")[0])
                    resolved = index - 1 if index > 0 else len(vertices) + index
                    if index == 0 or not 0 <= resolved < len(vertices):
                        raise ValueError("ссылка на вершину вне OBJ")
                    used.add(resolved)
        if objects and objects != [path.stem]:
            report.errors.append(f"имя объекта {objects} не совпадает с именем файла")
        for library in libraries:
            current = None
            for command, fields in records(library):
                if command == "newmtl":
                    current = " ".join(fields)
                    if not MATERIAL_NAME.fullmatch(current):
                        report.errors.append(f"неверное имя материала: {current}")
                    materials[current] = None
                elif command == "map_Kd":
                    if current is None or not fields or fields[0].startswith("-"):
                        raise ValueError("неверный map_Kd")
                    filename = Path(" ".join(fields).replace("\\", "/"))
                    if texture_dir is not None:
                        texture = Path(texture_dir) / filename.name
                    else:
                        texture = library.parent / filename
                    if texture.suffix.lower() != ".png":
                        report.errors.append(f"{texture.name}: нужна PNG-текстура")
                    materials[current] = texture
        report.materials = len(set(materials) | (used_materials - {None}))
        if None in used_materials:
            report.materials += 1
        for name in used_materials - {None}:
            if name not in materials:
                report.errors.append(f"не определён материал {name}")
        report.textures = {t for t in materials.values() if t is not None}
        triangles, material_limit, texture_limit, dimensions = BUDGETS[report.category]
        if report.triangles > triangles:
            report.errors.append(f"бюджет треугольников превышен: {report.triangles} > {triangles}")
        if report.materials > material_limit:
            report.errors.append(f"бюджет материалов превышен: {report.materials} > {material_limit}")
        if len(report.textures) > texture_limit:
            report.errors.append(f"бюджет текстур превышен: {len(report.textures)} > {texture_limit}")
        if not used:
            report.errors.append("нет граней")
        elif path.stem.startswith("prop_"):
            points = [vertices[i] for i in used]
            bottom = min(p[1] for p in points)
            if abs(bottom) > tolerance:
                report.errors.append(f"origin: минимум Y={bottom:.4g}, нужен 0 ± {tolerance} м")
            base = [p for p in points if p[1] <= bottom + tolerance]
            lo = [min(p[a] for p in base) for a in (0, 2)]
            hi = [max(p[a] for p in base) for a in (0, 2)]
            center = [(a + b) / 2 for a, b in zip(lo, hi)]
            if path.stem == "prop_door" or path.stem.startswith("prop_door_"):
                if not any(min(abs(lo[a]), abs(hi[a])) <= tolerance
                           and abs(center[1 - a]) <= tolerance for a in range(2)):
                    report.errors.append("origin двери: нужна ось петель на краю основания")
            elif any(abs(v) > tolerance for v in center):
                report.errors.append(f"origin: центр основания X/Z={center}, нужен (0, 0) ± {tolerance} м")
        for texture in sorted(report.textures):
            texture_report = validate_texture(texture)
            report.errors.extend(f"{texture.name}: {e}" for e in texture_report.errors)
            report.warnings.extend(f"{texture.name}: {w}" for w in texture_report.warnings)
            if not texture_report.errors:
                with Image.open(texture) as image:
                    size = (image.width, image.height // texture_frames(texture, image.size))
                if 256 not in size and any(n > limit for n, limit in zip(size, dimensions)):
                    report.errors.append(f"{texture.name}: {size} превышает {dimensions} для {report.category}")
    except (OSError, ValueError, KeyError, UnicodeError) as error:
        report.errors.append(str(error))
    return report


def print_reports(reports):
    print(f"{'Ассет':<32} {'Категория':<12} {'Треуг.':>7} {'Мат.':>5}  Результат")
    for report in reports:
        status = "ОШИБКА" if report.errors else ("ПРЕДУПРЕЖДЕНИЕ" if report.warnings else "OK")
        print(f"{report.asset_id:<32} {report.category:<12} {report.triangles:>7} {report.materials:>5}  {status}")
        for message in report.errors:
            print(f"  ОШИБКА: {message}")
        for message in report.warnings:
            print(f"  ПРЕДУПРЕЖДЕНИЕ: {message}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+", help="OBJ/PNG или каталог с OBJ/MTL/PNG")
    parser.add_argument("--textures", type=Path, help="каталог исходных PNG для map_Kd")
    parser.add_argument("--category", action="append", default=[], metavar="ID=CATEGORY",
                        help=", ".join(BUDGETS))
    parser.add_argument("--origin-tolerance", type=float, default=0.01)
    args = parser.parse_args(argv)
    try:
        categories = category_arguments(args.category)
        if not math.isfinite(args.origin_tolerance) or args.origin_tolerance < 0:
            raise ValueError("допуск origin должен быть конечным и неотрицательным")
        paths = set()
        for path in args.paths:
            if not path.exists():
                raise ValueError(f"путь не найден: {path}")
            if path.is_dir():
                paths.update(p for p in path.rglob("*") if p.suffix.lower() in {".obj", ".png"})
            elif path.suffix.lower() in {".obj", ".png"}:
                paths.add(path)
            else:
                raise ValueError(f"нужен OBJ/PNG или каталог: {path}")
        reports = [validate_asset(p, args.textures, categories.get(p.stem), args.origin_tolerance)
                   if p.suffix.lower() == ".obj" else validate_texture(p) for p in sorted(paths)]
        print_reports(reports)
        return int(any(r.errors for r in reports))
    except (OSError, ValueError) as error:
        print(f"ОШИБКА: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
