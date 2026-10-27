#!/usr/bin/env python3
"""Конвейер A003: .blend → OBJ/MTL → MSH1/TEX1 → проверка → превью.

py -3.12 tools/assets.py build [--only prop_crt17] [--blender PATH]
--root задаёт корень дерева assets/, --output и --previews переопределяют выходы
(относительные пути отсчитываются от --root). Неоднозначная категория пропса:
--category prop_custom=large_prop. Прочие категории: small_prop/workplace/room/
character/screen; автоматический выбор — validate_assets.category_for.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

from validate_assets import (ASSET_NAME, BUDGETS, category_arguments, print_reports,
                             texture_frames, validate_asset, validate_texture)


TOOLS = Path(__file__).resolve().parent


def find_blender(explicit=None):
    candidate = explicit or os.environ.get("BLENDER") or "blender"
    executable = shutil.which(candidate)
    if not executable:
        raise ValueError("Blender не найден. Укажите --blender PATH, задайте переменную "
                         "окружения BLENDER или добавьте blender в PATH.")
    return executable


def run_command(command):
    process = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                             errors="replace", timeout=300,
                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1"})
    if process.returncode:
        raise ValueError(f"{Path(command[0]).name}: код {process.returncode}\n"
                         + process.stdout[-12000:] + process.stderr[-12000:])


def run_blender(blender, source, script, output):
    run_command([blender, "--background", "--factory-startup", "--disable-autoexec", str(source),
                 "--python-exit-code", "1", "--python", str(TOOLS / script), "--", "-o", str(output)])


def rooted(root, path, default):
    path = path if path is not None else Path(default)
    return (path if path.is_absolute() else root / path).resolve()


def build(args):
    root = args.root.resolve()
    output = rooted(root, args.output, "build/assets")
    previews = rooted(root, args.previews, "assets/previews")
    textures = root / "assets" / "textures"
    sources = sorted((root / "assets" / "src").rglob("*.blend"))
    categories = category_arguments(args.category)
    if args.only:
        sources = [source for source in sources if source.stem == args.only]
        if not sources:
            raise ValueError(f"ассет --only {args.only} не найден")
    seen = set()
    for source in sources:
        if not ASSET_NAME.fullmatch(source.stem):
            raise ValueError(f"{source}: неверное имя ассета (ART.md §7)")
        if source.stem.lower() in seen:
            raise ValueError(f"повторяющийся id: {source.stem}")
        seen.add(source.stem.lower())
    blender = find_blender(args.blender) if sources else None
    if not sources:
        print("assets/src/: нет .blend, это не ошибка.")
    reports, errors = [], []
    converted_textures = 0
    completed = 0
    texture_paths = sorted(textures.glob("*.png"))
    if not sources and not texture_paths:
        print("Итого: ассетов 0, текстур 0, ошибок 0.")
        return 0
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".assets-", dir=output) as temporary:
        staging = Path(temporary)
        for texture in texture_paths:
            try:
                report = validate_texture(texture)
                reports.append(report)
                if report.errors:
                    continue
                with Image.open(texture) as image:
                    frames = texture_frames(texture, image.size)
                format_name = "t8" if (texture.stem.startswith("chr_")
                                        or any(word in texture.stem.split("_")
                                               for word in ("poster", "gamebox", "gameboxes"))) else "t4"
                target = staging / (texture.stem + ".tex")
                run_command([sys.executable, "-B", str(TOOLS / "convert_texture.py"), str(texture),
                             "-o", str(target), "--format", format_name, "--frames", str(frames)])
                target.replace(output / target.name)
                converted_textures += 1
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                errors.append(f"{texture.stem}: {error}")
        for source in sources:
            asset_id = source.stem
            print(f"Сборка {asset_id}…", flush=True)
            try:
                asset_stage = staging / asset_id
                asset_stage.mkdir()
                obj = asset_stage / (asset_id + ".obj")
                mesh = obj.with_suffix(".msh")
                preview = obj.with_suffix(".png")
                run_blender(blender, source, "blender_export.py", obj)
                run_command([sys.executable, "-B", str(TOOLS / "convert_model.py"), str(obj), "-o", str(mesh)])
                report = validate_asset(obj, textures, categories.get(asset_id))
                reports.append(report)
                if report.errors:
                    continue
                run_blender(blender, source, "render_preview.py", preview)
                with Image.open(preview) as image:
                    if image.format != "PNG" or image.size != (256, 256):
                        raise ValueError("неверное превью, нужен PNG 256×256")
                    image.load()
                previews.mkdir(parents=True, exist_ok=True)
                for artifact in (obj, obj.with_suffix(".mtl"), mesh):
                    if artifact.exists():
                        artifact.replace(output / artifact.name)
                preview.replace(previews / preview.name)
                completed += 1
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                errors.append(f"{asset_id}: {error}")
    print_reports(reports)
    for error in errors:
        print(f"ОШИБКА: {error}", file=sys.stderr)
    count = len(errors) + sum(bool(report.errors) for report in reports)
    warnings = sum(len(report.warnings) for report in reports)
    print(f"Итого: ассетов {completed}/{len(sources)}, текстур {converted_textures}/{len(texture_paths)}, "
          f"ошибок {count}, предупреждений {warnings}.")
    return int(count > 0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="выгрузить, конвертировать, проверить и создать превью")
    build_parser.add_argument("--only", metavar="ID")
    build_parser.add_argument("--blender", help="Blender: --blender, затем BLENDER, затем blender в PATH")
    build_parser.add_argument("--root", type=Path, default=TOOLS.parent, help="корень проекта/временного дерева")
    build_parser.add_argument("--output", type=Path, help="по умолчанию <root>/build/assets")
    build_parser.add_argument("--previews", type=Path, help="по умолчанию <root>/assets/previews")
    build_parser.add_argument("--category", action="append", default=[], metavar="ID=CATEGORY",
                              help=", ".join(BUDGETS))
    args = parser.parse_args(argv)
    try:
        return build(args)
    except (OSError, ValueError) as error:
        print(f"ОШИБКА: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
