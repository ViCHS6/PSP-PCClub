#!/usr/bin/env python3
"""Внутри Blender: blender -b asset.blend --python tools/blender_export.py -- -o asset.obj."""

import argparse
import math
from pathlib import Path
import sys

import bpy


def asset_object():
    asset_id = Path(bpy.data.filepath).stem
    obj = bpy.data.objects.get(asset_id)
    if obj is None or obj.type != "MESH":
        raise ValueError(f"{asset_id}: нужен mesh-объект с именем файла .blend")
    if obj.mode != "OBJECT":
        raise ValueError(f"{obj.name}: перед выгрузкой выйдите из режима редактирования")
    basis = obj.matrix_basis.to_3x3()
    euler_rotation = (obj.rotation_mode not in {"QUATERNION", "AXIS_ANGLE"}
                      and any(abs(v) > 1e-6 for v in obj.rotation_euler))
    if (any(abs(v - 1.0) > 1e-6 for v in obj.scale) or euler_rotation
            or any(not math.isfinite(basis[r][c])
                   or abs(basis[r][c] - (1.0 if r == c else 0.0)) > 1e-6
                   for r in range(3) for c in range(3))):
        raise ValueError(f"{obj.name}: не применены поворот/масштаб (Ctrl+A → Rotation & Scale)")
    return obj


def srgb(value):
    value = min(1.0, max(0.0, value))
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055


def material_data(material):
    color = material.diffuse_color[:3]
    image = None
    if material.use_nodes and material.node_tree:
        outputs = [n for n in material.node_tree.nodes
                   if n.type == "OUTPUT_MATERIAL" and n.is_active_output]
        surface = outputs[0].inputs.get("Surface") if outputs else None
        shader = surface.links[0].from_node if surface and surface.is_linked else None
        if shader is None or shader.type != "BSDF_PRINCIPLED":
            raise ValueError(f"{material.name}: нужен Principled BSDF у активного Material Output")
        base = shader.inputs["Base Color"]
        color = base.default_value[:3]
        if base.is_linked:
            node = base.links[0].from_node
            if node.type == "TEX_IMAGE" and node.image:
                image = node.image
                color = (1.0, 1.0, 1.0)
            elif ((node.type == "VERTEX_COLOR" and node.layer_name == "Col")
                  or (node.type == "ATTRIBUTE" and node.attribute_name == "Col")):
                color = (1.0, 1.0, 1.0)
            else:
                raise ValueError(f"{material.name}: запеките сложный Base Color в PNG/Col")
    texture = None
    if image:
        texture = Path(bpy.path.abspath(image.filepath, library=image.library)).name
        if not texture.lower().endswith(".png"):
            raise ValueError(f"{material.name}: текстура должна иметь имя исходного PNG")
    return tuple(srgb(c) for c in color), texture


def export_obj(output):
    obj = asset_object()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh(preserve_all_data_layers=True,
                             depsgraph=bpy.context.evaluated_depsgraph_get())
    try:
        if any(len(p.vertices) > 4 for p in mesh.polygons):
            raise ValueError(f"{obj.name}: n-гоны запрещены ART.md §6")
        mesh.calc_loop_triangles()
        if not mesh.loop_triangles:
            raise ValueError(f"{obj.name}: нет граней")
        col = mesh.color_attributes.get("Col")
        if col and col.domain not in {"POINT", "CORNER"}:
            raise ValueError(f"{obj.name}: Col должен быть POINT или CORNER")
        uv = mesh.uv_layers.active
        materials = [slot.material for slot in evaluated.material_slots]
        if any(m is None for m in materials):
            raise ValueError(f"{obj.name}: пустой слот материала")
        mtl_lines = []
        for material in dict.fromkeys(materials):
            kd, texture = material_data(material)
            mtl_lines += [f"newmtl {material.name}", "Kd " + " ".join(f"{v:.9g}" for v in kd)]
            if texture:
                mtl_lines.append(f'map_Kd "{texture}"')
        lines = [f"# Blender Z-up, -Y forward -> engine Y-up, +Z front; local origin",
                 f"o {obj.name}"]
        if materials:
            lines.append(f'mtllib "{output.with_suffix(".mtl").name}"')
        vertices, uvs, faces = [], [], []
        seen = {}
        active_material = None
        for tri in mesh.loop_triangles:
            if materials:
                material = materials[tri.material_index].name
                if material != active_material:
                    faces.append(f"usemtl {material}")
                    active_material = material
            indices = []
            for loop_index in tri.loops:
                vertex_index = mesh.loops[loop_index].vertex_index
                x, y, z = mesh.vertices[vertex_index].co
                # Поворот на -90° вокруг X сохраняет CCW и переводит -Y в +Z.
                position = (x, z, -y)
                color = tuple(col.data[vertex_index if col.domain == "POINT" else loop_index]
                              .color_srgb[:3]) if col else ()
                texcoord = tuple(uv.data[loop_index].uv) if uv else ()
                values = position + color + texcoord
                if not all(math.isfinite(v) for v in values):
                    raise ValueError(f"{obj.name}: NaN/Inf в геометрии, UV или Col")
                key = (position, color, texcoord)
                if key not in seen:
                    seen[key] = len(vertices) + 1
                    vertices.append("v " + " ".join(f"{v:.9g}" for v in position + color))
                    if uv:
                        uvs.append("vt " + " ".join(f"{v:.9g}" for v in texcoord))
                index = seen[key]
                indices.append(f"{index}/{index}" if uv else str(index))
            faces.append("f " + " ".join(indices))
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("\n".join(lines + vertices + uvs + faces) + "\n", encoding="utf-8")
        if materials:
            output.with_suffix(".mtl").write_text("\n".join(mtl_lines) + "\n", encoding="utf-8")
        print(f"{obj.name}: {len(mesh.loop_triangles)} треугольников -> {output}")
    finally:
        evaluated.to_mesh_clear()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    try:
        export_obj(args.output.resolve())
    except (ValueError, OSError, RuntimeError) as error:
        print(f"ОШИБКА: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
